//! Recompute on the edge into a phi a constant-offset address that its own
//! block only loads and stores through.
//!
//!   b:    %a = Add %s, 3                    b:    %a = gep %s, 3
//!         %x = load %a                            %x = load %a
//!         condbr %c, next, exit       =>          condbr %c, next, e
//!                                           e:    %a2 = gep %s, 3; br exit
//!   exit: %r = phi [%a, b], ...             exit: %r = phi [%a2, e], ...
//!
//! A match loop that stops at the first differing byte (`while (*++scan ==
//! *++match && ...)`) leaves with the address it reached, so each step's
//! address was computed into a register of its own for the exit's phi.
//! Recomputed on the way out, the step's address is used only by its load,
//! and the backend folds it there (`ldrb w0, [x1, #3]`): an `add` less per
//! byte compared. The new block holds what phi elimination would have put
//! on that critical edge anyway; an edge from a block with one successor
//! gets the recomputation at the end of the block instead.
//!
//! Pointer increments are lowered as integer adds; one whose every use is
//! the address of a load or store becomes a GEP here, which the backend
//! folds the same way. 64-bit targets only.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use crate::ir::reexports::{BasicBlock, BlockId, Instruction, IrBinOp, IrFunction, IrModule, Operand, Terminator, Value};

/// A value to recompute on an edge: (value, base, offset).
type Recompute = (u32, Value, i64);

pub(crate) fn split_module(module: &mut IrModule) -> usize {
    if crate::common::types::target_ptr_size() != 8 {
        return 0;
    }
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    module.functions.iter_mut()
        .filter(|f| !f.is_declaration && f.blocks.len() > 1)
        .map(|f| split_function(f, &mut next_label))
        .sum()
}

/// `base + k` for a constant k, as a GEP or a 64-bit add.
fn const_offset(inst: &Instruction) -> Option<(Value, Value, i64)> {
    match inst {
        Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } => Some((*dest, *base, c.to_i64_exact()?)),
        Instruction::BinOp { dest, op: IrBinOp::Add, lhs, rhs, ty: IrType::I64 | IrType::U64 } => match (lhs, rhs) {
            (Operand::Value(b), Operand::Const(c)) | (Operand::Const(c), Operand::Value(b)) => Some((*dest, *b, c.to_i64_exact()?)),
            _ => None,
        },
        _ => None,
    }
    .filter(|&(_, _, k)| k >= i32::MIN as i64 && k <= i32::MAX as i64)
}

fn split_function(func: &mut IrFunction, next_label: &mut u32) -> usize {
    let mut defs: FxHashMap<u32, (Value, i64)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some((d, b, k)) = const_offset(inst) {
            defs.insert(d.0, (b, k));
        }
    }
    if defs.is_empty() {
        return 0;
    }
    // Offsets of offsets from their first base (`(s + 2) + 4` is `s + 6`),
    // so each step is its own access address rather than the next one's base.
    let roots: FxHashMap<u32, (Value, i64)> = defs.iter().map(|(&v, &(mut b, mut k))| {
        for _ in 0..8 {
            match defs.get(&b.0) {
                Some(&(b2, k2)) if (k + k2) >= i32::MIN as i64 && (k + k2) <= i32::MAX as i64 => {
                    b = b2;
                    k += k2;
                }
                _ => break,
            }
        }
        (v, (b, k))
    }).collect();
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        let Some((d, b, _)) = const_offset(inst) else { continue };
        let (root, k) = roots[&d.0];
        if root == b {
            continue;
        }
        let off = Operand::Const(crate::ir::reexports::IrConst::I64(k));
        match inst {
            Instruction::GetElementPtr { base, offset, .. } => {
                *base = root;
                *offset = off;
            }
            Instruction::BinOp { lhs, rhs, .. } => {
                *lhs = Operand::Value(root);
                *rhs = off;
            }
            _ => {}
        }
    }
    let defs = roots;
    // Which of them are used other than as an access address or by a phi.
    let mut other: FxHashSet<u32> = FxHashSet::default();
    let mut accessed: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ptr, .. } => { accessed.insert(ptr.0); }
                Instruction::Store { val, ptr, .. } => {
                    accessed.insert(ptr.0);
                    if let Operand::Value(v) = val {
                        other.insert(v.0);
                    }
                }
                Instruction::Phi { .. } => {}
                _ => inst.for_each_used_value(|v| { other.insert(v); }),
            }
        }
        block.terminator.for_each_used_value(|v| { other.insert(v); });
    }
    // Phi entries to recompute: (pred, phi block) -> [(value, base, k)].
    let index: FxHashMap<BlockId, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    let mut edges: FxHashMap<(usize, usize), Vec<Recompute>> = FxHashMap::default();
    for (ei, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            let Instruction::Phi { incoming, .. } = inst else { continue };
            for (op, l) in incoming {
                let Operand::Value(v) = op else { continue };
                let Some(&(base, k)) = defs.get(&v.0) else { continue };
                if other.contains(&v.0) || !accessed.contains(&v.0) {
                    continue;
                }
                let Some(&pi) = index.get(l) else { continue };
                let list = edges.entry((pi, ei)).or_default();
                if !list.iter().any(|e| e.0 == v.0) {
                    list.push((v.0, base, k));
                }
            }
        }
    }
    // Every phi reading such a value must get it recomputed, or the value
    // stays live and nothing is gained.
    let mut kept: FxHashSet<u32> = FxHashSet::default();
    let mut ok_edges: Vec<((usize, usize), Vec<Recompute>)> = Vec::new();
    let mut edge_list: Vec<_> = edges.into_iter().collect();
    edge_list.sort_unstable_by_key(|e| e.0);
    for ((pi, ei), list) in edge_list {
        let label = func.blocks[ei].label;
        let pred = &func.blocks[pi];
        let mut to_e = 0;
        let plain = match &pred.terminator {
            Terminator::Branch(t) => { to_e += (*t == label) as u32; true }
            Terminator::CondBranch { true_label, false_label, .. } => {
                to_e += (*true_label == label) as u32 + (*false_label == label) as u32;
                true
            }
            _ => false,
        };
        let asm_goto = pred.instructions.iter().any(|i| matches!(i, Instruction::InlineAsm { goto_labels, .. } if !goto_labels.is_empty()));
        if !plain || to_e != 1 || asm_goto {
            kept.extend(list.iter().map(|e| e.0));
            continue;
        }
        ok_edges.push(((pi, ei), list));
    }
    ok_edges.retain(|(_, list)| list.iter().all(|e| !kept.contains(&e.0)));
    if ok_edges.is_empty() {
        return 0;
    }

    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut new_blocks: Vec<BasicBlock> = Vec::new();
    let mut count = 0;
    for ((pi, ei), list) in ok_edges {
        let e_label = func.blocks[ei].label;
        let p_label = func.blocks[pi].label;
        let mut insts = Vec::with_capacity(list.len());
        let mut renamed: FxHashMap<u32, Value> = FxHashMap::default();
        for &(v, base, k) in &list {
            let d = Value(next_id);
            next_id += 1;
            insts.push(Instruction::GetElementPtr {
                dest: d, base, offset: Operand::Const(crate::ir::reexports::IrConst::I64(k)), ty: IrType::Ptr,
            });
            renamed.insert(v, d);
        }
        count += insts.len();
        let one_succ = matches!(func.blocks[pi].terminator, Terminator::Branch(_));
        let from = if one_succ {
            let pred = &mut func.blocks[pi];
            if !pred.source_spans.is_empty() {
                let span = *pred.source_spans.last().unwrap();
                pred.source_spans.extend(std::iter::repeat_n(span, insts.len()));
            }
            pred.instructions.extend(insts);
            p_label
        } else {
            let n = BlockId(*next_label);
            *next_label += 1;
            if let Terminator::CondBranch { true_label, false_label, .. } = &mut func.blocks[pi].terminator {
                for t in [true_label, false_label] {
                    if *t == e_label {
                        *t = n;
                    }
                }
            }
            let span = func.blocks[pi].source_spans.last().copied();
            let source_spans = span.map(|s| vec![s; insts.len()]).unwrap_or_default();
            new_blocks.push(BasicBlock { label: n, instructions: insts, terminator: Terminator::Branch(e_label), source_spans });
            n
        };
        for inst in &mut func.blocks[ei].instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                for (op, l) in incoming.iter_mut() {
                    if *l != p_label {
                        continue;
                    }
                    *l = from;
                    if let Operand::Value(v) = op {
                        if let Some(&d) = renamed.get(&v.0) {
                            *op = Operand::Value(d);
                        }
                    }
                }
            }
        }
    }
    func.blocks.extend(new_blocks);

    // Adds used only as access addresses (and no longer by phis) are GEPs.
    let mut still_phi: FxHashSet<u32> = FxHashSet::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Phi { incoming, .. } = inst {
            for (op, _) in incoming {
                if let Operand::Value(v) = op {
                    still_phi.insert(v.0);
                }
            }
        }
    }
    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
        if let Instruction::BinOp { dest, .. } = inst {
            let d = dest.0;
            if accessed.contains(&d) && !other.contains(&d) && !still_phi.contains(&d) {
                if let Some((dest, base, k)) = const_offset(inst) {
                    *inst = Instruction::GetElementPtr {
                        dest, base, offset: Operand::Const(crate::ir::reexports::IrConst::I64(k)), ty: IrType::Ptr,
                    };
                }
            }
        }
    }
    func.next_value_id = next_id;
    count
}

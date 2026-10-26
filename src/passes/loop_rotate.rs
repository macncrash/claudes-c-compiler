//! Loop rotation by header duplication.
//!
//! A `for`/`while` loop is lowered top-tested: the header computes the
//! condition and branches into the body or out, and every iteration ends
//! with an unconditional branch back to it. Copying a small header to the
//! end of each latch turns the loop bottom-tested, so an iteration runs one
//! conditional branch instead of a taken branch plus a conditional one, and
//! the original header remains as the entry guard:
//!
//!   header:  %c = cmp slt %i, %n             header:  %c = cmp slt %i, %n
//!            condbr %c, body, exit                    condbr %c, body, exit
//!   body:    ...                         =>   body:    ...
//!            %i = copy %next                          %i = copy %next
//!            br header                                %c2 = cmp slt %i, %n
//!                                                     condbr %c2, body, exit
//!
//! Runs after phi elimination, where executing the header's instructions at
//! the end of the latch is exactly what the original path did. Values the
//! header defines get fresh names in the copy; one also used outside the
//! header is turned into a variable assigned by a Copy in both places, like
//! a lowered phi.

use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Operand, Terminator, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};

/// Most instructions a header may have to be duplicated.
const MAX_HEADER_INSTS: usize = 6;

pub(crate) fn rotate_loops(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        rotate_function(func);
    }
}

pub(crate) fn rotate_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let label_to_idx = analysis::build_label_map(func);
    let dominates = |a: usize, mut b: usize| loop {
        if a == b {
            return true;
        }
        let up = cfg.idom[b];
        if up == b || up == usize::MAX {
            return false;
        }
        b = up;
    };

    // Definitions per value, and whether each value is used outside the
    // block it is defined in (checked per header below).
    if func.next_value_id == 0 {
        func.next_value_id = func.blocks.iter().flat_map(|b| &b.instructions)
            .filter_map(|i| i.dest()).map(|v| v.0 + 1).max().unwrap_or(0);
    }
    let n = func.next_value_id as usize;
    let mut defs = vec![0u8; n];
    let mut use_blocks: Vec<Option<usize>> = vec![None; n];
    let mut used_in_several = vec![false; n];
    for (b, block) in func.blocks.iter().enumerate() {
        let mut note = |v: u32| {
            let v = v as usize;
            if v >= n {
                return;
            }
            match use_blocks[v] {
                None => use_blocks[v] = Some(b),
                Some(u) if u != b => used_in_several[v] = true,
                _ => {}
            }
        };
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                if (d.0 as usize) < n {
                    defs[d.0 as usize] = defs[d.0 as usize].saturating_add(1);
                }
            }
            inst.for_each_used_value(&mut note);
        }
        block.terminator.for_each_used_value(&mut note);
    }

    let mut rotated = 0;
    let mut self_loops = Vec::new();
    for latch in 0..func.blocks.len() {
        let Terminator::Branch(target) = func.blocks[latch].terminator else { continue };
        let Some(&header) = label_to_idx.get(&target) else { continue };
        if header == latch || !dominates(header, latch) {
            continue;
        }
        let hb = &func.blocks[header];
        if !matches!(hb.terminator, Terminator::CondBranch { .. }) || hb.instructions.len() > MAX_HEADER_INSTS {
            continue;
        }
        let duplicable = hb.instructions.iter().all(|inst| {
            let simple = matches!(inst,
                Instruction::Cmp { .. } | Instruction::BinOp { .. } | Instruction::UnaryOp { .. }
                | Instruction::Cast { .. } | Instruction::Copy { .. } | Instruction::GetElementPtr { .. }
                | Instruction::Load { .. } | Instruction::Select { .. });
            simple && inst.dest().is_some_and(|d| (d.0 as usize) < defs.len())
        });
        if !duplicable {
            continue;
        }

        // A value used outside the header becomes a variable, as a lowered
        // phi is: the header computes a fresh value and copies it over, and
        // so will the duplicate. (A Copy already is such a definition.)
        let mut k = 0;
        while k < func.blocks[header].instructions.len() {
            let inst = &func.blocks[header].instructions[k];
            let d = inst.dest().unwrap().0 as usize;
            // (Values this pass created are used only in their own block.)
            let local = d >= n || (!used_in_several[d] && use_blocks[d].is_none_or(|u| u == header));
            if defs[d] == 1 && !local {
                defs[d] = 2;
                if !matches!(inst, Instruction::Copy { .. }) {
                    let fresh = Value(func.next_value_id);
                    func.next_value_id += 1;
                    defs.push(1);
                    let hb = &mut func.blocks[header];
                    set_dest(&mut hb.instructions[k], fresh);
                    hb.instructions.insert(k + 1, Instruction::Copy { dest: Value(d as u32), src: Operand::Value(fresh) });
                    if hb.source_spans.len() + 1 == hb.instructions.len() {
                        let span = hb.source_spans[k];
                        hb.source_spans.insert(k + 1, span);
                    }
                    k += 1;
                }
            }
            k += 1;
        }

        // Copy the header, renaming its single-definition values.
        let hb = &func.blocks[header];
        let mut remap: Vec<Option<Operand>> = vec![None; func.next_value_id as usize];
        let mut copies = Vec::with_capacity(hb.instructions.len());
        let mut next_id = func.next_value_id;
        for inst in &hb.instructions {
            let mut copy = inst.clone();
            replace_operands_in_instruction(&mut copy, &remap);
            let d = inst.dest().unwrap();
            if defs[d.0 as usize] == 1 {
                let fresh = Value(next_id);
                next_id += 1;
                defs.push(1);
                set_dest(&mut copy, fresh);
                remap[d.0 as usize] = Some(Operand::Value(fresh));
            }
            copies.push(copy);
        }
        let mut term = hb.terminator.clone();
        replace_operands_in_terminator(&mut term, &remap);
        let spans = hb.source_spans.clone();
        func.next_value_id = next_id;

        let lb = &mut func.blocks[latch];
        if lb.source_spans.len() == lb.instructions.len() && spans.len() == copies.len() {
            lb.source_spans.extend(spans);
        }
        lb.instructions.extend(copies);
        if matches!(term, Terminator::CondBranch { true_label, false_label, .. }
            if true_label == lb.label || false_label == lb.label)
        {
            self_loops.push((lb.label, target));
        }
        lb.terminator = term;
        rotated += 1;
    }

    // A one-block loop rotates into its back edge's block (the trampoline
    // holding the phi copies), now a loop of its own entered only from the
    // old header. Phi elimination put that block before the header; right
    // after it, the header falls into the loop:
    //
    //   pre:     ...; b header                  pre:     ...
    //   latch:   ...; b.lt latch, exit    =>    header:  ...; b.ge exit
    //   header:  ...; b.lt latch, exit          latch:   ...; b.lt latch
    for (latch, header) in self_loops {
        let Some(from) = func.blocks.iter().position(|b| b.label == latch) else { continue };
        let block = func.blocks.remove(from);
        let at = func.blocks.iter().position(|b| b.label == header).map_or(from, |h| h + 1);
        func.blocks.insert(at, block);
    }
    rotated
}

/// Most instructions a latch may have to be copied into the blocks that
/// jump to it, and most copies of one latch.
const MAX_LATCH_INSTS: usize = 4;
const MAX_LATCH_COPIES: usize = 8;

/// Copy a small latch that branches back conditionally into the loop blocks
/// that jump to it, so each path through the loop ends with the latch's own
/// test instead of a jump to it:
///
///   lit:    ...; b latch                      lit:    ...; ldr w0, [x10, #n]
///   match:  ...; b latch            =>                cmp x14, x0; b.lo header
///   latch:  ldr w0, [x10, #n]                 match:  ...; (likewise)
///           cmp x14, x0; b.lo header          latch:  (the block falling into it)
///
/// compress_block's literal and match paths each took a jump to the shared
/// loop test. The block laid out just before the latch keeps falling into
/// it. The latch's values must be used only in it (their copies get fresh
/// names), and its instructions be simple ones.
pub(crate) fn duplicate_latches(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 3 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let label_to_idx = analysis::build_label_map(func);
    let dominates = |a: usize, mut b: usize| loop {
        if a == b {
            return true;
        }
        let up = cfg.idom[b];
        if up == b || up == usize::MAX {
            return false;
        }
        b = up;
    };
    // Blocks each value is used in.
    let mut used_in: crate::common::fx_hash::FxHashMap<u32, Vec<usize>> = crate::common::fx_hash::FxHashMap::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| used_in.entry(v).or_default().push(b));
        }
        block.terminator.for_each_used_value(|v| used_in.entry(v).or_default().push(b));
    }
    if func.next_value_id == 0 {
        func.next_value_id = func.max_value_id() + 1;
    }
    let mut copied = 0;
    for latch in 0..func.blocks.len() {
        let lb = &func.blocks[latch];
        let Terminator::CondBranch { true_label, false_label, .. } = lb.terminator else { continue };
        // A back edge: to a header, or to the block holding its phi copies.
        let header_of = |t: usize| match func.blocks[t].terminator {
            Terminator::Branch(h) if !dominates(t, latch) => label_to_idx.get(&h).copied(),
            _ => Some(t),
        };
        let back = [true_label, false_label].iter().any(|l| label_to_idx.get(l).and_then(|&t| header_of(t))
            .is_some_and(|h| h != latch && dominates(h, latch)));
        if !back || lb.instructions.is_empty() || lb.instructions.len() > MAX_LATCH_INSTS {
            continue;
        }
        let local = lb.instructions.iter().all(|inst| {
            matches!(inst, Instruction::Cmp { .. } | Instruction::BinOp { .. } | Instruction::Cast { .. }
                | Instruction::GetElementPtr { .. } | Instruction::Load { .. })
                && inst.dest().is_some_and(|d| used_in.get(&d.0).is_none_or(|bs| bs.iter().all(|&b| b == latch)))
        });
        // Only a latch that rereads its bound from memory (`sx <
        // s->sym_next`): copying a bare compare of values the path just
        // assigned measured slower in SQLite's searches, the copy reading the
        // value through an extra move.
        if !local || !lb.instructions.iter().any(|i| matches!(i, Instruction::Load { .. })) {
            continue;
        }
        let label = lb.label;
        let preds: Vec<usize> = cfg.preds.row(latch).iter().map(|&p| p as usize)
            .filter(|&p| p != latch && p + 1 != latch && matches!(func.blocks[p].terminator, Terminator::Branch(l) if l == label))
            .take(MAX_LATCH_COPIES)
            .collect();
        for p in preds {
            let lb = &func.blocks[latch];
            let mut remap: Vec<Option<Operand>> = vec![None; func.next_value_id as usize];
            let mut copies = Vec::with_capacity(lb.instructions.len());
            for inst in &lb.instructions {
                let mut copy = inst.clone();
                replace_operands_in_instruction(&mut copy, &remap);
                let d = inst.dest().unwrap();
                let fresh = Value(func.next_value_id + copies.len() as u32);
                set_dest(&mut copy, fresh);
                if (d.0 as usize) < remap.len() {
                    remap[d.0 as usize] = Some(Operand::Value(fresh));
                }
                copies.push(copy);
            }
            let mut term = lb.terminator.clone();
            replace_operands_in_terminator(&mut term, &remap);
            let spans = lb.source_spans.clone();
            func.next_value_id += copies.len() as u32;
            let pb = &mut func.blocks[p];
            if pb.source_spans.len() == pb.instructions.len() && spans.len() == copies.len() {
                pb.source_spans.extend(spans);
            } else {
                pb.source_spans.clear();
            }
            pb.instructions.extend(copies);
            pb.terminator = term;
            copied += 1;
        }
    }
    copied
}

/// Rename the value an instruction defines (one of the kinds rotation copies).
fn set_dest(inst: &mut Instruction, v: Value) {
    match inst {
        Instruction::Cmp { dest, .. } | Instruction::BinOp { dest, .. } | Instruction::UnaryOp { dest, .. }
        | Instruction::Cast { dest, .. } | Instruction::Copy { dest, .. } | Instruction::GetElementPtr { dest, .. }
        | Instruction::Load { dest, .. } | Instruction::Select { dest, .. } => *dest = v,
        _ => unreachable!("loop rotation copies only simple instructions"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::IrType;
    use crate::ir::reexports::{BasicBlock, BlockId, IrBinOp, IrCmpOp, IrConst};

    /// A one-block loop after phi elimination: its back edge's trampoline
    /// (holding the phi copy) laid out before the header.
    #[test]
    fn rotated_one_block_loop_follows_its_header() {
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        func.blocks = vec![
            block(0, vec![Instruction::Copy { dest: Value(5), src: Operand::Const(IrConst::I64(0)) }], Terminator::Branch(BlockId(2))),
            block(1, vec![Instruction::Copy { dest: Value(5), src: v(3) }], Terminator::Branch(BlockId(2))),
            block(2, vec![
                Instruction::BinOp { dest: Value(3), op: IrBinOp::Add, lhs: v(5), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::Cmp { dest: Value(4), op: IrCmpOp::Slt, lhs: v(3), rhs: Operand::Const(IrConst::I64(100)), ty: IrType::I64 },
            ], Terminator::CondBranch { cond: v(4), true_label: BlockId(1), false_label: BlockId(3) }),
            block(3, vec![], Terminator::Return(Some(v(3)))),
        ];
        func.next_value_id = 6;
        assert_eq!(rotate_function(&mut func), 1);
        let order: Vec<u32> = func.blocks.iter().map(|b| b.label.0).collect();
        assert_eq!(order, [0, 2, 1, 3]);
        assert!(matches!(func.blocks[2].terminator, Terminator::CondBranch { true_label: BlockId(1), false_label: BlockId(3), .. }));
    }

    /// header -> a | b; a jumps to the latch, b falls into it; the latch
    /// reloads the bound and branches back through the phi-copy block.
    fn two_path_loop(load: bool) -> IrFunction {
        use crate::common::types::AddressSpace;
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        let bound = if load {
            Instruction::Load { dest: Value(20), ptr: Value(1), ty: IrType::I64, seg_override: AddressSpace::Default, volatile: false }
        } else {
            Instruction::BinOp { dest: Value(20), op: IrBinOp::Add, lhs: v(1), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 }
        };
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![Instruction::BinOp { dest: Value(5), op: IrBinOp::Add, lhs: v(4), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 }],
                Terminator::CondBranch { cond: v(2), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, vec![], Terminator::Branch(BlockId(4))),
            block(3, vec![], Terminator::Branch(BlockId(4))),
            block(4, vec![bound, Instruction::Cmp { dest: Value(21), op: IrCmpOp::Slt, lhs: v(5), rhs: v(20), ty: IrType::I64 }],
                Terminator::CondBranch { cond: v(21), true_label: BlockId(5), false_label: BlockId(6) }),
            block(5, vec![Instruction::Copy { dest: Value(4), src: v(5) }], Terminator::Branch(BlockId(1))),
            block(6, vec![], Terminator::Return(Some(v(5)))),
        ];
        func.next_value_id = 30;
        func
    }

    #[test]
    fn latch_rereading_its_bound_copied_into_the_path_jumping_to_it() {
        let mut func = two_path_loop(true);
        assert_eq!(duplicate_latches(&mut func), 1);
        let a = &func.blocks[2];
        assert!(matches!(a.instructions[..], [Instruction::Load { dest: Value(30), .. }, Instruction::Cmp { dest: Value(31), rhs: Operand::Value(Value(30)), .. }]),
            "{:?}", a.instructions);
        assert!(matches!(a.terminator, Terminator::CondBranch { cond: Operand::Value(Value(31)), true_label: BlockId(5), false_label: BlockId(6) }));
        // The block falling into the latch keeps it.
        assert!(matches!(func.blocks[3].terminator, Terminator::Branch(BlockId(4))));
        // A latch computing its bound without memory stays shared.
        let mut func = two_path_loop(false);
        assert_eq!(duplicate_latches(&mut func), 0);
    }
}

//! Induction variable widening.
//!
//! A signed 32-bit loop counter on a 64-bit target costs a sign extension
//! per iteration: the narrowed `i + 1` is an I32 add, kept in canonical
//! sign-extended form, and array indexing sign-extends `i` again. Since
//! signed overflow is undefined, the counter can instead be kept as a 64-bit
//! value equal to `sext(i)` throughout:
//!
//!   %i = phi I32 [%init, pre], [%next, latch]     %i = phi I64 [sext %init, pre], [%next, latch]
//!   %next = Add I32 %i, 1                     =>  %next = Add I64 %i, 1
//!   %w = Cast %i, I32 -> I64                      %w = Copy %i
//!   %c = Cmp slt I32 %i, %n                       %c = Cmp slt I64 %i, sext %n
//!
//! Compares against constants or loop-invariant values are widened (sign
//! extension preserves both signed and unsigned order of I32 values); any
//! other use reads an explicit truncation. Runs once, late (after narrowing
//! has produced the I32 adds), on 64-bit targets only.

use crate::common::fx_hash::FxHashMap;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{BasicBlock, Instruction, IrBinOp, IrCmpOp, IrConst, IrFunction, Operand, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

pub(crate) fn widen_function(func: &mut IrFunction) -> usize {
    if crate::common::types::target_is_32bit() || func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut widened = 0;
    // Each truncation made so far, and the widened counter it truncates.
    let mut narrow: FxHashMap<u32, Value> = FxHashMap::default();
    // Likewise for unsigned counters (zero extensions).
    let mut unarrow: FxHashMap<u32, Value> = FxHashMap::default();
    for lp in loop_analysis::merge_loops_by_header(loops) {
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        let latches: Vec<usize> = cfg.preds.row(lp.header).iter().map(|&p| p as usize)
            .filter(|p| lp.body.contains(p)).collect();
        if latches.len() != 1 {
            continue;
        }
        let (pre_label, latch_label) = (func.blocks[pre].label, func.blocks[latches[0]].label);
        let phis: Vec<(Value, bool)> = func.blocks[lp.header].instructions.iter()
            .filter_map(|i| match i {
                Instruction::Phi { dest, ty: IrType::I32, .. } => Some((*dest, false)),
                Instruction::Phi { dest, ty: IrType::U32, .. } => Some((*dest, true)),
                _ => None,
            })
            .collect();
        for (phi, unsigned) in phis {
            let maps = if unsigned { &mut unarrow } else { &mut narrow };
            if widen_iv(func, &lp.body, lp.header, pre, latches[0], phi, unsigned, pre_label, latch_label, &cfg.idom, maps) {
                widened += 1;
            }
        }
    }
    // A counter carried from one loop into the next (a partition's `i`, kept
    // across swaps) is truncated where the first loop leaves it and sign
    // extended where the next takes it: the wide value is that extension.
    // Likewise any other extension of a value already extended once.
    for block in &mut func.blocks {
        for inst in &mut block.instructions {
            if let Instruction::Cast { dest, src: Operand::Value(v), from_ty: from @ (IrType::I32 | IrType::U32), to_ty: IrType::I64 | IrType::U64 } = *inst {
                let maps = if from == IrType::U32 { &unarrow } else { &narrow };
                if let Some(&wide) = maps.get(&v.0).filter(|w| **w != dest) {
                    *inst = Instruction::Copy { dest, src: Operand::Value(wide) };
                }
            }
        }
    }
    widened
}

/// The constant of `op` as an i64, if it is an integer constant.
fn const_i64(op: &Operand) -> Option<i64> {
    match op {
        Operand::Const(c) => c.to_i64_exact(),
        _ => None,
    }
}

#[allow(clippy::too_many_arguments)]
fn widen_iv(func: &mut IrFunction, body: &crate::common::fx_hash::FxHashSet<usize>, header: usize, pre: usize,
            latch: usize, phi: Value, unsigned: bool, pre_label: crate::ir::reexports::BlockId,
            latch_label: crate::ir::reexports::BlockId, idom: &[usize], narrow: &mut FxHashMap<u32, Value>) -> bool {
    let narrow_ty = if unsigned { IrType::U32 } else { IrType::I32 };
    // A constant extended the way the counter is.
    let ext_op = |k: i64| if unsigned { k as u32 as i64 } else { k as i32 as i64 };
    // The phi: [init from the preheader, next from the latch].
    let Some((init, next)) = func.blocks[header].instructions.iter().find_map(|i| match i {
        Instruction::Phi { dest, incoming, .. } if *dest == phi && incoming.len() == 2 => {
            let init = incoming.iter().find(|(_, l)| *l == pre_label)?.0;
            let Operand::Value(next) = incoming.iter().find(|(_, l)| *l == latch_label)?.0 else { return None };
            Some((init, next))
        }
        _ => None,
    }) else { return false };
    // next = Add/Sub I32 phi, const, in the loop.
    let mut next_at = None;
    for &b in body {
        for (k, inst) in func.blocks[b].instructions.iter().enumerate() {
            if let Instruction::BinOp { dest, op: op @ (IrBinOp::Add | IrBinOp::Sub), lhs, rhs, ty } = inst {
                if *ty != narrow_ty {
                    continue;
                }
                let is_phi = |op: &Operand| matches!(op, Operand::Value(v) if *v == phi);
                // An unsigned counter steps by exactly 1 (see below).
                let step_ok = |c: &Operand| const_i64(c).is_some_and(|k| !unsigned || (*op == IrBinOp::Add && k == 1));
                let steps = (is_phi(lhs) && step_ok(rhs))
                    || (*op == IrBinOp::Add && is_phi(rhs) && step_ok(lhs));
                if *dest == next && steps {
                    next_at = Some((b, k));
                }
            }
        }
    }
    let Some((next_block, next_idx)) = next_at else { return false };

    let is_iv = |v: u32| v == phi.0 || v == next.0;
    // A value sign extended for the widened counter (its start, or the other
    // side of a compare) must be available before the loop: not computed
    // after it, for a compare after it.
    let available_before = |func: &IrFunction, v: u32| {
        let v = narrow.get(&v).map_or(v, |w| w.0);
        let Some(def) = func.blocks.iter().position(|b| b.instructions.iter().any(|i| i.dest().is_some_and(|d| d.0 == v)))
        else { return true };
        let mut b = pre;
        loop {
            if b == def {
                return true;
            }
            if idom[b] == b || idom[b] == usize::MAX {
                return false;
            }
            b = idom[b];
        }
    };
    if let Operand::Const(c) = init {
        if c.to_i64_exact().is_none() {
            return false;
        }
    }
    // Unsigned wrapping is defined, so an unsigned counter widens only when
    // it cannot wrap: it starts at a constant below 2^32 - 1, steps by 1, and
    // the one latch goes back only while `next <u B`. Then the counter stays
    // below 2^32 - 1 in the loop and `next` at most 2^32 - 1 (B being a
    // 32-bit value), so each equals the zero extension of its narrow value,
    // in the loop and after it.
    if unsigned {
        let Operand::Const(c) = init else { return false };
        if !c.to_i64_exact().is_some_and(|k| (0..0xffff_fffe).contains(&k)) {
            return false;
        }
        let header_label = func.blocks[header].label;
        let crate::ir::reexports::Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label } = func.blocks[latch].terminator
        else { return false };
        let back_if_true = true_label == header_label && false_label != header_label;
        let back_if_false = false_label == header_label && true_label != header_label;
        let is_next = |op: &Operand| matches!(op, Operand::Value(v) if *v == next);
        let bounded = func.blocks[latch].instructions.iter().any(|i| match i {
            Instruction::Cmp { dest, op, lhs, rhs, ty: IrType::U32 } if *dest == c => match op {
                IrCmpOp::Ult => back_if_true && is_next(lhs) && !is_next(rhs),
                IrCmpOp::Ugt => back_if_true && is_next(rhs) && !is_next(lhs),
                IrCmpOp::Uge => back_if_false && is_next(lhs) && !is_next(rhs),
                IrCmpOp::Ule => back_if_false && is_next(rhs) && !is_next(lhs),
                _ => false,
            },
            _ => false,
        });
        if !bounded {
            return false;
        }
    }
    let label_to_idx = analysis::build_label_map(func);

    // Find the uses that widen naturally. Every other use reads a
    // truncation: one per block, placed before the block's first such use (a
    // phi's incoming value is used at the end of its predecessor), so no
    // narrow copy of the counter stays live across the loop.
    let mut compares: Vec<(usize, usize)> = Vec::new();
    let mut extensions = 0;
    let mut trunc_at: FxHashMap<(usize, u32), usize> = FxHashMap::default();
    let mut note = |b: usize, at: usize, v: u32| {
        if is_iv(v) {
            let first = trunc_at.entry((b, v)).or_insert(at);
            *first = (*first).min(at);
        }
    };
    for (b, block) in func.blocks.iter().enumerate() {
        for (k, inst) in block.instructions.iter().enumerate() {
            let mut uses_iv = false;
            inst.for_each_used_value(|v| uses_iv |= is_iv(v));
            if !uses_iv || (b == next_block && k == next_idx) || matches!(inst, Instruction::Phi { dest, .. } if *dest == phi) {
                continue;
            }
            match inst {
                Instruction::Cast { src: Operand::Value(v), from_ty, to_ty: IrType::I64 | IrType::U64, .. }
                    if *from_ty == narrow_ty && is_iv(v.0) => extensions += 1,
                Instruction::Phi { incoming, .. } => {
                    for (op, label) in incoming {
                        if let Operand::Value(v) = op {
                            let Some(&p) = label_to_idx.get(label) else { return false };
                            note(p, func.blocks[p].instructions.len(), v.0);
                        }
                    }
                }
                Instruction::Cmp { ty, op, lhs, rhs, .. } if *ty == narrow_ty => {
                    let other = if matches!(lhs, Operand::Value(v) if is_iv(v.0)) { rhs } else { lhs };
                    // Zero extension keeps unsigned order and equality, not
                    // signed order; it is placed after the value's definition.
                    let signed_cmp = matches!(op, IrCmpOp::Slt | IrCmpOp::Sle | IrCmpOp::Sgt | IrCmpOp::Sge);
                    let ok = !(unsigned && signed_cmp) && match other {
                        Operand::Const(c) => c.to_i64_exact().is_some(),
                        Operand::Value(v) => !is_iv(v.0) && (unsigned || available_before(func, v.0)),
                    };
                    if ok {
                        compares.push((b, k));
                    } else {
                        inst.for_each_used_value(|v| note(b, k, v));
                    }
                }
                _ => inst.for_each_used_value(|v| note(b, k, v)),
            }
        }
        block.terminator.for_each_used_value(|v| note(b, block.instructions.len(), v));
    }

    // Nothing makes an unsigned counter's 32-bit operations wrong, so it
    // widens only to save more extensions than it adds truncations (even
    // ones after the loop: SQLite ran more instructions counting only those
    // in it).
    if unsigned && extensions <= trunc_at.len() {
        return false;
    }
    let mut next_id = func.next_value_id;
    let mut fresh = || { let v = Value(next_id); next_id += 1; v };
    let trunc: FxHashMap<(usize, u32), Value> = trunc_at.keys().map(|&key| (key, fresh())).collect();

    // Sign extensions go at the end of a preheader that only enters the
    // loop, else right after the value's definition (which dominates the
    // preheader).
    let dedicated = !unsigned && matches!(func.blocks[pre].terminator,
        crate::ir::reexports::Terminator::Branch(l) if l == func.blocks[header].label);
    let mut pre_insts: Vec<Instruction> = Vec::new();
    let mut after_def: Vec<(Value, Instruction)> = Vec::new();
    let mut extended: FxHashMap<u32, Value> = FxHashMap::default();
    let mut sext = |op: Operand, narrow: &mut FxHashMap<u32, Value>| match op {
        Operand::Const(c) => Operand::Const(IrConst::I64(ext_op(c.to_i64_exact().unwrap_or(0)))),
        Operand::Value(v) => {
            if let Some(&w) = narrow.get(&v.0).or(extended.get(&v.0)) {
                return Operand::Value(w);
            }
            let w = fresh();
            let cast = Instruction::Cast { dest: w, src: op, from_ty: narrow_ty, to_ty: IrType::I64 };
            if dedicated {
                pre_insts.push(cast);
                extended.insert(v.0, w);
            } else {
                after_def.push((v, cast));
                narrow.insert(v.0, w);
            }
            Operand::Value(w)
        }
    };
    let new_init = sext(init, narrow);

    // Widen the compares, and turn sign extensions of the counter into copies.
    for &(b, k) in &compares {
        if let Instruction::Cmp { ty, lhs, rhs, .. } = &mut func.blocks[b].instructions[k] {
            for op in [lhs, rhs] {
                if !matches!(op, Operand::Value(v) if is_iv(v.0)) {
                    *op = sext(*op, narrow);
                }
            }
            *ty = IrType::I64;
        }
    }
    let mut map: Vec<Option<Operand>> = vec![None; phi.0.max(next.0) as usize + 1];
    for (b, block) in func.blocks.iter_mut().enumerate() {
        for w in [phi, next] {
            map[w.0 as usize] = trunc.get(&(b, w.0)).map(|t| Operand::Value(*t));
        }
        for (k, inst) in block.instructions.iter_mut().enumerate() {
            match inst {
                Instruction::Phi { dest, ty, incoming } if *dest == phi => {
                    *ty = IrType::I64;
                    for (op, label) in incoming.iter_mut() {
                        if *label == pre_label {
                            *op = new_init;
                        }
                    }
                }
                Instruction::Phi { incoming, .. } => {
                    for (op, label) in incoming.iter_mut() {
                        if let Operand::Value(v) = op {
                            if is_iv(v.0) {
                                *op = Operand::Value(trunc[&(label_to_idx[label], v.0)]);
                            }
                        }
                    }
                }
                Instruction::BinOp { ty, lhs, rhs, .. } if b == next_block && k == next_idx => {
                    *ty = IrType::I64;
                    for op in [lhs, rhs] {
                        if let Some(c) = const_i64(op) {
                            *op = Operand::Const(IrConst::I64(ext_op(c)));
                        }
                    }
                }
                Instruction::Cast { dest, src: Operand::Value(v), from_ty, to_ty: IrType::I64 | IrType::U64 }
                    if *from_ty == narrow_ty && is_iv(v.0) => {
                    *inst = Instruction::Copy { dest: *dest, src: Operand::Value(*v) };
                }
                _ if compares.contains(&(b, k)) => {}
                _ => { replace_operands_in_instruction(inst, &map); }
            }
        }
        replace_operands_in_terminator(&mut block.terminator, &map);
    }

    // Place the truncations (last position first, so earlier ones stay put),
    // then the sign extensions.
    let mut placed: Vec<((usize, u32), usize)> = trunc_at.into_iter().collect();
    placed.sort_by_key(|p| std::cmp::Reverse(p.1));
    for ((b, w), at) in placed {
        let t = trunc[&(b, w)];
        narrow.insert(t.0, Value(w));
        insert_at(&mut func.blocks[b], at,
            Instruction::Cast { dest: t, src: Operand::Value(Value(w)), from_ty: IrType::I64, to_ty: narrow_ty });
    }
    for (v, cast) in after_def {
        let def = func.blocks.iter().enumerate().find_map(|(b, block)| {
            let k = block.instructions.iter().position(|i| i.dest() == Some(v))?;
            let phis = block.instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
            Some((b, (k + 1).max(phis)))
        });
        let (b, at) = def.unwrap_or((0, 0));
        insert_at(&mut func.blocks[b], at, cast);
    }
    let block = &mut func.blocks[pre];
    if let Some(&span) = block.source_spans.last() {
        block.source_spans.extend(std::iter::repeat_n(span, pre_insts.len()));
    }
    block.instructions.extend(pre_insts);
    func.next_value_id = next_id;
    true
}

/// Insert `inst` at `at` in `block`, with the span of the instruction it
/// follows (or precedes), so the spans stay one per instruction.
fn insert_at(block: &mut BasicBlock, at: usize, inst: Instruction) {
    if !block.source_spans.is_empty() {
        let span = block.source_spans[at.saturating_sub(1).min(block.source_spans.len() - 1)];
        block.source_spans.insert(at.min(block.source_spans.len()), span);
    }
    block.instructions.insert(at, inst);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::common::types::AddressSpace;
    use crate::ir::reexports::{BasicBlock, BlockId, IrCmpOp, Terminator};

    fn uses(inst: &Instruction, v: u32) -> bool {
        let mut found = false;
        inst.for_each_used_value(|u| found |= u == v);
        found
    }

    /// A scan whose counter the enclosing loop carries (`do i++; while
    /// (a[i] < 0)` repeated from where it stopped), compared after the scan
    /// with a value computed only after it.
    #[test]
    fn carried_counter_widened_and_late_compare_kept_narrow() {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(10), ty: IrType::I32, incoming: vec![(Operand::Const(IrConst::I32(-1)), BlockId(0)), (v(21), BlockId(3))] },
            ], Terminator::Branch(BlockId(2))),
            block(2, vec![
                Instruction::Phi { dest: Value(20), ty: IrType::I32, incoming: vec![(v(10), BlockId(1)), (v(21), BlockId(2))] },
                Instruction::BinOp { dest: Value(21), op: IrBinOp::Add, lhs: v(20), rhs: Operand::Const(IrConst::I32(1)), ty: IrType::I32 },
                Instruction::Cast { dest: Value(22), src: v(21), from_ty: IrType::I32, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(23), op: IrBinOp::Shl, lhs: v(22), rhs: Operand::Const(IrConst::I64(2)), ty: IrType::I64 },
                Instruction::GetElementPtr { dest: Value(24), base: Value(0), offset: v(23), ty: IrType::Ptr },
                Instruction::Load { dest: Value(25), ptr: Value(24), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cmp { dest: Value(26), op: IrCmpOp::Slt, lhs: v(25), rhs: Operand::Const(IrConst::I32(0)), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(26), true_label: BlockId(2), false_label: BlockId(3) }),
            block(3, vec![
                Instruction::BinOp { dest: Value(30), op: IrBinOp::Sub, lhs: v(1), rhs: v(25), ty: IrType::I32 },
                Instruction::Cmp { dest: Value(31), op: IrCmpOp::Slt, lhs: v(21), rhs: v(30), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(31), true_label: BlockId(1), false_label: BlockId(4) }),
            block(4, vec![], Terminator::Return(Some(v(21)))),
        ];
        func.next_value_id = 32;
        assert_eq!(widen_function(&mut func), 1);

        let b = &func.blocks;
        assert!(matches!(b[2].instructions[0], Instruction::Phi { ty: IrType::I64, .. }));
        assert!(matches!(b[2].instructions[1], Instruction::BinOp { ty: IrType::I64, .. }));
        // The scan truncates nothing per iteration.
        assert!(!b[2].instructions.iter().any(|i| matches!(i, Instruction::Cast { to_ty: IrType::I32, .. })));
        // Its start is extended once per scan, from the carried counter.
        assert!(matches!(b[1].instructions[..], [Instruction::Phi { .. }, Instruction::Cast { src: Operand::Value(Value(10)), .. }]));
        // %30 is computed after the scan: not extended before it, so the
        // compare stays narrow, reading a truncation made where the scan is left
        // (which the outer phi also reads from this block).
        assert!(!b[1].instructions.iter().any(|i| uses(i, 30)));
        let Instruction::Cast { dest: t, src: Operand::Value(Value(21)), from_ty: IrType::I64, to_ty: IrType::I32 } = b[3].instructions[1]
        else { panic!("no truncation before the late compare: {:?}", b[3].instructions) };
        assert!(matches!(b[3].instructions[2], Instruction::Cmp { ty: IrType::I32, lhs: Operand::Value(l), .. } if l == t));
        assert!(matches!(&b[1].instructions[0], Instruction::Phi { incoming, .. }
            if incoming.iter().any(|(op, l)| *l == BlockId(3) && matches!(op, Operand::Value(x) if *x == t))));
        // And the return its own, in its block.
        assert!(matches!(b[4].instructions[..], [Instruction::Cast { from_ty: IrType::I64, to_ty: IrType::I32, .. }]));
    }

    /// `do x = a[sx++]; while (sx < n)` with an unsigned sx: n loaded each
    /// iteration, sx extended for the index. `op` is the exit test's.
    fn unsigned_scan(op: IrCmpOp, init: Operand) -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(10), ty: IrType::U32, incoming: vec![(init, BlockId(0)), (v(11), BlockId(1))] },
                Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::U32 },
                Instruction::Cast { dest: Value(12), src: v(10), from_ty: IrType::U32, to_ty: IrType::I64 },
                Instruction::GetElementPtr { dest: Value(13), base: Value(0), offset: v(12), ty: IrType::Ptr },
                Instruction::Load { dest: Value(14), ptr: Value(13), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cast { dest: Value(17), src: v(10), from_ty: IrType::U32, to_ty: IrType::I64 },
                Instruction::Load { dest: Value(15), ptr: Value(1), ty: IrType::U32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cmp { dest: Value(16), op, lhs: v(11), rhs: v(15), ty: IrType::U32 },
            ], Terminator::CondBranch { cond: v(16), true_label: BlockId(1), false_label: BlockId(2) }),
            block(2, vec![], Terminator::Return(Some(v(17)))),
        ];
        func.next_value_id = 20;
        func
    }

    #[test]
    fn unsigned_counter_bounded_by_its_test_widened() {
        let mut func = unsigned_scan(IrCmpOp::Ult, Operand::Const(IrConst::I64(0)));
        assert_eq!(widen_function(&mut func), 1);
        let b = &func.blocks[1].instructions;
        assert!(matches!(b[0], Instruction::Phi { ty: IrType::I64, .. }));
        assert!(matches!(b[1], Instruction::BinOp { ty: IrType::I64, .. }));
        assert!(matches!(b[2], Instruction::Copy { dest: Value(12), src: Operand::Value(Value(10)) }));
        // The bound is zero-extended where it is loaded, and compared at 64 bits.
        assert!(b.iter().any(|i| matches!(i, Instruction::Cast { src: Operand::Value(Value(15)), from_ty: IrType::U32, to_ty: IrType::I64, .. })));
        assert!(b.iter().any(|i| matches!(i, Instruction::Cmp { dest: Value(16), ty: IrType::I64, .. })));
        // `sx <= n` lets it reach 2^32 - 1 and wrap; a start that is not
        // constant may be 2^32 - 1.
        for (op, init) in [(IrCmpOp::Ule, Operand::Const(IrConst::I64(0))), (IrCmpOp::Ult, Operand::Value(Value(2)))] {
            let mut func = unsigned_scan(op, init);
            assert_eq!(widen_function(&mut func), 0);
        }
    }
}

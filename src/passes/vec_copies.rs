//! Copies between local vectors removed.
//!
//! A vector lives in an alloca, and an inlined NEON intrinsic gives its
//! parameters and its result one each, joined by Memcpys:
//!
//!   memcpy %param, %a, 16
//!   %ret = vec add %param, %b          %a = vec add %a, %b
//!   memcpy %a, %ret, 16
//!
//! The backend keeps a whole alloca in a register for the whole function from
//! a small pool, so a loop of a few intrinsics, with dozens of these, ran out
//! of memory at a sixth of clang's speed. Two rules leave only the variables
//! the source has:
//!
//! - Forward: `memcpy d, s` where that is d's only write, every read of d
//!   follows it in the block, and s is not written before the last of them:
//!   those reads read s.
//! - Backward: `memcpy d, t` where t's only write is earlier in the block, this
//!   is t's only read, and d is untouched between: that write writes d.
//!
//! Only allocas whose every use is one of these instructions take part, so
//! nothing else can see them. Runs after the profile point, on non-SSA IR.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::ir::reexports::{Instruction, IntrinsicOp, IrFunction, IrModule, Operand, Value};

pub(crate) fn remove_module(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        remove_function(func);
    }
}

#[derive(Default)]
struct Uses {
    reads: Vec<(usize, usize)>,
    writes: Vec<(usize, usize)>,
}

struct Locals {
    size: FxHashMap<u32, usize>,
    uses: FxHashMap<u32, Uses>,
    /// Constant GEPs from a local, to it.
    root: FxHashMap<u32, u32>,
    bad: FxHashSet<u32>,
}

fn survey(func: &IrFunction) -> Locals {
    let insts = || func.blocks.iter().flat_map(|b| &b.instructions);
    let size: FxHashMap<u32, usize> = insts()
        .filter_map(|inst| match inst {
            Instruction::Alloca { dest, size: size @ (8 | 16), volatile: false, .. } => Some((dest.0, *size)),
            _ => None,
        })
        .collect();
    let mut root: FxHashMap<u32, u32> = size.keys().map(|&id| (id, id)).collect();
    loop {
        let mut changed = false;
        for inst in insts() {
            if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(_), .. } = inst {
                if let (Some(&r), false) = (root.get(&base.0), root.contains_key(&dest.0)) {
                    root.insert(dest.0, r);
                    changed = true;
                }
            }
        }
        if !changed {
            break;
        }
    }
    let mut locals = Locals { size, uses: FxHashMap::default(), root, bad: FxHashSet::default() };
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            let root = &locals.root;
            let at = (b, i);
            let (mut reads, mut writes): (Vec<u32>, Vec<u32>) = (Vec::new(), Vec::new());
            let mut others: Vec<u32> = Vec::new();
            match inst {
                Instruction::Alloca { .. } => {}
                Instruction::GetElementPtr { base, offset: Operand::Const(_), .. } if root.contains_key(&base.0) => {}
                Instruction::Memcpy { dest, src, .. } => {
                    writes.push(dest.0);
                    reads.push(src.0);
                }
                Instruction::Load { ptr, volatile: false, .. } => reads.push(ptr.0),
                Instruction::Store { ptr, val, volatile: false, .. } => {
                    writes.push(ptr.0);
                    if let Operand::Value(v) = val {
                        others.push(v.0);
                    }
                }
                Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr, args, .. } => {
                    for arg in args {
                        if let Operand::Value(v) = arg {
                            reads.push(v.0);
                        }
                    }
                    writes.extend(dest_ptr.map(|d| d.0));
                }
                _ => inst.for_each_used_value(|v| others.push(v)),
            }
            let (reads, writes): (Vec<u32>, Vec<u32>) = (
                reads.iter().filter_map(|v| root.get(v).copied()).collect(),
                writes.iter().filter_map(|v| root.get(v).copied()).collect(),
            );
            let others: Vec<u32> = others.iter().filter_map(|v| root.get(v).copied()).collect();
            for r in reads {
                locals.uses.entry(r).or_default().reads.push(at);
            }
            for w in writes {
                locals.uses.entry(w).or_default().writes.push(at);
            }
            locals.bad.extend(others);
        }
        let root = &locals.root;
        let mut escaped = Vec::new();
        block.terminator.for_each_used_value(|v| escaped.extend(root.get(&v).copied()));
        locals.bad.extend(escaped);
    }
    locals
}

/// A local with nothing unknown touching it, named directly, copied whole.
fn whole(locals: &Locals, v: Value, n: usize) -> bool {
    locals.size.get(&v.0) == Some(&n) && !locals.bad.contains(&v.0)
}

fn replace_reads(inst: &mut Instruction, from: Value, to: Value) {
    match inst {
        Instruction::Memcpy { src, .. } if *src == from => *src = to,
        Instruction::Load { ptr, .. } if *ptr == from => *ptr = to,
        Instruction::GetElementPtr { base, .. } if *base == from => *base = to,
        Instruction::Intrinsic { args, .. } => {
            for arg in args {
                if matches!(arg, Operand::Value(v) if *v == from) {
                    *arg = Operand::Value(to);
                }
            }
        }
        _ => {}
    }
}

fn remove_at(func: &mut IrFunction, b: usize, i: usize) {
    let block = &mut func.blocks[b];
    block.instructions.remove(i);
    if i < block.source_spans.len() {
        block.source_spans.remove(i);
    }
}

/// d's reads, if they all follow `i` in block `b` and nothing writes s until the last.
fn forward(locals: &Locals, b: usize, i: usize, d: Value, s: Value) -> bool {
    let none = Uses::default();
    let du = locals.uses.get(&d.0).unwrap_or(&none);
    let su = locals.uses.get(&s.0).unwrap_or(&none);
    if du.writes != [(b, i)] || du.reads.iter().any(|&(rb, ri)| rb != b || ri <= i) {
        return false;
    }
    let last = du.reads.iter().map(|&(_, ri)| ri).max().unwrap_or(i);
    !su.writes.iter().any(|&(wb, wi)| wb == b && wi > i && wi < last)
}

/// The instruction that alone writes t, if this copy alone reads it and d is left alone between.
fn backward(locals: &Locals, b: usize, i: usize, d: Value, t: Value) -> Option<usize> {
    let none = Uses::default();
    let tu = locals.uses.get(&t.0)?;
    let du = locals.uses.get(&d.0).unwrap_or(&none);
    if tu.reads != [(b, i)] || tu.writes.len() != 1 {
        return None;
    }
    let (wb, p) = tu.writes[0];
    if wb != b || p >= i {
        return None;
    }
    let between = |&(ub, ui): &(usize, usize)| ub == b && ui > p && ui < i;
    (!du.reads.iter().any(between) && !du.writes.iter().any(between)).then_some(p)
}

fn remove_function(func: &mut IrFunction) -> usize {
    if !func.blocks.iter().flat_map(|b| &b.instructions).any(|i| matches!(i, Instruction::Memcpy { .. })) {
        return 0;
    }
    let mut removed = 0;
    let mut emptied: FxHashSet<u32> = FxHashSet::default();
    'again: loop {
        let locals = survey(func);
        for b in 0..func.blocks.len() {
            for i in 0..func.blocks[b].instructions.len() {
                let Instruction::Memcpy { dest, src, size } = func.blocks[b].instructions[i] else { continue };
                if dest == src || !whole(&locals, dest, size) || !whole(&locals, src, size) {
                    continue;
                }
                if forward(&locals, b, i, dest, src) {
                    for inst in &mut func.blocks[b].instructions[i + 1..] {
                        replace_reads(inst, dest, src);
                    }
                    // A lane's address may have been hoisted above the copy, even
                    // out of the block; only what it is used for has to follow.
                    for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
                        if matches!(inst, Instruction::GetElementPtr { .. }) {
                            replace_reads(inst, dest, src);
                        }
                    }
                    remove_at(func, b, i);
                    emptied.insert(dest.0);
                    removed += 1;
                    continue 'again;
                }
                if let Some(p) = backward(&locals, b, i, dest, src) {
                    let retargeted = match &mut func.blocks[b].instructions[p] {
                        Instruction::Memcpy { dest: w, size: n, .. } if *w == src && *n == size => {
                            *w = dest;
                            true
                        }
                        Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr: Some(w), .. } if *w == src => {
                            *w = dest;
                            true
                        }
                        _ => false,
                    };
                    if retargeted {
                        remove_at(func, b, i);
                        emptied.insert(src.0);
                        removed += 1;
                        continue 'again;
                    }
                }
            }
        }
        break;
    }
    if removed > 0 {
        // The allocas this left unnamed. Not a parameter's: the backend pairs
        // those with parameters by position.
        for param in &func.param_alloca_values {
            emptied.remove(&param.0);
        }
        let mut used: FxHashSet<u32> = FxHashSet::default();
        for block in &func.blocks {
            for inst in &block.instructions {
                inst.for_each_used_value(|v| {
                    used.insert(v);
                });
            }
            block.terminator.for_each_used_value(|v| {
                used.insert(v);
            });
        }
        for block in &mut func.blocks {
            let mut i = 0;
            while i < block.instructions.len() {
                let gone = matches!(&block.instructions[i], Instruction::Alloca { dest, .. } if emptied.contains(&dest.0) && !used.contains(&dest.0));
                if gone {
                    block.instructions.remove(i);
                    if i < block.source_spans.len() {
                        block.source_spans.remove(i);
                    }
                } else {
                    i += 1;
                }
            }
        }
    }
    removed
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::IrType;
    use crate::ir::reexports::{BasicBlock, BlockId, Terminator};
    use crate::ir::intrinsics::{VecElem, VecOpKind};

    fn alloca(v: u32) -> Instruction {
        Instruction::Alloca { dest: Value(v), ty: IrType::Ptr, size: 16, align: 16, volatile: false }
    }

    fn memcpy(d: u32, s: u32) -> Instruction {
        Instruction::Memcpy { dest: Value(d), src: Value(s), size: 16 }
    }

    fn add(d: u32, a: u32, b: u32) -> Instruction {
        Instruction::Intrinsic {
            dest: None,
            op: IntrinsicOp::VecOp { op: VecOpKind::Add, elem: VecElem::U32, bytes: 16, splat: 0 },
            dest_ptr: Some(Value(d)),
            args: vec![Operand::Value(Value(a)), Operand::Value(Value(b))],
        }
    }

    fn run(instructions: Vec<Instruction>) -> (usize, Vec<Instruction>) {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        f.blocks.push(BasicBlock { label: BlockId(0), instructions, terminator: Terminator::Return(None), source_spans: vec![] });
        let n = remove_function(&mut f);
        (n, f.blocks.remove(0).instructions)
    }

    fn copies(insts: &[Instruction]) -> usize {
        insts.iter().filter(|i| matches!(i, Instruction::Memcpy { .. })).count()
    }

    #[test]
    fn test_a_parameter_and_a_result_go() {
        // 1 and 2 are variables, 3 a parameter, 4 a result.
        let (n, out) = run(vec![alloca(1), alloca(2), alloca(3), alloca(4), memcpy(3, 1), add(4, 3, 2), memcpy(1, 4)]);
        assert_eq!(n, 2);
        assert_eq!(copies(&out), 0);
        assert!(matches!(out.last(), Some(Instruction::Intrinsic { dest_ptr: Some(Value(1)), args, .. })
            if matches!(args[0], Operand::Value(Value(1)))));
    }

    #[test]
    fn test_a_lane_address_made_before_the_copy_follows_it() {
        // %5 = gep %3, 4 sits above memcpy %3, %1; the load after it must read %1.
        let gep = Instruction::GetElementPtr { dest: Value(5), base: Value(3), offset: Operand::Const(crate::ir::reexports::IrConst::I64(4)), ty: IrType::I8 };
        let load = Instruction::Load { dest: Value(6), ptr: Value(5), ty: IrType::I32, seg_override: Default::default(), volatile: false,
            alias: crate::common::types::AliasClass::ANY };
        let (n, out) = run(vec![alloca(1), alloca(3), gep, memcpy(3, 1), load]);
        assert_eq!(n, 1);
        assert!(out.iter().any(|i| matches!(i, Instruction::GetElementPtr { base: Value(1), .. })));
    }

    #[test]
    fn test_unused_allocas_it_did_not_empty_stay() {
        // 7 was unused already: a parameter's, perhaps, paired by position.
        let (n, out) = run(vec![alloca(7), alloca(1), alloca(2), alloca(3), memcpy(3, 1), add(2, 3, 3)]);
        assert_eq!(n, 1);
        assert!(out.iter().any(|i| matches!(i, Instruction::Alloca { dest: Value(7), .. })));
        assert!(!out.iter().any(|i| matches!(i, Instruction::Alloca { dest: Value(3), .. })));
    }

    #[test]
    fn test_a_source_written_before_the_last_read_keeps_the_copy() {
        // 3 keeps the old 1, which is overwritten before 3 is read.
        let (n, out) = run(vec![alloca(1), alloca(2), alloca(3), memcpy(3, 1), add(1, 2, 2), add(2, 3, 1)]);
        assert_eq!(n, 0);
        assert_eq!(copies(&out), 1);
    }

    #[test]
    fn test_a_destination_read_between_keeps_the_copy() {
        // 1 is read after 4 is made, before the copy back.
        let (n, _) = run(vec![alloca(1), alloca(2), alloca(4), add(4, 2, 2), add(2, 1, 1), memcpy(1, 4)]);
        assert_eq!(n, 0);
    }

    #[test]
    fn test_a_local_whose_address_escapes_is_left_alone() {
        let esc = Instruction::Store { val: Operand::Value(Value(3)), ptr: Value(9), ty: IrType::Ptr,
            seg_override: Default::default(), volatile: false, alias: crate::common::types::AliasClass::ANY };
        let (n, out) = run(vec![alloca(1), alloca(3), memcpy(3, 1), esc, add(1, 3, 3)]);
        assert_eq!(n, 0);
        assert_eq!(copies(&out), 1);
    }

    #[test]
    fn test_a_value_written_twice_keeps_its_copy() {
        // 3 is also written by the add, so its reads see two values.
        let (n, _) = run(vec![alloca(1), alloca(2), alloca(3), memcpy(3, 1), add(2, 3, 3), add(3, 2, 2), add(2, 3, 3)]);
        assert_eq!(n, 0);
    }
}

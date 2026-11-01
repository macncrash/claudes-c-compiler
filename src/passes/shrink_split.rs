//! Values split where a function's slow path starts, so its fast path can
//! run without a frame (AArch64 shrink-wrapping).
//!
//! sqlite3BtreeNext returns 0 or tail-calls btreeNext on most calls and calls
//! moveToChild in a loop on the rest. Its cursor is live across that call, so
//! the register allocator gives it a callee-saved register for the whole
//! function, and the fast path, reading the cursor there, needs the prologue
//! that saves it. Here the blocks reachable from a block with a call that is
//! not a tail call (the slow region, closed under successors) get their own
//! copy of every value they share with the rest (the fast region), made on
//! the edge into the slow region's one entry block:
//!
//!   entry: ... cbz w0, slow          entry: ... cbz w0, split
//!   slow:  ... bl moveToChild  =>    split: v' = copy v; b slow
//!                                    slow:  ... (v' for v) bl moveToChild
//!
//! With the fast blocks laid out first, then the split block, then the slow
//! ones, a value's live interval in the fast region spans no call and takes
//! a caller-saved register; its copy, live across the calls, a callee-saved
//! one. The peephole's shrink_wrap then moves the prologue to the split
//! block when the fast blocks need no frame (no stack, no callee-saved
//! register) and drops their epilogues.
//!
//! Runs last, after phi elimination (values may have several definitions:
//! every occurrence in the slow region is renamed) and const_hoist (whose
//! hoisted constants the slow region gets copies of too). Functions are left
//! alone when the slow region has several entries, when the fast region
//! never returns, or when anything there needs the stack (an alloca's
//! address, 128-bit arithmetic, intrinsics that call out, inline asm).

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::ir::reexports::{BasicBlock, BlockId, Instruction, IrConst, IrFunction, IrModule, Operand, Terminator, Value};
use super::inline::{remap_instruction, remap_terminator, Remap};

/// With a profile, the least share of calls (in tenths) that must leave by
/// the fast region's returns.
const MIN_FAST_TENTHS: u64 = 3;

/// Most instructions in a chain of blocks copied for the fast region.
const MAX_CLONED_EXIT: usize = 4;

/// Most instructions (and terminators) in the fast region.
const MAX_FAST: usize = 64;

/// Callees that return twice: their caller's frame must exist from the start.
const RETURNS_TWICE: &[&str] = &["setjmp", "_setjmp", "sigsetjmp", "__sigsetjmp", "savectx", "vfork", "getcontext"];

pub(crate) fn split_module(module: &mut IrModule) -> usize {
    // Block labels are unique across the module.
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    module.functions.iter_mut().filter(|f| !f.is_declaration).map(|f| split_function(f, &mut next_label)).sum()
}

/// The call a block ends with, if the block returns its result (through
/// casts and copies, `call; cast I64->I32; ret`) or is a void call and a
/// plain return, there or in an empty block it jumps to: a call the backend
/// can make a tail call, which needs no frame.
fn ends_in_tail_call(block: &BasicBlock, blocks: &[BasicBlock], index: &FxHashMap<BlockId, usize>) -> Option<usize> {
    let k = block.instructions.iter().rposition(|i| !matches!(i, Instruction::Cast { .. } | Instruction::Copy { .. }))?;
    let (Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. }) = &block.instructions[k] else { return None };
    // The value the call's result becomes along the casts and copies.
    let mut result = info.dest;
    for inst in &block.instructions[k + 1..] {
        match inst {
            Instruction::Cast { dest, src: Operand::Value(v), .. } | Instruction::Copy { dest, src: Operand::Value(v) }
                if Some(*v) == result => result = Some(*dest),
            _ => return None,
        }
    }
    let term = match &block.terminator {
        Terminator::Branch(t) if blocks[index[t]].instructions.is_empty() => &blocks[index[t]].terminator,
        t => t,
    };
    let returns_it = match (term, result) {
        (Terminator::Return(None), _) => k + 1 == block.instructions.len(),
        (Terminator::Return(Some(Operand::Value(v))), Some(d)) => *v == d,
        _ => false,
    };
    let plain = !info.is_sret && !info.is_variadic && info.args.len() <= 8
        && info.struct_arg_sizes.iter().all(|s| s.is_none());
    (returns_it && plain).then_some(k)
}

pub(crate) fn split_function(func: &mut IrFunction, next_label: &mut u32) -> usize {
    let nb = func.blocks.len();
    if nb < 2 || func.is_variadic {
        return 0;
    }
    let allocas: FxHashSet<u32> = func.blocks.iter().flat_map(|b| &b.instructions).filter_map(|i| match i {
        Instruction::Alloca { dest, .. } => Some(dest.0),
        _ => None,
    }).collect();
    let index: FxHashMap<BlockId, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    // The backend makes no tail call where a local's address may outlive the
    // frame (convert_tail_calls' frame_stays_private): passed to a call,
    // stored, or returned (moveToChild hands its callee `&pNew`).
    let mut local_ptrs = allocas.clone();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        match inst {
            Instruction::GetElementPtr { dest, base, .. } if local_ptrs.contains(&base.0) => { local_ptrs.insert(dest.0); }
            Instruction::Copy { dest, src: Operand::Value(v) } | Instruction::Cast { dest, src: Operand::Value(v), .. }
                if local_ptrs.contains(&v.0) => { local_ptrs.insert(dest.0); }
            _ => {}
        }
    }
    let escapes = func.blocks.iter().any(|b| {
        matches!(&b.terminator, Terminator::Return(Some(Operand::Value(v))) if local_ptrs.contains(&v.0))
            || b.instructions.iter().any(|inst| match inst {
                Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. } =>
                    info.args.iter().any(|a| matches!(a, Operand::Value(v) if local_ptrs.contains(&v.0))),
                Instruction::Store { val: Operand::Value(v), .. } => local_ptrs.contains(&v.0),
                _ => false,
            })
    });
    // Blocks with something that needs the frame (a call that is not a tail
    // call, ...); give up on what has to have it from the start.
    let mut frame = vec![false; nb];
    for (b, block) in func.blocks.iter().enumerate() {
        if matches!(block.terminator, Terminator::IndirectBranch { .. }) {
            return 0;
        }
        let tail = if escapes { None } else { ends_in_tail_call(block, &func.blocks, &index) };
        for (k, inst) in block.instructions.iter().enumerate() {
            let needs = match inst {
                Instruction::Call { func: name, .. } if RETURNS_TWICE.contains(&name.as_str()) => return 0,
                Instruction::InlineAsm { .. } | Instruction::DynAlloca { .. } | Instruction::StackSave { .. }
                | Instruction::StackRestore { .. } | Instruction::VaStart { .. } => return 0,
                Instruction::Alloca { .. } => false,
                Instruction::Call { .. } | Instruction::CallIndirect { .. } => tail != Some(k),
                Instruction::Intrinsic { op, .. } => !op.is_pure(),
                _ => {
                    let mut uses_alloca = false;
                    inst.for_each_used_value(|v| uses_alloca |= allocas.contains(&v));
                    uses_alloca || inst.result_type().is_some_and(|t| t.is_128bit() || t.is_long_double())
                }
            };
            frame[b] |= needs;
        }
        let mut uses_alloca = false;
        block.terminator.for_each_used_value(|v| uses_alloca |= allocas.contains(&v));
        frame[b] |= uses_alloca;
    }
    let mut succs: Vec<Vec<usize>> = func.blocks.iter().map(|b| match &b.terminator {
        Terminator::Branch(t) => vec![index[t]],
        Terminator::CondBranch { true_label, false_label, .. } => vec![index[true_label], index[false_label]],
        Terminator::Switch { cases, default, .. } => cases.iter().map(|(_, t)| index[t]).chain([index[default]]).collect(),
        _ => Vec::new(),
    }).collect();
    // The slow region: what a block needing the frame reaches.
    let mut slow = frame.clone();
    let mut work: Vec<usize> = (0..nb).filter(|&b| slow[b]).collect();
    while let Some(b) = work.pop() {
        for &s in &succs[b] {
            if !slow[s] {
                slow[s] = true;
                work.push(s);
            }
        }
    }
    // A short chain of blocks that only returns (jumps through blocks of a
    // few copies to a ret), entered from both regions (where
    // vdbeSorterCompareInt's fast compares and its call to
    // vdbeSorterCompareTail meet), is copied for the fast region, which
    // then does not enter the slow region there. Only with a profile: an
    // exit shared with a slow path is often taken mostly from it (an error
    // code returned once a call fails, insertCellFast's).
    let chain = |b: usize| -> Option<Vec<usize>> {
        let mut blocks = vec![b];
        let mut insts = func.blocks[b].instructions.len();
        let mut cur = b;
        loop {
            match &func.blocks[cur].terminator {
                Terminator::Return(_) => return (insts <= MAX_CLONED_EXIT).then_some(blocks),
                Terminator::Branch(_) if blocks.len() < 4 => {
                    cur = succs[cur][0];
                    insts += func.blocks[cur].instructions.len();
                    blocks.push(cur);
                }
                _ => return None,
            }
        }
    };
    let exits: Vec<Vec<usize>> = (0..nb).filter(|_| func.profile.is_some()).filter(|&b| slow[b] && !frame[b] && (0..nb).any(|p| !slow[p] && succs[p].contains(&b)))
        .filter_map(chain).filter(|c| c.iter().all(|&b| !frame[b])).collect();
    for c in exits {
        let base = func.blocks.len();
        let labels: Vec<BlockId> = c.iter().map(|_| {
            *next_label += 1;
            BlockId(*next_label - 1)
        }).collect();
        for (i, &b) in c.iter().enumerate() {
            let mut copy = func.blocks[b].clone();
            copy.label = labels[i];
            if let Some(&next) = c.get(i + 1) {
                retarget(&mut copy.terminator, func.blocks[next].label, labels[i + 1]);
            }
            func.blocks.push(copy);
            succs.push(if i + 1 < c.len() { vec![base + i + 1] } else { Vec::new() });
            slow.push(false);
        }
        let from = func.blocks[c[0]].label;
        let preds: Vec<usize> = (0..base).filter(|&p| !slow[p] && succs[p].contains(&c[0])).collect();
        for p in preds {
            retarget(&mut func.blocks[p].terminator, from, labels[0]);
            for x in succs[p].iter_mut().filter(|x| **x == c[0]) {
                *x = base;
            }
        }
    }
    let nb = func.blocks.len();
    // A block that can only go on into the slow region (sqlite3BtreeNext's
    // setup of its moveToChild loop, branching to the loop or past it) is
    // slow too: its fast path never returns, and with it in the slow region
    // that region has one entry.
    loop {
        let more: Vec<usize> = (1..nb).filter(|&b| !slow[b] && !succs[b].is_empty() && succs[b].iter().all(|&s| slow[s])).collect();
        if more.is_empty() {
            break;
        }
        for b in more {
            slow[b] = true;
        }
    }
    if slow[0] || !slow.iter().any(|&s| s) {
        return 0;
    }
    if !(0..nb).any(|b| !slow[b] && matches!(func.blocks[b].terminator, Terminator::Return(_))) {
        return 0;
    }
    let entries: FxHashSet<usize> = (0..nb).filter(|&b| !slow[b]).flat_map(|b| succs[b].iter().copied()).filter(|&s| slow[s]).collect();
    if entries.len() != 1 {
        return 0;
    }
    let entry = *entries.iter().next().unwrap();
    // With a profile, only where the fast returns are taken often enough
    // (an exit taken on a few calls in a hundred saves its prologue there,
    // and costs the slow path's copies on all the others).
    if let Some(profile) = &func.profile {
        let count = |b: usize| profile.get(&func.blocks[b].label.0).copied().unwrap_or(0);
        // (The calls that do not enter the slow region by its one entry: the
        // copied exits have no counts of their own.)
        let calls = count(0);
        let fast = calls.saturating_sub(count(entry));
        if calls == 0 || fast.saturating_mul(10) < calls.saturating_mul(MIN_FAST_TENTHS) {
            return 0;
        }
    }
    // A long fast region is mostly checks on the way in (the header parsing
    // of jsonTranslateBlobToText), whose returns are its error paths; a
    // profile says how often they are taken instead.
    let fast_size: usize = (0..nb).filter(|&b| !slow[b]).map(|b| func.blocks[b].instructions.len() + 1).sum();
    if fast_size > MAX_FAST && func.profile.is_none() {
        return 0;
    }
    // Values occurring in both regions (allocas stay: they name frame slots).
    let occurs = |region: bool| {
        let mut set: FxHashSet<u32> = FxHashSet::default();
        for b in (0..nb).filter(|&b| slow[b] == region) {
            for inst in &func.blocks[b].instructions {
                if let Some(d) = inst.dest() {
                    set.insert(d.0);
                }
                inst.for_each_used_value(|v| { set.insert(v); });
            }
            func.blocks[b].terminator.for_each_used_value(|v| { set.insert(v); });
        }
        set
    };
    let (in_fast, in_slow) = (occurs(false), occurs(true));
    let mut shared: Vec<u32> = in_fast.intersection(&in_slow).copied().filter(|v| !allocas.contains(v)).collect();
    shared.sort_unstable();
    if shared.is_empty() {
        return 0;
    }
    let live_in = live_into(func, &succs, entry, &shared);
    let mut next = func.next_value_id.max(func.max_value_id() + 1);
    let rename: FxHashMap<u32, Value> = shared.iter().map(|&v| {
        next += 1;
        (v, Value(next - 1))
    }).collect();
    func.next_value_id = next;
    let remap = Remap { value: &|v| rename.get(&v.0).copied().unwrap_or(v), block: &|b| b };
    for b in (0..nb).filter(|&b| slow[b]) {
        let block = &mut func.blocks[b];
        block.instructions = block.instructions.iter().map(|i| remap_instruction(i, &remap)).collect();
        block.terminator = remap_terminator(&block.terminator, &remap);
    }
    // The split block on the edges into the slow region.
    let target = func.blocks[entry].label;
    let label = BlockId(*next_label);
    *next_label += 1;
    // A constant (const_hoist's, defined once) is made again rather than
    // copied, so what reads it still sees the constant (the multiply-high
    // of sqlite3Int64ToText's division by 100).
    let mut defs: FxHashMap<u32, (usize, Option<IrConst>)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Some(d) = inst.dest() {
            let e = defs.entry(d.0).or_insert((0, None));
            e.0 += 1;
            e.1 = match inst {
                Instruction::Copy { src: Operand::Const(c), .. } => Some(*c),
                _ => None,
            };
        }
    }
    let copies: Vec<Instruction> = shared.iter().filter(|v| live_in.contains(v)).map(|v| match defs.get(v) {
        Some(&(1, Some(c))) => Instruction::Copy { dest: rename[v], src: Operand::Const(c) },
        _ => Instruction::Copy { dest: rename[v], src: Operand::Value(Value(*v)) },
    }).collect();
    func.split_copies.extend(copies.iter().filter(|c| matches!(c, Instruction::Copy { src: Operand::Value(_), .. }))
        .filter_map(|c| c.dest()));
    for b in (0..nb).filter(|&b| !slow[b]) {
        retarget(&mut func.blocks[b].terminator, target, label);
    }
    let split = BasicBlock { label, instructions: copies, terminator: Terminator::Branch(target), source_spans: Vec::new() };
    // Fast blocks first, then the split block, then the slow ones.
    let blocks = std::mem::take(&mut func.blocks);
    let (fast, rest): (Vec<_>, Vec<_>) = blocks.into_iter().zip(slow).partition(|(_, s)| !s);
    func.blocks = fast.into_iter().map(|(b, _)| b).chain([split]).chain(rest.into_iter().map(|(b, _)| b)).collect();
    1
}

/// The values of `vals` live on entry to block `entry`.
fn live_into(func: &IrFunction, succs: &[Vec<usize>], entry: usize, vals: &[u32]) -> FxHashSet<u32> {
    let id: FxHashMap<u32, usize> = vals.iter().enumerate().map(|(i, &v)| (v, i)).collect();
    let nb = func.blocks.len();
    // Per block: values read before written (gen) and written (kill).
    let mut gen = vec![vec![false; vals.len()]; nb];
    let mut kill = vec![vec![false; vals.len()]; nb];
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| {
                if let Some(&i) = id.get(&v) {
                    if !kill[b][i] {
                        gen[b][i] = true;
                    }
                }
            });
            if let Some(d) = inst.dest() {
                if let Some(&i) = id.get(&d.0) {
                    kill[b][i] = true;
                }
            }
        }
        block.terminator.for_each_used_value(|v| {
            if let Some(&i) = id.get(&v) {
                if !kill[b][i] {
                    gen[b][i] = true;
                }
            }
        });
    }
    let mut live = gen.clone();
    loop {
        let mut changed = false;
        for b in (0..nb).rev() {
            for &s in &succs[b] {
                for i in 0..vals.len() {
                    if live[s][i] && !kill[b][i] && !live[b][i] {
                        live[b][i] = true;
                        changed = true;
                    }
                }
            }
        }
        if !changed {
            break;
        }
    }
    vals.iter().enumerate().filter(|&(i, _)| live[entry][i]).map(|(_, &v)| v).collect()
}

/// Point every edge of a terminator to `from` at `to` instead.
fn retarget(term: &mut Terminator, from: BlockId, to: BlockId) {
    match term {
        Terminator::Branch(t) if *t == from => *t = to,
        Terminator::CondBranch { true_label, false_label, .. } => {
            for t in [true_label, false_label] {
                if *t == from {
                    *t = to;
                }
            }
        }
        Terminator::Switch { cases, default, .. } => {
            for (_, t) in cases.iter_mut() {
                if *t == from {
                    *t = to;
                }
            }
            if *default == from {
                *default = to;
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::IrType;
    use crate::ir::reexports::{CallInfo, IrCmpOp};

    fn call(func: &str, dest: Option<u32>, args: Vec<Operand>) -> Instruction {
        Instruction::Call { func: func.to_string(), info: CallInfo {
            dest: dest.map(Value), arg_types: vec![IrType::I64; args.len()], num_fixed_args: args.len(),
            struct_arg_sizes: vec![None; args.len()], args, return_type: IrType::I64, is_variadic: false,
            struct_arg_aligns: vec![], struct_arg_classes: Vec::new(), struct_arg_riscv_float_classes: Vec::new(),
            is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
        } }
    }

    fn block(label: u32, instructions: Vec<Instruction>, terminator: Terminator) -> BasicBlock {
        BasicBlock { label: BlockId(label), instructions, terminator, source_spans: Vec::new() }
    }

    /// f(p): if (p == 0) return 7; if (p == 1) return tail(p); return g() + p.
    fn func() -> IrFunction {
        let mut f = IrFunction::new("f".to_string(), IrType::I64, vec![], false);
        let v = |n: u32| Operand::Value(Value(n));
        let cmp = |dest: u32, k: i64| Instruction::Cmp { dest: Value(dest), op: IrCmpOp::Eq, lhs: v(1), rhs: Operand::Const(IrConst::I64(k)), ty: IrType::I64 };
        f.blocks = vec![
            block(0, vec![Instruction::ParamRef { dest: Value(1), param_idx: 0, ty: IrType::I64 }, cmp(2, 0)],
                  Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(2) }),
            block(1, vec![], Terminator::Return(Some(Operand::Const(IrConst::I64(7))))),
            block(3, vec![call("g", Some(4), vec![]),
                          Instruction::BinOp { dest: Value(5), op: crate::ir::reexports::IrBinOp::Add, lhs: v(4), rhs: v(1), ty: IrType::I64 }],
                  Terminator::Return(Some(v(5)))),
            block(2, vec![cmp(6, 1)], Terminator::CondBranch { cond: v(6), true_label: BlockId(4), false_label: BlockId(3) }),
            block(4, vec![call("tail", Some(7), vec![v(1)])], Terminator::Return(Some(v(7)))),
        ];
        f.next_value_id = 8;
        f
    }

    #[test]
    fn slow_path_gets_its_own_copy_of_shared_values() {
        let mut f = func();
        let mut next_label = 10;
        assert_eq!(split_function(&mut f, &mut next_label), 1);
        let labels: Vec<u32> = f.blocks.iter().map(|b| b.label.0).collect();
        // Fast blocks (the tail call's included) first, then the split block, then the slow one.
        assert_eq!(labels, [0, 1, 2, 4, 10, 3]);
        let split = &f.blocks[4];
        let [Instruction::Copy { dest, src: Operand::Value(Value(1)) }] = split.instructions[..] else { panic!("{:?}", split.instructions) };
        assert_eq!(f.split_copies, [dest]);
        assert!(matches!(f.blocks[2].terminator, Terminator::CondBranch { false_label: BlockId(10), .. }));
        let Instruction::BinOp { rhs: Operand::Value(r), .. } = f.blocks[5].instructions[1] else { panic!() };
        assert_eq!(r, dest);
        // The tail call's block still reads the original.
        let Instruction::Call { info, .. } = &f.blocks[3].instructions[0] else { panic!() };
        assert!(matches!(info.args[0], Operand::Value(Value(1))));
    }

    /// f(p): r = p == 0 ? 7 : g() + p; return r -- both paths end at one
    /// return. With a profile whose calls mostly return 7 the fast path gets
    /// its own copy of that return; without one the function is left alone.
    #[test]
    fn shared_return_is_copied_for_the_fast_path_with_a_profile() {
        let v = |n: u32| Operand::Value(Value(n));
        let make = || {
            let mut f = IrFunction::new("f".to_string(), IrType::I64, vec![], false);
            f.blocks = vec![
                block(0, vec![Instruction::ParamRef { dest: Value(1), param_idx: 0, ty: IrType::I64 },
                              Instruction::Cmp { dest: Value(2), op: IrCmpOp::Eq, lhs: v(1), rhs: Operand::Const(IrConst::I64(0)), ty: IrType::I64 }],
                      Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(2) }),
                block(1, vec![Instruction::Copy { dest: Value(6), src: Operand::Const(IrConst::I64(7)) }], Terminator::Branch(BlockId(3))),
                block(2, vec![call("g", Some(4), vec![]),
                              Instruction::BinOp { dest: Value(5), op: crate::ir::reexports::IrBinOp::Add, lhs: v(4), rhs: v(1), ty: IrType::I64 },
                              Instruction::Copy { dest: Value(6), src: v(5) }],
                      Terminator::Branch(BlockId(3))),
                block(3, vec![], Terminator::Return(Some(v(6)))),
            ];
            f.next_value_id = 7;
            f
        };
        let mut next_label = 10;
        let mut f = make();
        assert_eq!(split_function(&mut f, &mut next_label), 0);
        let mut f = make();
        f.profile = Some([(0, 100), (1, 90), (2, 10), (3, 100)].into_iter().collect());
        assert_eq!(split_function(&mut f, &mut next_label), 1);
        let labels: Vec<u32> = f.blocks.iter().map(|b| b.label.0).collect();
        // The fast blocks and the return's copy, the split block, the slow ones.
        assert_eq!(labels, [0, 1, 10, 11, 2, 3]);
        assert!(matches!(f.blocks[1].terminator, Terminator::Branch(BlockId(10))));
        assert!(matches!(f.blocks[2].terminator, Terminator::Return(Some(Operand::Value(Value(6))))));
        // The slow path returns its own copy of the result.
        let Terminator::Return(Some(Operand::Value(r))) = f.blocks[5].terminator else { panic!() };
        assert_ne!(r, Value(6));
    }
}

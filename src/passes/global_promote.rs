//! Keep a file-scope variable in a register across a loop that updates it.
//!
//!   pre:   br loop                          pre:   %x0 = load @g; store %x0 -> %a; br loop
//!   loop:  %x = load @g                     loop:  %x = load %a
//!          store %x + k -> @g        =>            store %x + k -> %a
//!          ...; condbr %c, loop, exit              ...; condbr %c, loop, exit
//!   exit:  ...                              exit:  %y = load %a; store %y -> @g; ...
//!
//! and mem2reg then makes `%a` a phi. A generator's state (`rng_state +=
//! ...` in every call of an inlined `rng()`) was otherwise loaded and stored
//! on every iteration, each load waiting for the previous store.
//!
//! Only `static` variables whose address the module uses for nothing but
//! direct loads and stores of one type (so no pointer can reach them), in
//! loops with no calls, inline assembly or other instructions that might
//! touch memory unseen, and whose exits are reached only from the loop. The
//! outermost such loop is used, so the value stays in a register through the
//! whole nest. Runs once, after inlining.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Terminator, Value};
use super::loop_analysis;

pub(crate) fn promote_module(module: &mut IrModule) -> usize {
    let candidates = scalar_globals(module);
    if candidates.is_empty() {
        return 0;
    }
    let mut total = 0;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration && !f.blocks.is_empty()) {
        let n = promote_function(func, &candidates);
        if n > 0 {
            crate::ir::mem2reg::promote_function_allocas(func);
            super::copy_prop::propagate_copies(func);
            super::dce::eliminate_dead_code(func);
            total += n;
        }
    }
    total
}

/// Static, non-thread-local globals whose address only ever reaches direct,
/// non-volatile loads and stores of a single scalar type (their type).
fn scalar_globals(module: &IrModule) -> FxHashMap<String, IrType> {
    let mut out: FxHashMap<String, IrType> = FxHashMap::default();
    for g in &module.globals {
        let scalar = matches!(g.ty, IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32 | IrType::U32
            | IrType::I64 | IrType::U64 | IrType::Ptr | IrType::F32 | IrType::F64);
        if g.is_static && !g.is_extern && !g.is_thread_local && !g.is_const && g.section.is_none() && !g.is_used
            && scalar && g.size == g.ty.size()
        {
            out.insert(g.name.clone(), g.ty);
        }
    }
    // Anything else naming it: another global's initializer, an alias,
    // assembly that might use the symbol.
    let mut escaped: FxHashSet<String> = FxHashSet::default();
    for g in &module.globals {
        g.init.for_each_ref(&mut |name| { escaped.insert(name.to_string()); });
    }
    for (alias, target, _) in &module.aliases {
        escaped.insert(alias.clone());
        escaped.insert(target.clone());
    }
    for asm in &module.toplevel_asm {
        out.retain(|name, _| !asm.contains(name.as_str()));
    }
    for func in &module.functions {
        let mut addr: FxHashMap<u32, &str> = FxHashMap::default();
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            match inst {
                Instruction::GlobalAddr { dest, name } => { addr.insert(dest.0, name.as_str()); }
                Instruction::InlineAsm { template, .. } => out.retain(|name, _| !template.contains(name.as_str())),
                _ => {}
            }
        }
        for block in &func.blocks {
            for inst in &block.instructions {
                match inst {
                    Instruction::Load { ptr, ty, volatile, seg_override, .. } => {
                        if let Some(&name) = addr.get(&ptr.0) {
                            if *volatile || *seg_override != AddressSpace::Default || out.get(name) != Some(ty) {
                                escaped.insert(name.to_string());
                            }
                        }
                    }
                    Instruction::Store { val, ptr, ty, volatile, seg_override } => {
                        if let Some(&name) = addr.get(&ptr.0) {
                            if *volatile || *seg_override != AddressSpace::Default || out.get(name) != Some(ty) {
                                escaped.insert(name.to_string());
                            }
                        }
                        if let crate::ir::reexports::Operand::Value(v) = val {
                            if let Some(&name) = addr.get(&v.0) {
                                escaped.insert(name.to_string());
                            }
                        }
                    }
                    _ => inst.for_each_used_value(|v| {
                        if let Some(&name) = addr.get(&v) {
                            escaped.insert(name.to_string());
                        }
                    }),
                }
            }
            block.terminator.for_each_used_value(|v| {
                if let Some(&name) = addr.get(&v) {
                    escaped.insert(name.to_string());
                }
            });
        }
    }
    out.retain(|name, _| !escaped.contains(name));
    out
}

/// Instructions a loop may contain: none of them touches memory other than
/// through an explicit load or store address.
fn plain(inst: &Instruction) -> bool {
    matches!(inst,
        Instruction::BinOp { .. } | Instruction::UnaryOp { .. } | Instruction::Cmp { .. } | Instruction::Cast { .. }
        | Instruction::Copy { .. } | Instruction::Select { .. } | Instruction::GetElementPtr { .. }
        | Instruction::GlobalAddr { .. } | Instruction::Phi { .. }
        | Instruction::Load { volatile: false, .. } | Instruction::Store { volatile: false, .. })
}

fn promote_function(func: &mut IrFunction, candidates: &FxHashMap<String, IrType>) -> usize {
    // The candidates this function accesses, by address value.
    let mut addr: FxHashMap<u32, String> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GlobalAddr { dest, name } = inst {
            if candidates.contains_key(name) {
                addr.insert(dest.0, name.clone());
            }
        }
    }
    if addr.is_empty() || func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let mut loops = loop_analysis::merge_loops_by_header(
        loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
    // Outermost first: inner loops then see the variable already promoted.
    loops.sort_by_key(|l| std::cmp::Reverse(l.body.len()));

    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut promoted = 0;
    for lp in &loops {
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        let mut body: Vec<usize> = lp.body.iter().copied().collect();
        body.sort_unstable();
        let ok = body.iter().all(|&b| {
            let block = &func.blocks[b];
            block.instructions.iter().all(plain)
                && matches!(block.terminator, Terminator::Branch(_) | Terminator::CondBranch { .. } | Terminator::Switch { .. })
        });
        if !ok {
            continue;
        }
        // Exits, each reached only from the loop.
        let mut exits: Vec<usize> = Vec::new();
        for &b in &body {
            for &s in cfg.succs.row(b) {
                let s = s as usize;
                if !lp.body.contains(&s) && !exits.contains(&s) {
                    exits.push(s);
                }
            }
        }
        if exits.iter().any(|&e| cfg.preds.row(e).iter().any(|&p| !lp.body.contains(&(p as usize)))) {
            continue;
        }
        // The variables the loop accesses, and whether it writes them.
        let mut used: Vec<(String, bool)> = Vec::new();
        for &b in &body {
            for inst in &func.blocks[b].instructions {
                let (ptr, writes) = match inst {
                    Instruction::Load { ptr, .. } => (ptr, false),
                    Instruction::Store { ptr, .. } => (ptr, true),
                    _ => continue,
                };
                if let Some(name) = addr.get(&ptr.0) {
                    match used.iter_mut().find(|(n, _)| n == name) {
                        Some(u) => u.1 |= writes,
                        None => used.push((name.clone(), writes)),
                    }
                }
            }
        }
        for (name, writes) in used {
            let ty = candidates[&name];
            let mut fresh = || { next_id += 1; Value(next_id - 1) };
            let slot = fresh();
            let names: FxHashSet<u32> = addr.iter().filter(|(_, n)| **n == name).map(|(v, _)| *v).collect();
            for &b in &body {
                for inst in &mut func.blocks[b].instructions {
                    match inst {
                        Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } if names.contains(&ptr.0) => *ptr = slot,
                        _ => {}
                    }
                }
            }
            let (g, x) = (fresh(), fresh());
            // After the entry's allocas: the first ones are the parameters'.
            let entry = &mut func.blocks[0];
            let at = entry.instructions.iter().take_while(|i| matches!(i, Instruction::Alloca { .. })).count();
            entry.instructions.insert(at, Instruction::Alloca { dest: slot, ty, size: ty.size(), align: ty.size(), volatile: false });
            if !entry.source_spans.is_empty() {
                let span = entry.source_spans[at.min(entry.source_spans.len() - 1)];
                entry.source_spans.insert(at.min(entry.source_spans.len()), span);
            }
            let load_in = [
                Instruction::GlobalAddr { dest: g, name: name.clone() },
                Instruction::Load { dest: x, ptr: g, ty, seg_override: AddressSpace::Default, volatile: false },
                Instruction::Store { val: crate::ir::reexports::Operand::Value(x), ptr: slot, ty, seg_override: AddressSpace::Default, volatile: false },
            ];
            push_end(&mut func.blocks[pre], load_in);
            if writes {
                for &e in &exits {
                    let (g, y) = (fresh(), fresh());
                    let store_out = [
                        Instruction::GlobalAddr { dest: g, name: name.clone() },
                        Instruction::Load { dest: y, ptr: slot, ty, seg_override: AddressSpace::Default, volatile: false },
                        Instruction::Store { val: crate::ir::reexports::Operand::Value(y), ptr: g, ty, seg_override: AddressSpace::Default, volatile: false },
                    ];
                    let block = &mut func.blocks[e];
                    let at = block.instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
                    if !block.source_spans.is_empty() {
                        let span = block.source_spans[at.min(block.source_spans.len() - 1)];
                        block.source_spans.splice(at..at, std::iter::repeat_n(span, store_out.len()));
                    }
                    block.instructions.splice(at..at, store_out);
                }
            }
            promoted += 1;
        }
    }
    func.next_value_id = next_id;
    promoted
}

fn push_end(block: &mut crate::ir::reexports::BasicBlock, insts: [Instruction; 3]) {
    if !block.source_spans.is_empty() {
        let span = *block.source_spans.last().unwrap();
        block.source_spans.extend(std::iter::repeat_n(span, insts.len()));
    }
    block.instructions.extend(insts);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, BlockId, IrBinOp, IrCmpOp, IrConst, IrGlobal, GlobalInit, Operand};

    fn global(name: &str) -> IrGlobal {
        IrGlobal {
            name: name.to_string(), ty: IrType::U64, size: 8, align: 8, init: GlobalInit::Zero,
            is_static: true, is_extern: false, is_common: false, section: None, is_weak: false,
            visibility: None, has_explicit_align: false, is_const: false, is_used: false, is_thread_local: false,
        }
    }

    /// `for (i = 0; i < 10; i++) g += 1; return g;`
    fn counting_loop() -> IrFunction {
        let mut func = IrFunction::new("f".to_string(), IrType::U64, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        let st = |val, ptr| Instruction::Store { val, ptr: Value(ptr), ty: IrType::U64, seg_override: AddressSpace::Default, volatile: false };
        let ld = |dest, ptr| Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::U64, seg_override: AddressSpace::Default, volatile: false };
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(1), ty: IrType::I32, incoming: vec![(Operand::Const(IrConst::I32(0)), BlockId(0)), (v(5), BlockId(2))] },
                Instruction::Cmp { dest: Value(2), op: IrCmpOp::Slt, lhs: v(1), rhs: Operand::Const(IrConst::I32(10)), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(2), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, vec![
                Instruction::GlobalAddr { dest: Value(3), name: "g".to_string() },
                ld(4, 3),
                Instruction::BinOp { dest: Value(6), op: IrBinOp::Add, lhs: v(4), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::U64 },
                st(v(6), 3),
                Instruction::BinOp { dest: Value(5), op: IrBinOp::Add, lhs: v(1), rhs: Operand::Const(IrConst::I32(1)), ty: IrType::I32 },
            ], Terminator::Branch(BlockId(1))),
            block(3, vec![Instruction::GlobalAddr { dest: Value(7), name: "g".to_string() }, ld(8, 7)], Terminator::Return(Some(v(8)))),
        ];
        func.next_value_id = 9;
        func
    }

    fn accesses_g(block: &BasicBlock) -> bool {
        let addrs: Vec<Value> = block.instructions.iter().filter_map(|i| match i {
            Instruction::GlobalAddr { dest, .. } => Some(*dest),
            _ => None,
        }).collect();
        block.instructions.iter().any(|i| matches!(i, Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } if addrs.contains(ptr)))
    }

    #[test]
    fn loop_keeps_static_in_register() {
        let mut module = IrModule::new();
        module.globals.push(global("g"));
        module.functions.push(counting_loop());
        assert_eq!(promote_module(&mut module), 1);
        let f = &module.functions[0];
        // The loop body no longer touches g; the value is loaded before the
        // loop and stored where it leaves.
        assert!(!accesses_g(&f.blocks[2]));
        assert!(accesses_g(&f.blocks[0]));
        assert!(f.blocks[3].instructions.iter().any(|i| matches!(i, Instruction::Store { .. })));
        assert!(!f.blocks[0].instructions.iter().any(|i| matches!(i, Instruction::Alloca { .. })));
    }

    #[test]
    fn escaping_or_global_static_stays_in_memory() {
        // Its address stored to memory elsewhere in the module.
        let mut module = IrModule::new();
        module.globals.push(global("g"));
        module.functions.push(counting_loop());
        let mut other = IrFunction::new("h".to_string(), IrType::Void, vec![], false);
        other.blocks = vec![BasicBlock {
            label: BlockId(9),
            instructions: vec![
                Instruction::GlobalAddr { dest: Value(0), name: "g".to_string() },
                Instruction::Alloca { dest: Value(1), ty: IrType::Ptr, size: 8, align: 8, volatile: false },
                Instruction::Store { val: Operand::Value(Value(0)), ptr: Value(1), ty: IrType::Ptr, seg_override: AddressSpace::Default, volatile: false },
            ],
            terminator: Terminator::Return(None),
            source_spans: Vec::new(),
        }];
        other.next_value_id = 2;
        module.functions.push(other);
        assert_eq!(promote_module(&mut module), 0);
        // Not static.
        let mut module = IrModule::new();
        let mut g = global("g");
        g.is_static = false;
        module.globals.push(g);
        module.functions.push(counting_loop());
        assert_eq!(promote_module(&mut module), 0);
    }
}

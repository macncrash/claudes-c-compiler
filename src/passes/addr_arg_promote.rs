//! Keep a local in a register when its address only goes to calls that use
//! it and let go of it.
//!
//!   %a = alloca i32                        %a = alloca i32; %r = alloca i32
//!   store 0 -> %a                          store 0 -> %r
//!   ...                                    ...
//!   call f(..., %a)              =>        %t = load %r; store %t -> %a
//!                                          call f(..., %a)
//!                                          %u = load %a; store %u -> %r
//!   %x = load %a                           %x = load %r
//!
//! and mem2reg then promotes `%r`, leaving `%a` touched only around the
//! calls. SQLite's interpreter loop passes the `u32` a varint is decoded
//! into, the result of a seek, and others to helpers by address; each was
//! read and written through the stack.
//!
//! The callee must be one `ModRef` summarizes and must not capture that
//! parameter, so nothing can reach the slot outside the call. The copy back
//! is left out when the callee does not write through the parameter. Only
//! register-sized allocas accessed directly with their own type and not
//! volatile; a parameter's only when the IR stores its value into it (not
//! one the backend fills in the prologue). Runs once, after inlining.
//!
//! A variable assigned in many places is left alone (see MAX_STORES).

use crate::common::types::AliasClass;
use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::AddressSpace;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Operand, Value};
use crate::common::types::IrType;
use super::mod_ref::ModRef;

/// Most stores to a variable this pass promotes. A status variable assigned
/// throughout a large function (the interpreter's `rc`, 153 assignments,
/// passed by address once) becomes a value live across all of it, and the
/// register it takes for that whole stretch is one the values of the hot
/// paths needed: speedtest1 ran 1.9% more instructions with it promoted and
/// 1.6% fewer without it. Below this, promoting paid off.
const MAX_STORES: usize = 32;

pub(crate) fn promote_module(module: &mut IrModule) -> usize {
    let mr = ModRef::compute(module);
    let mut total = 0;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration && !f.blocks.is_empty()) {
        let n = promote_function(func, &mr);
        if n > 0 {
            crate::ir::mem2reg::promote_function_allocas(func);
            super::copy_prop::propagate_copies(func);
            super::dce::eliminate_dead_code(func);
            total += n;
        }
    }
    total
}

fn promote_function(func: &mut IrFunction, mr: &ModRef) -> usize {
    // A parameter's alloca whose value the IR stores itself (ParamRef, then
    // a store into it: a scalar parameter whose address is taken) is like
    // any other local; one the backend fills in the prologue is not.
    let param_refs: FxHashSet<u32> = func.blocks.iter().flat_map(|b| &b.instructions)
        .filter_map(|i| match i { Instruction::ParamRef { dest, .. } => Some(dest.0), _ => None })
        .collect();
    let stored_params: FxHashSet<u32> = func.blocks.iter().flat_map(|b| &b.instructions)
        .filter_map(|i| match i {
            Instruction::Store { val: Operand::Value(v), ptr, .. } if param_refs.contains(&v.0) => Some(ptr.0),
            _ => None,
        })
        .collect();
    let params: FxHashSet<u32> = func.param_alloca_values.iter().map(|v| v.0)
        .filter(|a| !stored_params.contains(a))
        .collect();
    let mut candidates: FxHashMap<u32, (IrType, usize, usize)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::Alloca { dest, ty, size, align, volatile: false } = inst {
            let scalar = matches!(ty, IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32
                | IrType::U32 | IrType::I64 | IrType::U64 | IrType::Ptr | IrType::F32 | IrType::F64);
            if scalar && *size <= 8 && !params.contains(&dest.0) {
                candidates.insert(dest.0, (*ty, *size, *align));
            }
        }
    }
    if candidates.is_empty() {
        return 0;
    }
    // Candidates passed to at least one call, and whether some call may
    // write each one (then the value is copied back after each call); any
    // other use disqualifies.
    let mut bad: FxHashSet<u32> = FxHashSet::default();
    let mut passed: FxHashMap<u32, bool> = FxHashMap::default();
    let mut stores: FxHashMap<u32, usize> = FxHashMap::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ptr, ty, volatile, .. } => {
                    if candidates.get(&ptr.0).is_some_and(|c| *volatile || c.0 != *ty) {
                        bad.insert(ptr.0);
                    }
                }
                Instruction::Store { val, ptr, ty, volatile, .. } => {
                    if candidates.get(&ptr.0).is_some_and(|c| *volatile || c.0 != *ty) {
                        bad.insert(ptr.0);
                    }
                    *stores.entry(ptr.0).or_insert(0) += 1;
                    if let Operand::Value(v) = val {
                        bad.insert(v.0);
                    }
                }
                Instruction::Call { func: callee, info } => {
                    let effects = mr.call_effects(callee, info);
                    for (i, a) in info.args.iter().enumerate() {
                        let Operand::Value(v) = a else { continue };
                        if !candidates.contains_key(&v.0) {
                            continue;
                        }
                        match effects {
                            Some(e) if !e.captures_param(i) => {
                                *passed.entry(v.0).or_insert(false) |= e.writes_param(i);
                            }
                            _ => { bad.insert(v.0); }
                        }
                    }
                }
                _ => inst.for_each_used_value(|v| { bad.insert(v); }),
            }
        }
        for v in block.terminator.used_values() {
            bad.insert(v);
        }
    }
    passed.retain(|v, _| !bad.contains(v) && stores.get(v).copied().unwrap_or(0) <= MAX_STORES);
    if passed.is_empty() {
        return 0;
    }

    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut fresh = || { next_id += 1; Value(next_id - 1) };
    // Each candidate's register copy.
    let slots: FxHashMap<u32, Value> = passed.keys().map(|&v| (v, fresh())).collect();
    let load = |dest: Value, ptr: Value, ty: IrType| {
        Instruction::Load { dest, ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    };
    let store = |val: Value, ptr: Value, ty: IrType| {
        Instruction::Store { val: Operand::Value(val), ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    };
    for block in &mut func.blocks {
        let calls = block.instructions.iter().any(|i| matches!(i, Instruction::Call { info, .. }
            if info.args.iter().any(|a| matches!(a, Operand::Value(v) if slots.contains_key(&v.0)))));
        if !calls {
            for inst in &mut block.instructions {
                if let Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } = inst {
                    if let Some(&s) = slots.get(&ptr.0) {
                        *ptr = s;
                    }
                }
            }
            continue;
        }
        let spans = !block.source_spans.is_empty();
        let mut insts = Vec::with_capacity(block.instructions.len() + 8);
        let mut new_spans = Vec::with_capacity(if spans { insts.capacity() } else { 0 });
        for (k, mut inst) in std::mem::take(&mut block.instructions).into_iter().enumerate() {
            let span = if spans { block.source_spans[k.min(block.source_spans.len() - 1)] } else { crate::common::source::Span::new(0, 0, 0) };
            let mut after = Vec::new();
            match &mut inst {
                Instruction::Load { ptr, .. } | Instruction::Store { ptr, .. } => {
                    if let Some(&s) = slots.get(&ptr.0) {
                        *ptr = s;
                    }
                }
                Instruction::Call { info, .. } => {
                    let mut seen: Vec<u32> = Vec::new();
                    for a in &info.args {
                        let Operand::Value(v) = a else { continue };
                        let Some(&s) = slots.get(&v.0) else { continue };
                        if seen.contains(&v.0) {
                            continue;
                        }
                        seen.push(v.0);
                        let ty = candidates[&v.0].0;
                        let t = fresh();
                        insts.push(load(t, s, ty));
                        insts.push(store(t, *v, ty));
                        if spans {
                            new_spans.extend([span, span]);
                        }
                        if passed[&v.0] {
                            let u = fresh();
                            after.push(load(u, *v, ty));
                            after.push(store(u, s, ty));
                        }
                    }
                }
                _ => {}
            }
            insts.push(inst);
            if spans {
                new_spans.push(span);
                new_spans.extend(std::iter::repeat_n(span, after.len()));
            }
            insts.extend(after);
        }
        block.instructions = insts;
        if spans {
            block.source_spans = new_spans;
        }
    }
    // The register copies, after the entry's allocas (the first ones are
    // the parameters').
    let entry = &mut func.blocks[0];
    let at = entry.instructions.iter().take_while(|i| matches!(i, Instruction::Alloca { .. })).count();
    let mut order: Vec<(u32, Value)> = slots.iter().map(|(&a, &s)| (a, s)).collect();
    order.sort_unstable_by_key(|&(a, _)| a);
    for (a, s) in order {
        let (ty, size, align) = candidates[&a];
        entry.instructions.insert(at, Instruction::Alloca { dest: s, ty, size, align, volatile: false });
        if !entry.source_spans.is_empty() {
            let span = entry.source_spans[at.min(entry.source_spans.len() - 1)];
            entry.source_spans.insert(at.min(entry.source_spans.len()), span);
        }
    }
    func.next_value_id = next_id;
    slots.len()
}

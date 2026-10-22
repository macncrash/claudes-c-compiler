//! Interprocedural memory effects ("mod/ref") and a per-function alias model.
//!
//! Without knowing the callee, a call is a barrier to every memory
//! optimization: it may write anything. For each function defined in the
//! module this computes what a call to it can do to its caller's memory:
//!
//! - which pointer parameters it may write through (`writes_params`);
//! - whether it may write any other memory: globals, memory reached through
//!   a loaded pointer, anything an unknown callee touches (`writes_other`);
//! - which pointer parameters it may capture: store somewhere, return, or
//!   pass where they are captured (`captures_params`);
//! - whether it might not return: it has a loop, or calls something unknown
//!   or something that might not return (`may_not_return`).
//!
//! Writes to a function's own locals are invisible to its callers and not
//! counted. Summaries are a fixpoint over the call graph starting from "no
//! effects", so recursive functions are summarized too.
//!
//! `AliasModel` answers, within one function, whether an instruction may
//! write the memory a pointer points into. Pointers are traced back to a
//! root (an alloca, a parameter, a global, or unknown) flow-insensitively,
//! which is sound for the non-SSA IR after phi elimination as well. An
//! alloca is private when its address reaches only loads, stores, pointer
//! arithmetic that stays with it, and call arguments that are not captured;
//! then only explicit writes to it, or calls given a pointer into it as a
//! written argument, can change it.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::ir::reexports::{CallInfo, Instruction, IrFunction, IrModule, Operand, Terminator};

/// Where a pointer may point: into one alloca, parameter's object or global,
/// or anywhere (`Unknown`). `None` is the bottom of the lattice: no
/// definition seen yet, or a value that is not a pointer.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub(crate) enum Root {
    #[default]
    None,
    Alloca(u32),
    Param(u32),
    /// A global, by an index interned per function.
    Global(u32),
    Unknown,
}

impl Root {
    fn join(self, other: Root) -> Root {
        match (self, other) {
            (Root::None, r) | (r, Root::None) => r,
            (a, b) if a == b => a,
            _ => Root::Unknown,
        }
    }
}

/// What a value may be derived from: the parameters (as a bitmask), memory
/// other than the function's locals (`other`), and its single root.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
struct Prov {
    params: u64,
    other: bool,
    root: Root,
}

impl Prov {
    const OTHER: Prov = Prov { params: 0, other: true, root: Root::Unknown };

    fn join(self, o: Prov) -> Prov {
        Prov { params: self.params | o.params, other: self.other || o.other, root: self.root.join(o.root) }
    }
}

/// Provenance of every value in `func`, indexed by value ID.
fn compute_provenance(func: &IrFunction) -> Vec<Prov> {
    let mut n = func.next_value_id as usize;
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                n = n.max(d.0 as usize + 1);
            }
            inst.for_each_used_value(|v| n = n.max(v as usize + 1));
        }
    }
    let mut globals: FxHashMap<&str, u32> = FxHashMap::default();
    let mut prov = vec![Prov::default(); n];
    loop {
        let mut changed = false;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            let Some(dest) = inst.dest() else { continue };
            let op = |o: &Operand| match o {
                Operand::Value(v) => prov[v.0 as usize],
                Operand::Const(_) => Prov::OTHER,
            };
            let new = match inst {
                Instruction::Alloca { .. } | Instruction::DynAlloca { .. } => {
                    Prov { params: 0, other: false, root: Root::Alloca(dest.0) }
                }
                Instruction::ParamRef { param_idx, .. } => Prov {
                    params: if *param_idx < 64 { 1 << param_idx } else { 0 },
                    other: *param_idx >= 64,
                    root: Root::Param(*param_idx as u32),
                },
                Instruction::GlobalAddr { name, .. } => {
                    let next = globals.len() as u32;
                    let id = *globals.entry(name.as_str()).or_insert(next);
                    Prov { params: 0, other: true, root: Root::Global(id) }
                }
                Instruction::GetElementPtr { base, .. } => prov[base.0 as usize],
                Instruction::Copy { src, .. } | Instruction::Cast { src, .. } | Instruction::UnaryOp { src, .. } => op(src),
                Instruction::BinOp { lhs, rhs, .. } => op(lhs).join(op(rhs)),
                Instruction::Select { true_val, false_val, .. } => op(true_val).join(op(false_val)),
                Instruction::Phi { incoming, .. } => {
                    incoming.iter().fold(Prov::default(), |acc, (o, _)| acc.join(op(o)))
                }
                // A flag, never a pointer.
                Instruction::Cmp { .. } => Prov::default(),
                // Loads, call results and the like: anything.
                _ => Prov::OTHER,
            };
            let slot = &mut prov[dest.0 as usize];
            let joined = slot.join(new);
            if joined != *slot {
                *slot = joined;
                changed = true;
            }
        }
        if !changed {
            return prov;
        }
    }
}

/// What a call to a function can do to its caller's memory.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub(crate) struct FnEffects {
    pub writes_other: bool,
    pub writes_params: u64,
    pub captures_params: u64,
    pub may_not_return: bool,
}

impl FnEffects {
    fn join(self, o: FnEffects) -> FnEffects {
        FnEffects {
            writes_other: self.writes_other || o.writes_other,
            writes_params: self.writes_params | o.writes_params,
            captures_params: self.captures_params | o.captures_params,
            may_not_return: self.may_not_return || o.may_not_return,
        }
    }

    fn writes_param(&self, i: usize) -> bool {
        i >= 64 || self.writes_params >> i & 1 != 0
    }

    fn captures_param(&self, i: usize) -> bool {
        i >= 64 || self.captures_params >> i & 1 != 0
    }
}

/// Rounds of the call-graph fixpoint before giving up on every summary.
const MAX_ROUNDS: usize = 64;

/// Effects of the module's functions, keyed by name, with their parameter
/// counts. Functions not listed are unknown.
#[derive(Default)]
pub(crate) struct ModRef {
    effects: FxHashMap<String, (FnEffects, usize)>,
}

impl ModRef {
    pub(crate) fn compute(module: &IrModule) -> ModRef {
        let candidates: Vec<&IrFunction> = module.functions.iter().filter(|f| summarizable(f)).collect();
        let mut mr = ModRef::default();
        for f in &candidates {
            mr.effects.insert(f.name.clone(), (FnEffects::default(), f.params.len()));
        }
        let provs: Vec<Vec<Prov>> = candidates.iter().map(|f| compute_provenance(f)).collect();
        let fixed: Vec<FnEffects> = candidates.iter().map(|f| intrinsic_effects(f)).collect();
        for _ in 0..MAX_ROUNDS {
            let mut changed = false;
            for (i, f) in candidates.iter().enumerate() {
                let e = summarize(f, &provs[i], &mr).join(fixed[i]);
                let slot = &mut mr.effects.get_mut(&f.name).unwrap().0;
                let joined = slot.join(e);
                if joined != *slot {
                    *slot = joined;
                    changed = true;
                }
            }
            if !changed {
                return mr;
            }
        }
        ModRef::default()
    }

    /// The callee's effects, if it is summarized and the call passes its
    /// parameters one per argument.
    pub(crate) fn call_effects(&self, name: &str, info: &CallInfo) -> Option<FnEffects> {
        let &(e, nparams) = self.effects.get(name)?;
        let plain = !info.is_variadic && !info.is_sret && info.args.len() == nparams
            && info.struct_arg_sizes.iter().all(|s| s.is_none());
        plain.then_some(e)
    }

    /// Whether `inst` is a call that writes no memory its caller can see.
    pub(crate) fn writes_nothing(&self, inst: &Instruction) -> bool {
        let Instruction::Call { func, info } = inst else { return false };
        self.call_effects(func, info)
            .is_some_and(|e| !e.writes_other && (0..info.args.len()).all(|i| !e.writes_param(i)))
    }

    /// Whether executing `inst` always continues to the next instruction
    /// (ignoring traps): it is not a call that might not return.
    pub(crate) fn always_continues(&self, inst: &Instruction) -> bool {
        match inst {
            Instruction::Call { func, info } => self.call_effects(func, info).is_some_and(|e| !e.may_not_return),
            Instruction::CallIndirect { .. } | Instruction::InlineAsm { .. } => false,
            _ => true,
        }
    }
}

/// Functions whose calls this analysis can describe: defined here, not
/// replaceable at link time, and with parameters passed one per argument.
fn summarizable(f: &IrFunction) -> bool {
    !f.is_declaration && !f.is_weak && !f.is_gnu_inline_def && !f.is_naked && !f.is_variadic
        && !f.uses_sret && f.params.len() <= 64 && f.params.iter().all(|p| p.struct_size.is_none())
}

/// Effects that do not depend on callees: loops (which might not end) and
/// parameters kept in an alloca, whose copies the analysis cannot follow.
fn intrinsic_effects(f: &IrFunction) -> FnEffects {
    let mut e = FnEffects { may_not_return: has_cycle(f), ..FnEffects::default() };
    if !f.param_alloca_values.is_empty() {
        let mut used = FxHashSet::default();
        for inst in f.blocks.iter().flat_map(|b| &b.instructions) {
            inst.for_each_used_value(|v| { used.insert(v); });
        }
        for (i, pv) in f.param_alloca_values.iter().enumerate() {
            if used.contains(&pv.0) {
                let bit = if i < 64 { 1 << i } else { 0 };
                e.writes_params |= bit;
                e.captures_params |= bit;
                e.writes_other |= i >= 64;
            }
        }
    }
    e
}

fn has_cycle(f: &IrFunction) -> bool {
    let index: FxHashMap<_, usize> = f.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    let succs = |b: usize| -> Vec<usize> {
        let labels = match &f.blocks[b].terminator {
            Terminator::Branch(l) => vec![*l],
            Terminator::CondBranch { true_label, false_label, .. } => vec![*true_label, *false_label],
            Terminator::Switch { cases, default, .. } => {
                cases.iter().map(|(_, l)| *l).chain(std::iter::once(*default)).collect()
            }
            Terminator::IndirectBranch { possible_targets, .. } => possible_targets.clone(),
            Terminator::Return(_) | Terminator::Unreachable => vec![],
        };
        labels.iter().filter_map(|l| index.get(l).copied()).collect()
    };
    // 0 = unvisited, 1 = on the DFS stack, 2 = done.
    let mut state = vec![0u8; f.blocks.len()];
    for start in 0..f.blocks.len() {
        if state[start] != 0 {
            continue;
        }
        let mut stack = vec![(start, succs(start), 0usize)];
        state[start] = 1;
        while let Some((b, out, i)) = stack.last_mut() {
            if *i < out.len() {
                let s = out[*i];
                *i += 1;
                match state[s] {
                    0 => {
                        state[s] = 1;
                        let next = succs(s);
                        stack.push((s, next, 0));
                    }
                    1 => return true,
                    _ => {}
                }
            } else {
                state[*b] = 2;
                stack.pop();
            }
        }
    }
    false
}

/// One function's effects on its callers' memory, given the current
/// summaries of its callees.
fn summarize(f: &IrFunction, prov: &[Prov], mr: &ModRef) -> FnEffects {
    let mut e = FnEffects::default();
    let op = |o: &Operand| match o {
        Operand::Value(v) => prov[v.0 as usize],
        Operand::Const(_) => Prov::OTHER,
    };
    fn write(e: &mut FnEffects, p: Prov) {
        if p.other || p.root == Root::None {
            e.writes_other = true;
        }
        e.writes_params |= p.params;
    }
    fn capture(e: &mut FnEffects, p: Prov) {
        e.captures_params |= p.params;
    }
    for block in &f.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Store { val, ptr, .. } => {
                    write(&mut e, prov[ptr.0 as usize]);
                    capture(&mut e, op(val));
                }
                Instruction::Memcpy { dest, .. } => write(&mut e, prov[dest.0 as usize]),
                Instruction::AtomicRmw { ptr, val, .. } | Instruction::AtomicStore { ptr, val, .. } => {
                    write(&mut e, op(ptr));
                    capture(&mut e, op(val));
                }
                Instruction::AtomicCmpxchg { ptr, expected, desired, .. } => {
                    write(&mut e, op(ptr));
                    capture(&mut e, op(expected));
                    capture(&mut e, op(desired));
                }
                Instruction::VaStart { va_list_ptr } | Instruction::VaEnd { va_list_ptr }
                | Instruction::VaArg { va_list_ptr, .. } => write(&mut e, prov[va_list_ptr.0 as usize]),
                Instruction::VaCopy { dest_ptr, .. } => write(&mut e, prov[dest_ptr.0 as usize]),
                Instruction::VaArgStruct { dest_ptr, va_list_ptr, .. } => {
                    write(&mut e, prov[dest_ptr.0 as usize]);
                    write(&mut e, prov[va_list_ptr.0 as usize]);
                }
                Instruction::Fence { .. } => e.writes_other = true,
                Instruction::InlineAsm { .. } => {
                    e.writes_other = true;
                    e.may_not_return = true;
                    inst.for_each_used_value(|v| {
                        write(&mut e, prov[v as usize]);
                        capture(&mut e, prov[v as usize]);
                    });
                }
                Instruction::Intrinsic { dest_ptr, args, op: iop, .. } => {
                    if let Some(p) = dest_ptr {
                        write(&mut e, prov[p.0 as usize]);
                    }
                    if !iop.is_pure() {
                        e.writes_other = true;
                    }
                    for a in args {
                        capture(&mut e, op(a));
                    }
                }
                Instruction::Call { func, info } => match mr.call_effects(func, info) {
                    Some(ce) => {
                        e.writes_other |= ce.writes_other;
                        e.may_not_return |= ce.may_not_return;
                        for (i, a) in info.args.iter().enumerate() {
                            if ce.writes_param(i) {
                                write(&mut e, op(a));
                            }
                            if ce.captures_param(i) {
                                capture(&mut e, op(a));
                            }
                        }
                    }
                    None => unknown_call(&mut e, info, &op),
                },
                Instruction::CallIndirect { info, .. } => unknown_call(&mut e, info, &op),
                _ => {}
            }
        }
        if let Terminator::Return(Some(o)) = &block.terminator {
            capture(&mut e, op(o));
        }
    }

    fn unknown_call(e: &mut FnEffects, info: &CallInfo, op: &impl Fn(&Operand) -> Prov) {
        e.writes_other = true;
        e.may_not_return = true;
        for a in &info.args {
            write(e, op(a));
            capture(e, op(a));
        }
    }
    e
}

/// Alias queries within one function.
pub(crate) struct AliasModel {
    prov: Vec<Prov>,
    /// Allocas whose address never escapes (see the module comment).
    private: FxHashSet<u32>,
}

impl AliasModel {
    pub(crate) fn new(func: &IrFunction, mr: &ModRef) -> AliasModel {
        let prov = compute_provenance(func);
        let mut private: FxHashSet<u32> = func.blocks.iter().flat_map(|b| &b.instructions).filter_map(|inst| match inst {
            Instruction::Alloca { dest, volatile: false, .. } | Instruction::DynAlloca { dest, .. } => Some(dest.0),
            _ => None,
        }).collect();
        let root_of = |o: &Operand| match o {
            Operand::Value(v) => prov[v.0 as usize].root,
            Operand::Const(_) => Root::Unknown,
        };
        // Roots whose address escapes. A value derived from an alloca keeps
        // it private only while the result has the same root.
        let mut escaped: Vec<Root> = Vec::new();
        let derive = |escaped: &mut Vec<Root>, o: &Operand, dest: &crate::ir::reexports::Value| {
            let r = root_of(o);
            if r != prov[dest.0 as usize].root {
                escaped.push(r);
            }
        };
        for block in &func.blocks {
            for inst in &block.instructions {
                match inst {
                    Instruction::Load { .. } | Instruction::Cmp { .. } | Instruction::Memcpy { .. }
                    | Instruction::AtomicLoad { .. } | Instruction::VaStart { .. } | Instruction::VaEnd { .. }
                    | Instruction::VaArg { .. } | Instruction::VaCopy { .. } | Instruction::VaArgStruct { .. }
                    | Instruction::StackRestore { .. } => {}
                    Instruction::Store { val, .. } => escaped.push(root_of(val)),
                    Instruction::GetElementPtr { dest, base, offset, .. } => {
                        derive(&mut escaped, &Operand::Value(*base), dest);
                        escaped.push(root_of(offset));
                    }
                    Instruction::Copy { dest, src } | Instruction::Cast { dest, src, .. }
                    | Instruction::UnaryOp { dest, src, .. } => derive(&mut escaped, src, dest),
                    Instruction::BinOp { dest, lhs, rhs, .. } => {
                        derive(&mut escaped, lhs, dest);
                        derive(&mut escaped, rhs, dest);
                    }
                    Instruction::Select { dest, true_val, false_val, .. } => {
                        derive(&mut escaped, true_val, dest);
                        derive(&mut escaped, false_val, dest);
                    }
                    Instruction::Phi { dest, incoming, .. } => {
                        for (o, _) in incoming {
                            derive(&mut escaped, o, dest);
                        }
                    }
                    Instruction::AtomicRmw { val, .. } | Instruction::AtomicStore { val, .. } => {
                        escaped.push(root_of(val));
                    }
                    Instruction::AtomicCmpxchg { expected, desired, .. } => {
                        escaped.push(root_of(expected));
                        escaped.push(root_of(desired));
                    }
                    Instruction::Intrinsic { args, .. } => escaped.extend(args.iter().map(root_of)),
                    Instruction::Call { func, info } => {
                        let ce = mr.call_effects(func, info);
                        for (i, a) in info.args.iter().enumerate() {
                            if ce.is_none_or(|ce| ce.captures_param(i)) {
                                escaped.push(root_of(a));
                            }
                        }
                    }
                    _ => inst.for_each_used_value(|v| escaped.push(prov[v as usize].root)),
                }
            }
            match &block.terminator {
                Terminator::Return(Some(o)) => escaped.push(root_of(o)),
                Terminator::IndirectBranch { target, .. } => escaped.push(root_of(target)),
                _ => {}
            }
        }
        for r in escaped {
            if let Root::Alloca(a) = r {
                private.remove(&a);
            }
        }
        AliasModel { prov, private }
    }

    /// The root of a pointer value.
    pub(crate) fn root(&self, v: u32) -> Root {
        match self.prov.get(v as usize).map_or(Root::Unknown, |p| p.root) {
            Root::None => Root::Unknown,
            r => r,
        }
    }

    fn root_op(&self, o: &Operand) -> Root {
        match o {
            Operand::Value(v) => self.root(v.0),
            Operand::Const(_) => Root::Unknown,
        }
    }

    pub(crate) fn is_private(&self, r: Root) -> bool {
        matches!(r, Root::Alloca(a) if self.private.contains(&a))
    }

    /// Whether memory under root `a` and memory under root `b` may overlap.
    pub(crate) fn may_alias(&self, a: Root, b: Root) -> bool {
        let unknown = |r: Root| if r == Root::None { Root::Unknown } else { r };
        match (unknown(a), unknown(b)) {
            (Root::Unknown, x) | (x, Root::Unknown) => !self.is_private(x),
            (Root::Alloca(x), Root::Alloca(y)) => x == y,
            // A parameter or global cannot point into this frame's allocas.
            (Root::Alloca(_), _) | (_, Root::Alloca(_)) => false,
            (Root::Global(x), Root::Global(y)) => x == y,
            _ => true,
        }
    }

    /// Call `f` with the root of each object `inst` may write (`Unknown`:
    /// any memory but private allocas).
    pub(crate) fn for_each_write(&self, inst: &Instruction, mr: &ModRef, mut f: impl FnMut(Root)) {
        match inst {
            Instruction::Store { ptr, .. } => f(self.root(ptr.0)),
            Instruction::Memcpy { dest, .. } => f(self.root(dest.0)),
            Instruction::AtomicRmw { ptr, .. } | Instruction::AtomicCmpxchg { ptr, .. }
            | Instruction::AtomicStore { ptr, .. } => f(self.root_op(ptr)),
            Instruction::VaStart { va_list_ptr } | Instruction::VaEnd { va_list_ptr }
            | Instruction::VaArg { va_list_ptr, .. } => f(self.root(va_list_ptr.0)),
            Instruction::VaCopy { dest_ptr, .. } => f(self.root(dest_ptr.0)),
            Instruction::VaArgStruct { dest_ptr, va_list_ptr, .. } => {
                f(self.root(dest_ptr.0));
                f(self.root(va_list_ptr.0));
            }
            Instruction::Intrinsic { dest_ptr, op, .. } => {
                if let Some(p) = dest_ptr {
                    f(self.root(p.0));
                }
                if !op.is_pure() {
                    f(Root::Unknown);
                }
            }
            Instruction::Fence { .. } | Instruction::InlineAsm { .. } | Instruction::CallIndirect { .. } => f(Root::Unknown),
            Instruction::Call { func, info } => match mr.call_effects(func, info) {
                Some(ce) => {
                    if ce.writes_other {
                        f(Root::Unknown);
                    }
                    for (i, a) in info.args.iter().enumerate() {
                        if ce.writes_param(i) {
                            f(self.root_op(a));
                        }
                    }
                }
                None => f(Root::Unknown),
            },
            _ => {}
        }
    }
}

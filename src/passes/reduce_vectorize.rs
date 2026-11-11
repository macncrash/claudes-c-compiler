//! Vectorize counted loops that add per-iteration float terms into scalar
//! sums, keeping each sum's additions in order (AArch64 NEON, float lanes).
//!
//!   h: %i = phi [%i0, pre], [%i1, b]          vh: %vi = phi [%i0, pre], [%vi4, vb]
//!      %s = phi [%s0, pre], [%s1, b]              %vs = phi [%s0, pre], [%vs1, vb]
//!      %c = cmp slt %i, %n                        %vi4 = add %vi, 4
//!      condbr %c, b, exit                         condbr (cmp sle %vi4, %n), vb, h
//!   b: %x = load f32 (p + 4 i)          =>    vb: %X = 16 bytes at p + 4 %vi
//!      %t = mul %x, %y                            %T = vec mul %X, %Y
//!      %s1 = add %s, %t                           %vs1 = (((%vs + T[0]) + T[1]) + T[2]) + T[3]
//!      %i1 = add %i, 1                            br vh
//!      br h                                   h: (as before, entered from vh with
//!                                                 %vi and %vs: the 0-3 iterations left)
//!
//! Every lane computes what its iteration did, so the sums see the same terms
//! in the same order and get the same result, except that a term the loop
//! fused into its sum (`s += a * b`, an Fma) is now rounded before the add:
//! C allows a contraction to be made or not at each place (FP_CONTRACT), and
//! clang's vectorized loops do the same. With -ffp-contract=off there is no
//! Fma and the result is exactly the scalar loop's.
//!
//! The loop is a header with only phis and the exit test `i < n`, and one
//! body block. The counter is an i64 (start and bound within 2^40: a 32-bit
//! value widened, or such a value times at most 256, a byte offset) or, after
//! lftr, a pointer below an end pointer; either way i + 4 steps cannot
//! overflow. Integer address arithmetic is read as linear in the counter; a
//! 32-bit step in it is taken as exact only on a counter that is itself a
//! 32-bit int counted by one (C: signed int arithmetic does not overflow). The body has no stores or calls; its
//! float loads are of addresses that step 4 bytes per iteration (one vector),
//! 8 bytes with the neighbouring float loaded too (both halves of 32 bytes,
//! deinterleaved: cos/sin pairs), or not at all (broadcast). Its float values
//! are lane operations (add, sub, mul, div, neg, fma) of those and of values
//! from outside the loop. Each other phi is a sum, updated by adds or subs of
//! terms (or an Fma into it) used nowhere else, or a pointer or integer
//! stepped by a constant. Integer instructions only compute addresses.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::analysis;
use crate::ir::reexports::{
    BasicBlock, BlockId, Instruction, IntrinsicOp, IrBinOp, IrCmpOp, IrConst, IrFunction, IrModule, IrUnaryOp,
    Operand, Terminator, Value, VecElem, VecOpKind, VecPermute,
};
use super::loop_analysis;

/// Lanes of a 16-byte vector of floats.
const LANES: i64 = 4;

pub(crate) fn vectorize_module(module: &mut IrModule) -> usize {
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    module.functions.iter_mut()
        .filter(|f| !f.is_declaration && f.blocks.len() > 2)
        .map(|f| vectorize_function(f, &mut next_label))
        .sum()
}

fn vectorize_function(func: &mut IrFunction, next_label: &mut u32) -> usize {
    let mut count = 0;
    // Headers of loops rewritten (the scalar loop left for the remainder)
    // or made (the vector loop): never again.
    let mut done: FxHashSet<u32> = FxHashSet::default();
    // One loop at a time: a rewrite adds blocks and moves edges.
    loop {
        let cfg = analysis::CfgAnalysis::build(func);
        let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
        let plan = loop_analysis::merge_loops_by_header(loops).into_iter().find_map(|lp| {
            if lp.body.len() != 2 || done.contains(&func.blocks[lp.header].label.0) {
                return None;
            }
            let pre = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds)?;
            let body = *lp.body.iter().find(|&&b| b != lp.header)?;
            analyze(func, lp.header, body, pre)
        });
        let Some(plan) = plan else { return count };
        done.insert(func.blocks[plan.header].label.0);
        done.insert(*next_label);
        rewrite(func, plan, next_label);
        count += 1;
    }
}

/// A linear form: sum of coef * atom, plus a constant. An atom is a value
/// from outside the loop, the counter phi, or a stepped phi.
#[derive(Clone, PartialEq, Eq, Debug)]
struct Lin {
    terms: Vec<(u32, i64)>,
    k: i64,
}

impl Lin {
    fn atom(v: u32) -> Lin {
        Lin { terms: vec![(v, 1)], k: 0 }
    }
    fn konst(k: i64) -> Lin {
        Lin { terms: Vec::new(), k }
    }
    fn add(mut self, other: &Lin, sign: i64) -> Option<Lin> {
        for &(v, c) in &other.terms {
            match self.terms.iter_mut().find(|t| t.0 == v) {
                Some(t) => t.1 = t.1.checked_add(c.checked_mul(sign)?)?,
                None => self.terms.push((v, c.checked_mul(sign)?)),
            }
        }
        self.terms.retain(|t| t.1 != 0);
        self.terms.sort_unstable();
        self.k = self.k.checked_add(other.k.checked_mul(sign)?)?;
        Some(self)
    }
    fn scale(mut self, m: i64) -> Option<Lin> {
        for t in &mut self.terms {
            t.1 = t.1.checked_mul(m)?;
        }
        self.terms.retain(|t| t.1 != 0);
        self.k = self.k.checked_mul(m)?;
        Some(self)
    }
}

/// How an accumulator is updated by one term: `s + t`, `s - t`, or
/// `s ± a * b` from an Fma (the product a term of its own here).
#[derive(Clone, Copy, Debug)]
enum Update {
    Add(Operand),
    Sub(Operand),
    FmaAdd(Operand, Operand),
    FmaSub(Operand, Operand),
}

struct Sum {
    phi: Value,
    init: Operand,
    /// The updates in order (each reads the previous one's result).
    updates: Vec<Update>,
}

/// A phi stepped by a constant each iteration: pointer (GEP) or integer.
struct Stepped {
    phi: Value,
    ty: IrType,
    init: Operand,
    step: i64,
}

/// A float load of the loop: its lane-0 address, and how it steps.
#[derive(Clone)]
struct FLoad {
    at: usize,
    dest: Value,
    lin: Lin,
    stride: i64,
}

struct Plan {
    header: usize,
    body: usize,
    pre: usize,
    counter: Value,
    /// I64 (a 32-bit value widened) or Ptr, stepped by `counter_step`.
    counter_ty: IrType,
    counter_step: i64,
    counter_init: Operand,
    bound: Operand,
    /// The exit test (Slt, or Ult on a pointer).
    test_op: IrCmpOp,
    sums: Vec<Sum>,
    stepped: Vec<Stepped>,
    loads: Vec<FLoad>,
    /// Float instructions of the body (not loads or sum updates), in order.
    lane_ops: Vec<usize>,
}

fn int_like(ty: IrType) -> bool {
    matches!(ty, IrType::I32 | IrType::I64 | IrType::Ptr)
}

/// Within 2^40 of zero: a constant, a 32-bit value sign-extended, or one
/// of those times at most 256 (a shift or multiply: a byte offset).
fn bounded(func: &IrFunction, op: &Operand) -> bool {
    match op {
        Operand::Const(c) => c.to_i64().is_some_and(|k| k.abs() < 1 << 40),
        Operand::Value(v) => func.blocks.iter().flat_map(|b| &b.instructions).any(|i| match i {
            Instruction::Cast { dest, from_ty: IrType::I32, to_ty: IrType::I64, .. } => dest == v,
            Instruction::BinOp { dest, op: IrBinOp::Shl, lhs, rhs: Operand::Const(k), ty: IrType::I64 } if dest == v =>
                k.to_i64().is_some_and(|k| (0..=8).contains(&k)) && sext32(func, lhs),
            Instruction::BinOp { dest, op: IrBinOp::Mul, lhs, rhs: Operand::Const(k), ty: IrType::I64 } if dest == v =>
                k.to_i64().is_some_and(|k| k.abs() <= 256) && sext32(func, lhs),
            _ => false,
        }),
    }
}

fn sext32(func: &IrFunction, op: &Operand) -> bool {
    matches!(op, Operand::Value(v) if func.blocks.iter().flat_map(|b| &b.instructions).any(|i| matches!(i,
        Instruction::Cast { dest, from_ty: IrType::I32, to_ty: IrType::I64, .. } if dest == v)))
}

fn analyze(func: &IrFunction, header: usize, body: usize, pre: usize) -> Option<Plan> {
    let (h, b) = (&func.blocks[header], &func.blocks[body]);
    let (h_label, b_label, pre_label) = (h.label, b.label, func.blocks[pre].label);
    if !matches!(b.terminator, Terminator::Branch(t) if t == h_label) {
        return None;
    }
    let Terminator::CondBranch { cond: Operand::Value(cond), true_label, false_label } = h.terminator else { return None };
    if true_label != b_label || false_label == b_label || false_label == h_label {
        return None;
    }
    // The header: phis, then the exit test.
    let (test, phis) = h.instructions.split_last()?;
    let Instruction::Cmp { dest, op: test_op @ (IrCmpOp::Slt | IrCmpOp::Ult), lhs: Operand::Value(counter), rhs: bound, ty: IrType::I64 } = test
        else { return None };
    if *dest != cond {
        return None;
    }
    let defined: FxHashSet<u32> = h.instructions.iter().chain(&b.instructions).filter_map(|i| i.dest()).map(|v| v.0).collect();
    let outside = |op: &Operand| !matches!(op, Operand::Value(v) if defined.contains(&v.0));
    if !outside(bound) {
        return None;
    }
    let def = |v: Value| b.instructions.iter().position(|i| i.dest() == Some(v));
    let mut phi_of: Vec<(Value, IrType, Operand, Operand)> = Vec::new();
    for inst in phis {
        let Instruction::Phi { dest, ty, incoming } = inst else { return None };
        if incoming.len() != 2 {
            return None;
        }
        let init = incoming.iter().find(|(_, l)| *l == pre_label)?.0;
        let next = incoming.iter().find(|(_, l)| *l == b_label)?.0;
        if !outside(&init) {
            return None;
        }
        phi_of.push((*dest, *ty, init, next));
    }
    // Uses of each value in the body and the header's test.
    let mut uses: FxHashMap<u32, usize> = FxHashMap::default();
    for inst in b.instructions.iter().chain(std::iter::once(test)) {
        inst.for_each_used_value(|v| *uses.entry(v).or_default() += 1);
    }
    let used = |v: Value| uses.get(&v.0).copied().unwrap_or(0);

    // The counter: a 32-bit integer widened to i64 below a 32-bit bound
    // (signed), or a pointer below an end pointer (lftr's form), so that
    // the counter plus four steps cannot overflow.
    let &(_, cty, counter_init, counter_next) = phi_of.iter().find(|p| p.0 == *counter)?;
    let Operand::Value(cn) = counter_next else { return None };
    let cn_at = def(cn)?;
    let int_ok = cty == IrType::I64 && *test_op == IrCmpOp::Slt && bounded(func, &counter_init) && bounded(func, bound);
    if !(int_ok || cty == IrType::Ptr) {
        return None;
    }
    let is_inc = |inst: &Instruction, of: Value| match inst {
        Instruction::BinOp { op: IrBinOp::Add, lhs: Operand::Value(x), rhs: Operand::Const(c), ty: IrType::I64, .. }
            if *x == of => c.to_i64(),
        Instruction::GetElementPtr { base, offset: Operand::Const(c), .. } if *base == of => c.to_i64(),
        _ => None,
    };
    let counter_step = is_inc(&b.instructions[cn_at], *counter).filter(|&k| (1..=1 << 16).contains(&k))?;
    // Every value of the counter is a 32-bit int (it counts by one from a
    // 32-bit start to a 32-bit bound): truncating it loses nothing.
    let fits32 = |op: &Operand| match op {
        Operand::Const(c) => c.to_i64().is_some_and(|k| i32::try_from(k).is_ok()),
        Operand::Value(_) => sext32(func, op),
    };
    let counter32 = int_ok && counter_step == 1 && fits32(&counter_init) && fits32(bound);

    // The other phis: sums, or stepped values.
    let mut skip: FxHashSet<usize> = [cn_at].into_iter().collect();
    let mut sums = Vec::new();
    let mut stepped = Vec::new();
    for &(phi, ty, init, next) in phi_of.iter().filter(|p| p.0 != *counter) {
        let Operand::Value(next) = next else { return None };
        let at = def(next)?;
        if int_like(ty) && !matches!(ty, IrType::I32) {
            // (Its next value may also be an address: lin_of sees the step.)
            let step = is_inc(&b.instructions[at], phi)?;
            skip.insert(at);
            stepped.push(Stepped { phi, ty, init, step });
            continue;
        }
        if ty != IrType::F32 {
            return None;
        }
        // Walk the chain of updates from the phi to `next`.
        let mut cur = phi;
        let mut updates = Vec::new();
        loop {
            if used(cur) != 1 {
                return None;
            }
            let k = b.instructions.iter().position(|i| i.used_values().contains(&cur.0))?;
            let other = |o: &Operand| !matches!(o, Operand::Value(v) if *v == cur);
            let (dest, update) = match &b.instructions[k] {
                Instruction::BinOp { dest, op: IrBinOp::Add, lhs, rhs, ty: IrType::F32 } => {
                    let t = if other(lhs) { *lhs } else { *rhs };
                    if !other(&t) || other(lhs) == other(rhs) {
                        return None;
                    }
                    (*dest, Update::Add(t))
                }
                Instruction::BinOp { dest, op: IrBinOp::Sub, lhs: Operand::Value(l), rhs, ty: IrType::F32 }
                    if *l == cur && other(rhs) => (*dest, Update::Sub(*rhs)),
                Instruction::Intrinsic { dest: Some(dest), op: IntrinsicOp::Fma { f64: false, neg_product, neg_addend: false }, args, .. }
                    if args.len() == 3 && !other(&args[2]) && other(&args[0]) && other(&args[1]) =>
                    (*dest, if *neg_product { Update::FmaSub(args[0], args[1]) } else { Update::FmaAdd(args[0], args[1]) }),
                _ => return None,
            };
            skip.insert(k);
            updates.push(update);
            if dest == next {
                break;
            }
            cur = dest;
        }
        if used(next) != 0 {
            return None;
        }
        sums.push(Sum { phi, init, updates });
    }
    if sums.is_empty() {
        return None;
    }

    // Linear forms of integer and pointer values.
    let stepped_ids: FxHashSet<u32> = stepped.iter().map(|s| s.phi.0).collect();
    let mut lin_memo: FxHashMap<u32, Option<Lin>> = FxHashMap::default();
    #[allow(clippy::too_many_arguments)]
    fn lin_of(op: &Operand, b: &BasicBlock, defined: &FxHashSet<u32>, counter: Value, int_counter: bool, stepped: &FxHashSet<u32>,
              memo: &mut FxHashMap<u32, Option<Lin>>, addr: &mut FxHashSet<usize>, depth: u32) -> Option<Lin> {
        let v = match op {
            Operand::Const(c) => return Some(Lin::konst(c.to_i64()?)),
            Operand::Value(v) => *v,
        };
        if v == counter || stepped.contains(&v.0) || !defined.contains(&v.0) {
            return Some(Lin::atom(v.0));
        }
        if let Some(l) = memo.get(&v.0) {
            return l.clone();
        }
        if depth > 32 {
            return None;
        }
        let at = b.instructions.iter().position(|i| i.dest() == Some(v))?;
        let mut rec = |o: &Operand, addr: &mut FxHashSet<usize>| lin_of(o, b, defined, counter, int_counter, stepped, memo, addr, depth + 1);
        let l = match &b.instructions[at] {
            Instruction::BinOp { op: op @ (IrBinOp::Add | IrBinOp::Sub), lhs, rhs, ty: IrType::I32 | IrType::I64, .. } => {
                let l = rec(lhs, addr)?;
                let r = rec(rhs, addr)?;
                l.add(&r, if *op == IrBinOp::Add { 1 } else { -1 })
            }
            Instruction::BinOp { op: IrBinOp::Mul, lhs, rhs: Operand::Const(c), ty: IrType::I32 | IrType::I64, .. } =>
                rec(lhs, addr)?.scale(c.to_i64()?),
            Instruction::BinOp { op: IrBinOp::Mul, lhs: Operand::Const(c), rhs, ty: IrType::I32 | IrType::I64, .. } =>
                rec(rhs, addr)?.scale(c.to_i64()?),
            Instruction::BinOp { op: IrBinOp::Shl, lhs, rhs: Operand::Const(c), ty: IrType::I32 | IrType::I64, .. } => {
                let k = c.to_i64().filter(|k| (0..32).contains(k))?;
                rec(lhs, addr)?.scale(1 << k)
            }
            // Signed 32-bit arithmetic does not overflow (C), and the
            // counter is a 32-bit value.
            Instruction::Cast { src, from_ty: IrType::I64, to_ty: IrType::I32, .. }
            | Instruction::Cast { src, from_ty: IrType::I32, to_ty: IrType::I64, .. } => rec(src, addr),
            Instruction::GetElementPtr { base, offset, .. } => {
                let l = rec(&Operand::Value(*base), addr)?;
                let r = rec(offset, addr)?;
                l.add(&r, 1)
            }
            _ => None,
        };
        // A 32-bit value is taken as exact (C: signed int arithmetic does
        // not overflow) only as a small multiple of the 32-bit counter plus
        // a small constant: a wider value truncated could wrap.
        let narrow = matches!(&b.instructions[at], Instruction::BinOp { ty: IrType::I32, .. }
            | Instruction::Cast { to_ty: IrType::I32, .. });
        let l = l.filter(|l| !narrow || l.k.abs() < 1 << 24
            && l.terms.iter().all(|&(t, c)| int_counter && t == counter.0 && c.abs() <= 64));
        if l.is_some() {
            addr.insert(at);
        }
        memo.insert(v.0, l.clone());
        l
    }

    // Pointers from outside the loop at a constant offset from another: the
    // loads of one struct's arrays then share a base (and its register).
    let outside_geps: FxHashMap<u32, (u32, i64)> = func.blocks.iter().enumerate()
        .filter(|(i, _)| *i != header && *i != body)
        .flat_map(|(_, blk)| &blk.instructions)
        .filter_map(|i| match i {
            Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } => Some((dest.0, (base.0, c.to_i64()?))),
            _ => None,
        })
        .collect();

    // Classify the body.
    let step_of =|v: u32| -> i64 {
        if v == counter.0 { counter_step } else { stepped.iter().find(|s| s.phi.0 == v).map_or(0, |s| s.step) }
    };
    let mut addr: FxHashSet<usize> = FxHashSet::default();
    let mut loads = Vec::new();
    let mut lane_ops = Vec::new();
    for (k, inst) in b.instructions.iter().enumerate() {
        if skip.contains(&k) {
            continue;
        }
        match inst {
            Instruction::Load { dest, ptr, ty: IrType::F32, volatile: false, .. } => {
                let lin = lin_of(&Operand::Value(*ptr), b, &defined, *counter, counter32, &stepped_ids, &mut lin_memo, &mut addr, 0)?;
                let lin = flatten(lin, &outside_geps);
                // One pointer atom, plain; the rest integers.
                let stride: i64 = lin.terms.iter().map(|&(v, c)| c * step_of(v)).sum();
                if !matches!(stride, 0 | 4 | 8) || lin.k % 4 != 0 {
                    return None;
                }
                loads.push(FLoad { at: k, dest: *dest, lin, stride });
            }
            Instruction::BinOp { op: IrBinOp::Add | IrBinOp::Sub | IrBinOp::Mul | IrBinOp::SDiv, ty: IrType::F32, .. }
            | Instruction::UnaryOp { op: IrUnaryOp::Neg, ty: IrType::F32, .. }
            | Instruction::Intrinsic { op: IntrinsicOp::Fma { f64: false, .. }, .. } => lane_ops.push(k),
            Instruction::Copy { src: Operand::Value(v), .. } if defined.contains(&v.0) => lane_ops.push(k),
            _ => {}
        }
    }
    // Everything else computes addresses (lin_of walked it), and is used
    // only for that.
    let float_insts: FxHashSet<usize> = loads.iter().map(|l| l.at).chain(lane_ops.iter().copied()).chain(skip.iter().copied()).collect();
    for (k, inst) in b.instructions.iter().enumerate() {
        if float_insts.contains(&k) {
            continue;
        }
        if !addr.contains(&k) {
            return None;
        }
        let d = inst.dest()?;
        // Its users: other address computations, or the loads' pointers.
        for (j, user) in b.instructions.iter().enumerate() {
            if !user.used_values().contains(&d.0) {
                continue;
            }
            let ok = addr.contains(&j) || matches!(user, Instruction::Load { ptr, .. } if *ptr == d && loads.iter().any(|l| l.at == j))
                && !matches!(user, Instruction::Load { dest, .. } if *dest == d);
            if !ok {
                return None;
            }
        }
        if test.used_values().contains(&d.0) {
            return None;
        }
    }
    // Lane values are used only in the body (the sums are the results).
    let lane_defs: FxHashSet<u32> = loads.iter().map(|l| l.dest.0)
        .chain(lane_ops.iter().filter_map(|&k| b.instructions[k].dest()).map(|v| v.0)).collect();
    let used_after = func.blocks.iter().enumerate().filter(|(i, _)| *i != body).any(|(_, blk)| {
        blk.instructions.iter().any(|i| i.used_values().iter().any(|v| lane_defs.contains(v)))
            || blk.terminator.used_values().iter().any(|v| lane_defs.contains(v))
    });
    if used_after {
        return None;
    }
    // A load 8 bytes apart per lane reads 32 bytes; its neighbour must be
    // loaded too, or the vector reads memory the loop never did.
    for l in loads.iter().filter(|l| l.stride == 8) {
        let partner = Lin { terms: l.lin.terms.clone(), k: if l.lin.k.rem_euclid(8) == 0 { l.lin.k + 4 } else { l.lin.k - 4 } };
        if !loads.iter().any(|o| o.stride == 8 && o.lin == partner) {
            return None;
        }
    }
    // Exactly one pointer atom per load address, with coefficient one.
    let ptr_atom = |lin: &Lin| -> Option<u32> {
        let ptrs: Vec<&(u32, i64)> = lin.terms.iter().filter(|(v, _)| value_type(func, *v) == Some(IrType::Ptr)).collect();
        match ptrs[..] {
            [&(v, 1)] => Some(v),
            _ => None,
        }
    };
    for l in &loads {
        ptr_atom(&l.lin)?;
        for &(v, _) in &l.lin.terms {
            if !matches!(value_type(func, v), Some(IrType::I64 | IrType::Ptr)) {
                return None;
            }
        }
    }
    // Worth it only with a vector load.
    if !loads.iter().any(|l| l.stride != 0) {
        return None;
    }
    Some(Plan {
        header, body, pre, counter: *counter, counter_ty: cty, counter_step, counter_init, bound: *bound, test_op: *test_op, sums, stepped, loads, lane_ops,
    })
}

/// `lin` with each pointer atom that is a constant GEP of another value
/// (from outside the loop) replaced by that value plus the offset.
fn flatten(mut lin: Lin, outside_geps: &FxHashMap<u32, (u32, i64)>) -> Lin {
    for _ in 0..8 {
        let Some(i) = lin.terms.iter().position(|(v, c)| *c == 1 && outside_geps.contains_key(v)) else { break };
        let (base, off) = outside_geps[&lin.terms[i].0];
        let Some(k) = lin.k.checked_add(off) else { break };
        lin.terms.remove(i);
        let Some(l) = lin.clone().add(&Lin::atom(base), 1) else { break };
        lin = Lin { k, ..l };
    }
    lin
}

/// The type of a value, from its definition (or a parameter's).
fn value_type(func: &IrFunction, v: u32) -> Option<IrType> {
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if inst.dest().map(|d| d.0) == Some(v) {
            return match inst {
                Instruction::Phi { ty, .. } | Instruction::BinOp { ty, .. } | Instruction::UnaryOp { ty, .. }
                | Instruction::Load { ty, .. } => Some(*ty),
                Instruction::Cast { to_ty, .. } => Some(*to_ty),
                Instruction::GetElementPtr { .. } | Instruction::Alloca { .. } | Instruction::GlobalAddr { .. } => Some(IrType::Ptr),
                Instruction::ParamRef { ty, .. } => Some(*ty),
                Instruction::Copy { .. } => None,
                _ => None,
            };
        }
    }
    None
}

fn rewrite(func: &mut IrFunction, plan: Plan, next_label: &mut u32) {
    let mut next_id = func.next_value_id;
    let mut fresh = || { let v = Value(next_id); next_id += 1; v };
    let vh = BlockId(*next_label);
    let vb = BlockId(*next_label + 1);
    *next_label += 2;
    let h_label = func.blocks[plan.header].label;
    let pre_label = func.blocks[plan.pre].label;
    let span = func.blocks[plan.body].source_spans.first().copied();
    let k64 = |n: i64| Operand::Const(IrConst::I64(n));
    let body = func.blocks[plan.body].instructions.clone();

    // vh: the counter, sums and stepped values of the vector loop.
    let vi = fresh();
    let vi4 = fresh();
    let go = fresh();
    let vsum: Vec<(Value, Value)> = plan.sums.iter().map(|_| (fresh(), fresh())).collect(); // (phi, next)
    let vstep: Vec<(Value, Value)> = plan.stepped.iter().map(|_| (fresh(), fresh())).collect();
    let mut vh_insts = vec![Instruction::Phi { dest: vi, ty: plan.counter_ty, incoming: vec![(plan.counter_init, pre_label), (Operand::Value(vi4), vb)] }];
    for (s, &(p, n)) in plan.sums.iter().zip(&vsum) {
        vh_insts.push(Instruction::Phi { dest: p, ty: IrType::F32, incoming: vec![(s.init, pre_label), (Operand::Value(n), vb)] });
    }
    for (s, &(p, n)) in plan.stepped.iter().zip(&vstep) {
        vh_insts.push(Instruction::Phi { dest: p, ty: s.ty, incoming: vec![(s.init, pre_label), (Operand::Value(n), vb)] });
    }
    // Four more iterations, all below the bound: counter + 4 steps <= bound.
    let four = LANES * plan.counter_step;
    vh_insts.push(if plan.counter_ty == IrType::Ptr {
        Instruction::GetElementPtr { dest: vi4, base: vi, offset: k64(four), ty: IrType::I8 }
    } else {
        Instruction::BinOp { dest: vi4, op: IrBinOp::Add, lhs: Operand::Value(vi), rhs: k64(four), ty: IrType::I64 }
    });
    let le = if plan.test_op == IrCmpOp::Ult { IrCmpOp::Ule } else { IrCmpOp::Sle };
    vh_insts.push(Instruction::Cmp { dest: go, op: le, lhs: Operand::Value(vi4), rhs: plan.bound, ty: IrType::I64 });

    // vb: each atom's value at lane 0 (the counter and stepped phis are the
    // vector loop's), then the loads, lane ops and sums.
    let mut atom_now: FxHashMap<u32, Value> = FxHashMap::default();
    atom_now.insert(plan.counter.0, vi);
    for (s, &(p, _)) in plan.stepped.iter().zip(&vstep) {
        atom_now.insert(s.phi.0, p);
    }
    let mut vb_insts: Vec<Instruction> = Vec::new();
    let mut allocas: Vec<Instruction> = Vec::new();
    // Address of lin + extra, at lane 0. Each scaled term, and each sum of
    // terms (an address before its constant), is computed once.
    let mut term_cache: FxHashMap<(u32, i64), Operand> = FxHashMap::default();
    let mut base_cache: FxHashMap<Vec<(u32, i64)>, Value> = FxHashMap::default();
    let mut emit_addr = |insts: &mut Vec<Instruction>, lin: &Lin, extra: i64, fresh: &mut dyn FnMut() -> Value| -> Value {
        let ptr = lin.terms.iter().find(|(v, _)| value_type(func, *v) == Some(IrType::Ptr)).map(|t| t.0).unwrap();
        let mut at = match base_cache.get(&lin.terms) {
            Some(&a) => a,
            None => {
                let base = atom_now.get(&ptr).copied().unwrap_or(Value(ptr));
                let mut off: Option<Operand> = None;
                for &(v, c) in lin.terms.iter().filter(|t| t.0 != ptr) {
                    let term = *term_cache.entry((v, c)).or_insert_with(|| {
                        let x = Operand::Value(atom_now.get(&v).copied().unwrap_or(Value(v)));
                        if c == 1 {
                            return x;
                        }
                        let t = fresh();
                        insts.push(if c > 0 && c & (c - 1) == 0 {
                            Instruction::BinOp { dest: t, op: IrBinOp::Shl, lhs: x, rhs: k64(c.trailing_zeros() as i64), ty: IrType::I64 }
                        } else {
                            Instruction::BinOp { dest: t, op: IrBinOp::Mul, lhs: x, rhs: k64(c), ty: IrType::I64 }
                        });
                        Operand::Value(t)
                    });
                    off = Some(match off {
                        None => term,
                        Some(o) => {
                            let t = fresh();
                            insts.push(Instruction::BinOp { dest: t, op: IrBinOp::Add, lhs: o, rhs: term, ty: IrType::I64 });
                            Operand::Value(t)
                        }
                    });
                }
                let mut at = base;
                if let Some(o) = off {
                    let t = fresh();
                    insts.push(Instruction::GetElementPtr { dest: t, base: at, offset: o, ty: IrType::I8 });
                    at = t;
                }
                base_cache.insert(lin.terms.clone(), at);
                at
            }
        };
        let k = lin.k + extra;
        if k != 0 {
            let t = fresh();
            insts.push(Instruction::GetElementPtr { dest: t, base: at, offset: k64(k), ty: IrType::I8 });
            at = t;
        }
        at
    };
    let new_vec = |allocas: &mut Vec<Instruction>, fresh: &mut dyn FnMut() -> Value| -> Value {
        let v = fresh();
        allocas.push(Instruction::Alloca { dest: v, ty: IrType::Ptr, size: 16, align: 16, volatile: false });
        v
    };
    // What each scalar of the body is in the vector loop.
    #[derive(Clone, Copy)]
    enum VVal {
        Vec(Value),
        Scalar(Operand),
    }
    let mut vval: FxHashMap<u32, VVal> = FxHashMap::default();
    let lookup = |vval: &FxHashMap<u32, VVal>, op: &Operand| -> VVal {
        match op {
            Operand::Value(v) => vval.get(&v.0).copied().unwrap_or(VVal::Scalar(*op)),
            Operand::Const(_) => VVal::Scalar(*op),
        }
    };
    // Loads: deinterleaved pairs share their two vector loads.
    // (terms, offset of the pair) -> its two vectors.
    type PairKey = (Vec<(u32, i64)>, i64);
    let mut pairs: FxHashMap<PairKey, (Value, Value)> = FxHashMap::default();
    let mut fr = || fresh();
    for l in &plan.loads {
        match l.stride {
            0 => {
                let a = emit_addr(&mut vb_insts, &l.lin, 0, &mut fr);
                let d = fr();
                vb_insts.push(Instruction::Load { dest: d, ptr: a, ty: IrType::F32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
                vval.insert(l.dest.0, VVal::Scalar(Operand::Value(d)));
            }
            4 => {
                let a = emit_addr(&mut vb_insts, &l.lin, 0, &mut fr);
                let t = new_vec(&mut allocas, &mut fr);
                vb_insts.push(Instruction::Memcpy { dest: t, src: a, size: 16 });
                vval.insert(l.dest.0, VVal::Vec(t));
            }
            _ => {
                let low = l.lin.k - l.lin.k.rem_euclid(8);
                let key = (l.lin.terms.clone(), low);
                let (lo, hi) = match pairs.get(&key) {
                    Some(&p) => p,
                    None => {
                        let base = Lin { terms: l.lin.terms.clone(), k: low };
                        let a = emit_addr(&mut vb_insts, &base, 0, &mut fr);
                        let a16 = fr();
                        vb_insts.push(Instruction::GetElementPtr { dest: a16, base: a, offset: k64(16), ty: IrType::I8 });
                        let (lo, hi) = (new_vec(&mut allocas, &mut fr), new_vec(&mut allocas, &mut fr));
                        vb_insts.push(Instruction::Memcpy { dest: lo, src: a, size: 16 });
                        vb_insts.push(Instruction::Memcpy { dest: hi, src: a16, size: 16 });
                        pairs.insert(key, (lo, hi));
                        (lo, hi)
                    }
                };
                let t = new_vec(&mut allocas, &mut fr);
                let perm = if l.lin.k.rem_euclid(8) == 0 { VecPermute::Uzp1 } else { VecPermute::Uzp2 };
                vb_insts.push(Instruction::Intrinsic {
                    dest: None, op: IntrinsicOp::VecOp { op: VecOpKind::Permute(perm), elem: VecElem::U32, bytes: 16, splat: 0 },
                    dest_ptr: Some(t), args: vec![Operand::Value(lo), Operand::Value(hi)],
                });
                vval.insert(l.dest.0, VVal::Vec(t));
            }
        }
    }
    // A vector op of `args` (vectors, or scalars broadcast).
    let vec_op = |vb_insts: &mut Vec<Instruction>, allocas: &mut Vec<Instruction>, fr: &mut dyn FnMut() -> Value,
                      op: VecOpKind, args: &[VVal]| -> Value {
        let t = new_vec(allocas, fr);
        let mut splat = 0u8;
        let ops: Vec<Operand> = args.iter().enumerate().map(|(i, a)| match a {
            VVal::Vec(v) => Operand::Value(*v),
            VVal::Scalar(s) => { splat |= 1 << i; *s }
        }).collect();
        vb_insts.push(Instruction::Intrinsic {
            dest: None, op: IntrinsicOp::VecOp { op, elem: VecElem::F32, bytes: 16, splat }, dest_ptr: Some(t), args: ops,
        });
        t
    };
    for &k in &plan.lane_ops {
        let inst = &body[k];
        let Some(dest) = inst.dest() else { continue };
        let (op, args): (Option<VecOpKind>, Vec<Operand>) = match inst {
            Instruction::BinOp { op, lhs, rhs, .. } => (Some(match op {
                IrBinOp::Add => VecOpKind::Add,
                IrBinOp::Sub => VecOpKind::Sub,
                IrBinOp::Mul => VecOpKind::Mul,
                _ => VecOpKind::Div,
            }), vec![*lhs, *rhs]),
            Instruction::UnaryOp { src, .. } => (Some(VecOpKind::Neg), vec![*src]),
            Instruction::Intrinsic { op: IntrinsicOp::Fma { neg_product, neg_addend, .. }, args, .. } =>
                (Some(VecOpKind::Fma { neg_product: *neg_product, neg_addend: *neg_addend }), args.clone()),
            Instruction::Copy { src, .. } => (None, vec![*src]),
            _ => unreachable!(),
        };
        let vargs: Vec<VVal> = args.iter().map(|a| lookup(&vval, a)).collect();
        let Some(op) = op else {
            vval.insert(dest.0, vargs[0]);
            continue;
        };
        if vargs.iter().all(|a| matches!(a, VVal::Scalar(_))) {
            // The same in every lane: the scalar instruction, on the vector
            // loop's scalars.
            let s: Vec<Operand> = vargs.iter().map(|a| match a { VVal::Scalar(s) => *s, VVal::Vec(_) => unreachable!() }).collect();
            let d = fr();
            vb_insts.push(match inst {
                Instruction::BinOp { op, ty, .. } => Instruction::BinOp { dest: d, op: *op, lhs: s[0], rhs: s[1], ty: *ty },
                Instruction::UnaryOp { op, ty, .. } => Instruction::UnaryOp { dest: d, op: *op, src: s[0], ty: *ty },
                Instruction::Intrinsic { op, .. } => Instruction::Intrinsic { dest: Some(d), op: *op, dest_ptr: None, args: s },
                _ => unreachable!(),
            });
            vval.insert(dest.0, VVal::Scalar(Operand::Value(d)));
            continue;
        }
        let t = vec_op(&mut vb_insts, &mut allocas, &mut fr, op, &vargs);
        vval.insert(dest.0, VVal::Vec(t));
    }
    // The sums, lane by lane, each lane's updates in order.
    let mut lane_cache: FxHashMap<(u32, i64), Value> = FxHashMap::default();
    let mut lane = |vb_insts: &mut Vec<Instruction>, fr: &mut dyn FnMut() -> Value, v: VVal, j: i64| -> Operand {
        match v {
            VVal::Scalar(s) => s,
            VVal::Vec(t) => {
                if let Some(&x) = lane_cache.get(&(t.0, j)) {
                    return Operand::Value(x);
                }
                let at = if j == 0 {
                    t
                } else {
                    let g = fr();
                    vb_insts.push(Instruction::GetElementPtr { dest: g, base: t, offset: k64(4 * j), ty: IrType::I8 });
                    g
                };
                let x = fr();
                vb_insts.push(Instruction::Load { dest: x, ptr: at, ty: IrType::F32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
                lane_cache.insert((t.0, j), x);
                Operand::Value(x)
            }
        }
    };
    // Products of Fma updates, as vectors (rounded: see the module comment).
    let mut products: Vec<Vec<Option<VVal>>> = Vec::new();
    for s in &plan.sums {
        let mut row = Vec::new();
        for u in &s.updates {
            row.push(match u {
                Update::FmaAdd(a, b) | Update::FmaSub(a, b) => {
                    let (va, vb_) = (lookup(&vval, a), lookup(&vval, b));
                    Some(match (va, vb_) {
                        (VVal::Scalar(x), VVal::Scalar(y)) => {
                            let d = fr();
                            vb_insts.push(Instruction::BinOp { dest: d, op: IrBinOp::Mul, lhs: x, rhs: y, ty: IrType::F32 });
                            VVal::Scalar(Operand::Value(d))
                        }
                        _ => VVal::Vec(vec_op(&mut vb_insts, &mut allocas, &mut fr, VecOpKind::Mul, &[va, vb_])),
                    })
                }
                _ => None,
            });
        }
        products.push(row);
    }
    for ((s, &(p, n)), row) in plan.sums.iter().zip(&vsum).zip(&products) {
        let mut acc = Operand::Value(p);
        let steps = s.updates.len() as i64 * LANES;
        let mut done = 0;
        for j in 0..LANES {
            for (u, prod) in s.updates.iter().zip(row) {
                let (sub, term) = match u {
                    Update::Add(t) => (false, lookup(&vval, t)),
                    Update::Sub(t) => (true, lookup(&vval, t)),
                    Update::FmaAdd(..) => (false, prod.unwrap()),
                    Update::FmaSub(..) => (true, prod.unwrap()),
                };
                let x = lane(&mut vb_insts, &mut fr, term, j);
                done += 1;
                let d = if done == steps { n } else { fr() };
                vb_insts.push(Instruction::BinOp { dest: d, op: if sub { IrBinOp::Sub } else { IrBinOp::Add }, lhs: acc, rhs: x, ty: IrType::F32 });
                acc = Operand::Value(d);
            }
        }
    }
    for (s, &(p, n)) in plan.stepped.iter().zip(&vstep) {
        let step = s.step * LANES;
        vb_insts.push(match s.ty {
            IrType::Ptr => Instruction::GetElementPtr { dest: n, base: p, offset: k64(step), ty: IrType::I8 },
            ty => Instruction::BinOp { dest: n, op: IrBinOp::Add, lhs: Operand::Value(p), rhs: k64(step), ty },
        });
    }
    func.next_value_id = next_id;

    // The scalar loop starts from the vector loop's state.
    let mut from_vh: FxHashMap<u32, Value> = FxHashMap::default();
    from_vh.insert(plan.counter.0, vi);
    for (s, &(p, _)) in plan.sums.iter().zip(&vsum) {
        from_vh.insert(s.phi.0, p);
    }
    for (s, &(p, _)) in plan.stepped.iter().zip(&vstep) {
        from_vh.insert(s.phi.0, p);
    }
    for inst in &mut func.blocks[plan.header].instructions {
        if let Instruction::Phi { dest, incoming, .. } = inst {
            for (op, l) in incoming.iter_mut() {
                if *l == pre_label {
                    *l = vh;
                    *op = Operand::Value(from_vh[&dest.0]);
                }
            }
        }
    }
    match &mut func.blocks[plan.pre].terminator {
        Terminator::Branch(t) => *t = vh,
        Terminator::CondBranch { true_label, false_label, .. } => {
            for t in [true_label, false_label] {
                if *t == h_label {
                    *t = vh;
                }
            }
        }
        _ => unreachable!("a preheader branches to its loop"),
    }
    let spans = |n: usize| span.map(|s| vec![s; n]).unwrap_or_default();
    let nalloc = allocas.len();
    // After the entry block's own allocas: the backend takes the n-th one
    // as parameter n's (find_param_alloca), so one put first was param 0.
    let entry = &mut func.blocks[0];
    let at = entry.instructions.iter().take_while(|i| matches!(i, Instruction::Alloca { .. })).count();
    entry.instructions.splice(at..at, allocas);
    if !entry.source_spans.is_empty() {
        let s = entry.source_spans[at.min(entry.source_spans.len() - 1)];
        entry.source_spans.splice(at..at, std::iter::repeat_n(s, nalloc));
    }
    let (nh, nb) = (vh_insts.len(), vb_insts.len());
    func.blocks.push(BasicBlock {
        label: vh, instructions: vh_insts, source_spans: spans(nh),
        terminator: Terminator::CondBranch { cond: Operand::Value(go), true_label: vb, false_label: h_label },
    });
    func.blocks.push(BasicBlock {
        label: vb, instructions: vb_insts, source_spans: spans(nb),
        terminator: Terminator::Branch(vh),
    });
}


#[cfg(test)]
mod tests {
    use super::*;

    fn load(dest: u32, ptr: u32) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::F32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    /// `s = s0; for (p = a; p < end; p++) s += *p * k` (lftr's pointer
    /// counter), the product fused into the sum when `fma`; `store` adds a
    /// store to the body.
    fn sum_loop(fma: bool, store: bool) -> IrFunction {
        let mut func = IrFunction::new("t".to_string(), IrType::F32, vec![], false);
        let v = |n| Operand::Value(Value(n));
        let block = |l, instructions: Vec<Instruction>, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        // Value 0: a, 1: end, 2: k, 3: s0, 4: destination of the store.
        let mut body = vec![load(20, 11)];
        if fma {
            body.push(Instruction::Intrinsic { dest: Some(Value(22)), op: IntrinsicOp::Fma { f64: false, neg_product: false, neg_addend: false },
                dest_ptr: None, args: vec![v(20), v(2), v(10)] });
        } else {
            body.push(Instruction::BinOp { dest: Value(21), op: IrBinOp::Mul, lhs: v(20), rhs: v(2), ty: IrType::F32 });
            body.push(Instruction::BinOp { dest: Value(22), op: IrBinOp::Add, lhs: v(10), rhs: v(21), ty: IrType::F32 });
        }
        if store {
            body.push(Instruction::Store { val: v(20), ptr: Value(4), ty: IrType::F32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
        }
        body.push(Instruction::GetElementPtr { dest: Value(23), base: Value(11), offset: Operand::Const(IrConst::I64(4)), ty: IrType::I8 });
        func.blocks = vec![
            block(0, vec![
                Instruction::ParamRef { dest: Value(0), param_idx: 0, ty: IrType::Ptr },
                Instruction::GetElementPtr { dest: Value(1), base: Value(0), offset: Operand::Const(IrConst::I64(400)), ty: IrType::I8 },
            ], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(10), ty: IrType::F32, incoming: vec![(v(3), BlockId(0)), (v(22), BlockId(2))] },
                Instruction::Phi { dest: Value(11), ty: IrType::Ptr, incoming: vec![(v(0), BlockId(0)), (v(23), BlockId(2))] },
                Instruction::Cmp { dest: Value(12), op: IrCmpOp::Slt, lhs: v(11), rhs: v(1), ty: IrType::I64 },
            ], Terminator::CondBranch { cond: v(12), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, body, Terminator::Branch(BlockId(1))),
            block(3, vec![], Terminator::Return(Some(v(10)))),
        ];
        func.next_value_id = 30;
        func
    }

    /// The float adds of the vector body, as (sum operand, lane loaded).
    fn adds(func: &IrFunction) -> Vec<Instruction> {
        func.blocks[5].instructions.iter().filter(|i| matches!(i, Instruction::BinOp { op: IrBinOp::Add, ty: IrType::F32, .. })).cloned().collect()
    }

    #[test]
    fn sum_adds_lanes_in_order() {
        let mut func = sum_loop(false, false);
        assert_eq!(vectorize_function(&mut func, &mut 4), 1);
        assert_eq!(func.blocks.len(), 6);
        // The vector header: four more iterations below the end pointer.
        let vh = &func.blocks[4].instructions;
        assert!(vh.iter().any(|i| matches!(i, Instruction::GetElementPtr { offset: Operand::Const(IrConst::I64(16)), .. })), "{vh:?}");
        assert!(vh.iter().any(|i| matches!(i, Instruction::Cmp { op: IrCmpOp::Sle, .. })), "{vh:?}");
        // One 16-byte load, one vector multiply by k broadcast, four adds
        // each taking the previous one's result.
        let vb = &func.blocks[5].instructions;
        assert_eq!(vb.iter().filter(|i| matches!(i, Instruction::Memcpy { size: 16, .. })).count(), 1);
        assert!(vb.iter().any(|i| matches!(i, Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: VecOpKind::Mul, elem: VecElem::F32, splat: 2, .. }, .. })));
        let adds = adds(&func);
        assert_eq!(adds.len(), 4);
        let Instruction::Phi { dest: vs, .. } = func.blocks[4].instructions[1] else { panic!() };
        let mut acc = vs;
        for a in &adds {
            let Instruction::BinOp { dest, lhs: Operand::Value(l), .. } = a else { panic!("{a:?}") };
            assert_eq!(*l, acc);
            acc = *dest;
        }
        // The scalar loop starts from the vector loop's state.
        let Instruction::Phi { incoming, .. } = &func.blocks[1].instructions[0] else { panic!() };
        assert!(incoming.iter().any(|(o, l)| matches!(o, Operand::Value(x) if *x == vs) && *l == BlockId(4)), "{incoming:?}");
    }

    #[test]
    fn fused_sum_rounds_the_product_first() {
        let mut func = sum_loop(true, false);
        assert_eq!(vectorize_function(&mut func, &mut 4), 1);
        let vb = &func.blocks[5].instructions;
        assert!(vb.iter().any(|i| matches!(i, Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: VecOpKind::Mul, .. }, .. })));
        assert!(!vb.iter().any(|i| matches!(i, Instruction::Intrinsic { op: IntrinsicOp::Fma { .. } | IntrinsicOp::VecOp { op: VecOpKind::Fma { .. }, .. }, .. })));
        assert_eq!(adds(&func).len(), 4);
    }

    #[test]
    fn loop_with_a_store_kept() {
        let mut func = sum_loop(false, true);
        assert_eq!(vectorize_function(&mut func, &mut 4), 0);
        assert_eq!(func.blocks.len(), 4);
    }
}

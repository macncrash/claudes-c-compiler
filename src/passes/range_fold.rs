//! Comparisons decided by value ranges.
//!
//! A value's range comes from its definition (constants, casts, arithmetic
//! that provably does not wrap, masks, remainders, shifts, selects and phis)
//! narrowed by the branch conditions that dominate the comparison: in
//!
//!   if (var <= 0 || (unsigned)(var - 1) < UINT_MAX) ...
//!
//! the second test runs only on the false edge of `var <= 0`, so there
//! `var` is in [1, INT_MAX], `var - 1` in [0, INT_MAX - 1], and the
//! comparison is always true. A comparison whose outcome the two operand
//! ranges decide becomes a constant; constant folding and CFG
//! simplification then remove the dead branch.
//!
//! Ranges are intervals of i128 in the interpretation of a value's own IR
//! type (its width and signedness). Only integer types up to 64 bits take
//! part. A range is reinterpreted at another width or signedness only where
//! that reads no bits above the value's own type and every value in it means
//! the same number in both, so nothing depends on how a backend keeps the
//! unused high bits of a register. Signed overflow is never assumed away:
//! arithmetic whose exact result interval leaves the type gives no range.

use crate::common::fx_hash::FxHashMap;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrBinOp, IrCmpOp, IrConst, IrFunction, Operand, Terminator, Value};

/// Recursion limit when computing a value's range from its definition.
const MAX_DEPTH: u32 = 6;
/// Limit on dominating blocks searched for branch conditions.
const MAX_DOM_STEPS: usize = 64;

pub(crate) fn range_fold_function(func: &mut IrFunction) -> usize {
    let folds = {
        let mut ranges = Ranges::new(func);
        let mut folds = Vec::new();
        for (b, block) in func.blocks.iter().enumerate() {
            if ranges.cfg.idom[b] == usize::MAX {
                continue;
            }
            for (i, inst) in block.instructions.iter().enumerate() {
                if let Instruction::Cmp { op, lhs, rhs, ty, .. } = inst {
                    if matches!((lhs, rhs), (Operand::Const(_), Operand::Const(_))) {
                        continue;
                    }
                    if let Some(result) = ranges.decide(*op, lhs, rhs, *ty, b) {
                        folds.push((b, i, result));
                    }
                }
            }
        }
        folds
    };
    for &(b, i, result) in &folds {
        let Instruction::Cmp { dest, .. } = func.blocks[b].instructions[i] else { unreachable!() };
        func.blocks[b].instructions[i] = Instruction::Copy { dest, src: Operand::Const(IrConst::I32(result as i32)) };
    }
    folds.len()
}

/// An integer interpretation: width in bits and signedness.
#[derive(Clone, Copy, PartialEq)]
struct Interp {
    w: u32,
    signed: bool,
}

impl Interp {
    fn of(ty: IrType) -> Option<Interp> {
        let (w, signed) = match ty {
            IrType::I8 => (8, true),
            IrType::I16 => (16, true),
            IrType::I32 => (32, true),
            IrType::I64 => (64, true),
            IrType::U8 => (8, false),
            IrType::U16 => (16, false),
            IrType::U32 => (32, false),
            IrType::U64 => (64, false),
            _ => return None,
        };
        Some(Interp { w, signed })
    }

    fn with_sign(self, signed: bool) -> Interp {
        Interp { w: self.w, signed }
    }

    fn min(self) -> i128 {
        if self.signed { -(1i128 << (self.w - 1)) } else { 0 }
    }

    fn max(self) -> i128 {
        if self.signed { (1i128 << (self.w - 1)) - 1 } else { (1i128 << self.w) - 1 }
    }

    fn full(self) -> R {
        R { lo: self.min(), hi: self.max() }
    }

    fn holds(self, r: R) -> bool {
        self.min() <= r.lo && r.hi <= self.max()
    }

    /// The number the low `w` bits of `x` stand for.
    fn wrap(self, x: i128) -> i128 {
        let m = 1i128 << self.w;
        let x = x.rem_euclid(m);
        if self.signed && x > self.max() { x - m } else { x }
    }
}

/// A closed interval, nonempty when built by `R::new`.
#[derive(Clone, Copy, PartialEq, Debug)]
struct R {
    lo: i128,
    hi: i128,
}

impl R {
    fn point(x: i128) -> R {
        R { lo: x, hi: x }
    }

    fn new(lo: i128, hi: i128) -> Option<R> {
        (lo <= hi).then_some(R { lo, hi })
    }

    fn meet(self, o: R) -> Option<R> {
        R::new(self.lo.max(o.lo), self.hi.min(o.hi))
    }

    fn join(self, o: R) -> R {
        R { lo: self.lo.min(o.lo), hi: self.hi.max(o.hi) }
    }

    /// The interval without `x`, where that leaves an interval.
    fn without(self, x: i128) -> Option<R> {
        if self.lo == x {
            R::new(x + 1, self.hi)
        } else if self.hi == x {
            R::new(self.lo, x - 1)
        } else {
            Some(self)
        }
    }

    /// `r`, the numbers of interpretation `from`, read as `to`: only when
    /// that reads no bits above `from` and every number means the same.
    fn view(self, from: Interp, to: Interp) -> Option<R> {
        (to.w <= from.w && to.holds(self)).then_some(self)
    }

    /// The numbers of `to` whose bits, read as `from` (same width), are in
    /// the interval: the parts on either side of the sign boundary, joined.
    fn reread(self, from: Interp, to: Interp) -> Option<R> {
        if from == to {
            return Some(self);
        }
        let m = 1i128 << from.w;
        let shared = R { lo: 0, hi: from.with_sign(true).max() };
        let mut out = self.meet(shared);
        let rest = if from.signed { R::new(self.lo.max(from.min()), self.hi.min(-1)) }
                   else { R::new(self.lo.max(shared.hi + 1), self.hi.min(from.max())) };
        if let Some(rest) = rest {
            let moved = if from.signed { R { lo: rest.lo + m, hi: rest.hi + m } }
                        else { R { lo: rest.lo - m, hi: rest.hi - m } };
            out = Some(out.map_or(moved, |o| o.join(moved)));
        }
        out
    }
}

/// Which interpretation an integer comparison reads its operands in.
fn cmp_interp(op: IrCmpOp, ty: IrType) -> Option<Interp> {
    let it = Interp::of(ty)?;
    Some(match op {
        IrCmpOp::Eq | IrCmpOp::Ne => it,
        IrCmpOp::Slt | IrCmpOp::Sle | IrCmpOp::Sgt | IrCmpOp::Sge => it.with_sign(true),
        IrCmpOp::Ult | IrCmpOp::Ule | IrCmpOp::Ugt | IrCmpOp::Uge => it.with_sign(false),
    })
}

/// `a op b` with the operands swapped: `b op' a`.
fn swapped(op: IrCmpOp) -> IrCmpOp {
    match op {
        IrCmpOp::Slt => IrCmpOp::Sgt,
        IrCmpOp::Sle => IrCmpOp::Sge,
        IrCmpOp::Sgt => IrCmpOp::Slt,
        IrCmpOp::Sge => IrCmpOp::Sle,
        IrCmpOp::Ult => IrCmpOp::Ugt,
        IrCmpOp::Ule => IrCmpOp::Uge,
        IrCmpOp::Ugt => IrCmpOp::Ult,
        IrCmpOp::Uge => IrCmpOp::Ule,
        op => op,
    }
}

/// `!(a op b)` as `a op' b`.
fn negated(op: IrCmpOp) -> IrCmpOp {
    match op {
        IrCmpOp::Eq => IrCmpOp::Ne,
        IrCmpOp::Ne => IrCmpOp::Eq,
        IrCmpOp::Slt => IrCmpOp::Sge,
        IrCmpOp::Sle => IrCmpOp::Sgt,
        IrCmpOp::Sgt => IrCmpOp::Sle,
        IrCmpOp::Sge => IrCmpOp::Slt,
        IrCmpOp::Ult => IrCmpOp::Uge,
        IrCmpOp::Ule => IrCmpOp::Ugt,
        IrCmpOp::Ugt => IrCmpOp::Ule,
        IrCmpOp::Uge => IrCmpOp::Ult,
    }
}

/// Narrow `r` (in `it`) by `x op k` holding for every x in it.
fn constrain(r: R, op: IrCmpOp, k: i128) -> Option<R> {
    match op {
        IrCmpOp::Eq => r.meet(R::point(k)),
        IrCmpOp::Ne => r.without(k),
        IrCmpOp::Slt | IrCmpOp::Ult => R::new(r.lo, r.hi.min(k - 1)),
        IrCmpOp::Sle | IrCmpOp::Ule => R::new(r.lo, r.hi.min(k)),
        IrCmpOp::Sgt | IrCmpOp::Ugt => R::new(r.lo.max(k + 1), r.hi),
        IrCmpOp::Sge | IrCmpOp::Uge => R::new(r.lo.max(k), r.hi),
    }
}

/// `a op b` for every a in `a` and b in `b`, when that is one answer.
fn compare(op: IrCmpOp, a: R, b: R) -> Option<bool> {
    let lt = |a: R, b: R| if a.hi < b.lo { Some(true) } else if a.lo >= b.hi { Some(false) } else { None };
    match op {
        IrCmpOp::Eq | IrCmpOp::Ne => {
            let eq = if a.hi < b.lo || b.hi < a.lo { Some(false) }
                     else if a.lo == a.hi && b.lo == b.hi { Some(true) }
                     else { None };
            eq.map(|e| e == (op == IrCmpOp::Eq))
        }
        IrCmpOp::Slt | IrCmpOp::Ult => lt(a, b),
        IrCmpOp::Sgt | IrCmpOp::Ugt => lt(b, a),
        IrCmpOp::Sge | IrCmpOp::Uge => lt(a, b).map(|x| !x),
        IrCmpOp::Sle | IrCmpOp::Ule => lt(b, a).map(|x| !x),
    }
}

fn const_interp(c: &IrConst) -> Option<(i128, Interp)> {
    let (x, ty) = match *c {
        IrConst::I8(x) => (x as i128, IrType::I8),
        IrConst::I16(x) => (x as i128, IrType::I16),
        IrConst::I32(x) => (x as i128, IrType::I32),
        IrConst::I64(x) => (x as i128, IrType::I64),
        _ => return None,
    };
    Some((x, Interp::of(ty)?))
}

struct Ranges<'a> {
    func: &'a IrFunction,
    cfg: analysis::CfgAnalysis,
    labels: FxHashMap<crate::ir::reexports::BlockId, usize>,
    /// Each value's defining block and instruction index.
    defs: FxHashMap<u32, (usize, usize)>,
    /// Ranges found, with the depth they were computed to.
    memo: FxHashMap<(u32, usize), (u32, R)>,
}

impl<'a> Ranges<'a> {
    fn new(func: &'a IrFunction) -> Self {
        let mut defs = FxHashMap::default();
        for (b, block) in func.blocks.iter().enumerate() {
            for (i, inst) in block.instructions.iter().enumerate() {
                if let Some(d) = inst.dest() {
                    defs.insert(d.0, (b, i));
                }
            }
        }
        Ranges {
            func,
            cfg: analysis::CfgAnalysis::build(func),
            labels: analysis::build_label_map(func),
            defs,
            memo: FxHashMap::default(),
        }
    }

    fn def(&self, v: Value) -> Option<&'a Instruction> {
        let &(b, i) = self.defs.get(&v.0)?;
        Some(&self.func.blocks[b].instructions[i])
    }

    /// The interpretation of a value's own type.
    fn interp(&self, v: Value, depth: u32) -> Option<Interp> {
        match self.def(v)? {
            Instruction::Copy { src: Operand::Value(s), .. } if depth > 0 => self.interp(*s, depth - 1),
            Instruction::Copy { src: Operand::Const(c), .. } => Some(const_interp(c)?.1),
            Instruction::Cmp { .. } => Interp::of(IrType::I8),
            inst => Interp::of(inst.result_type()?),
        }
    }

    /// Decide `lhs op rhs` in block `b`.
    fn decide(&mut self, op: IrCmpOp, lhs: &Operand, rhs: &Operand, ty: IrType, b: usize) -> Option<bool> {
        let it = cmp_interp(op, ty)?;
        let a = self.operand(lhs, b, it, MAX_DEPTH);
        let c = self.operand(rhs, b, it, MAX_DEPTH);
        if let (Some(a), Some(c)) = (a, c) {
            if let Some(x) = compare(op, a, c) {
                return Some(x);
            }
        }
        // `v == k` under a dominating `v != k`, which no interval holds.
        if !matches!(op, IrCmpOp::Eq | IrCmpOp::Ne) {
            return None;
        }
        let (v, k) = match (lhs, rhs) {
            (Operand::Value(v), Operand::Const(k)) | (Operand::Const(k), Operand::Value(v)) => (*v, k),
            _ => return None,
        };
        let k = it.wrap(const_interp(k)?.0);
        self.dominating_facts(v, b).into_iter()
            .any(|(fop, fit, fk)| fop == IrCmpOp::Ne && fit.w == it.w && it.wrap(fk) == k)
            .then_some(op == IrCmpOp::Ne)
    }

    /// An operand's range in block `b`, read in `it`.
    fn operand(&mut self, op: &Operand, b: usize, it: Interp, depth: u32) -> Option<R> {
        match op {
            Operand::Const(c) => {
                let (x, _) = const_interp(c)?;
                Some(R::point(it.wrap(x)))
            }
            Operand::Value(v) => {
                let own = self.interp(*v, MAX_DEPTH)?;
                self.value(*v, b, depth)?.view(own, it)
            }
        }
    }

    /// A value's range in block `b`, in its own interpretation.
    fn value(&mut self, v: Value, b: usize, depth: u32) -> Option<R> {
        if let Some(&(d, r)) = self.memo.get(&(v.0, b)) {
            if d >= depth {
                return Some(r);
            }
        }
        let own = self.interp(v, MAX_DEPTH)?;
        let mut r = if depth == 0 { own.full() } else { self.defined(v, b, own, depth - 1).unwrap_or(own.full()) };
        r = self.conditions(v, b, own, r)?;
        self.memo.insert((v.0, b), (depth, r));
        Some(r)
    }

    /// Narrow `r` by the branch conditions on `v` that dominate block `b`.
    fn conditions(&self, v: Value, b: usize, own: Interp, mut r: R) -> Option<R> {
        // Facts read at another width say nothing about `v`.
        for (op, it, k) in self.dominating_facts(v, b).into_iter().filter(|f| f.1.w == own.w) {
            r = if op == IrCmpOp::Ne {
                r.without(own.wrap(k))?
            } else {
                r.meet(constrain(it.full(), op, k)?.reread(it, own)?)?
            };
        }
        Some(r)
    }

    /// Facts `v op k` (with `v`'s low bits and `k` read in the interpretation)
    /// from the edges into the blocks dominating `b` that have one predecessor.
    fn dominating_facts(&self, v: Value, b: usize) -> Vec<(IrCmpOp, Interp, i128)> {
        let mut facts = Vec::new();
        let mut cur = b;
        for _ in 0..MAX_DOM_STEPS {
            let up = self.cfg.idom[cur];
            if up == usize::MAX || up == cur {
                break;
            }
            if let &[p] = self.cfg.preds.row(cur) {
                if let Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label }
                    = &self.func.blocks[p as usize].terminator {
                    if true_label != false_label {
                        let taken = self.labels.get(true_label) == Some(&cur);
                        self.facts_of(*c, taken, v, 4, &mut facts);
                    }
                }
            }
            cur = up;
        }
        facts
    }

    /// The facts about `v` in `c != 0` (`truth`) or `c == 0`: a comparison
    /// of `v` with a constant, or several under `&&` (true) or `||` (false).
    fn facts_of(&self, c: Value, truth: bool, v: Value, depth: u32, out: &mut Vec<(IrCmpOp, Interp, i128)>) {
        let Some(inst) = self.def(c) else { return };
        let is_const = |o: &Operand, nonzero: bool| matches!(o, Operand::Const(k) if k.is_nonzero() == nonzero
                                                                                  && const_interp(k).is_some());
        let (a, b) = match inst {
            Instruction::Cmp { op, lhs, rhs, ty, .. } => {
                let (op, other) = match (lhs, rhs) {
                    (Operand::Value(l), o) if self.same_bits(*l, v) => (*op, o),
                    (o, Operand::Value(r)) if self.same_bits(*r, v) => (swapped(*op), o),
                    _ => return,
                };
                let (Operand::Const(k), Some(it)) = (other, cmp_interp(op, *ty)) else { return };
                let Some((k, _)) = const_interp(k) else { return };
                out.push((if truth { op } else { negated(op) }, it, it.wrap(k)));
                return;
            }
            // a && b, a || b.
            Instruction::Select { cond, true_val, false_val, .. }
                if (truth && is_const(false_val, false)) || (!truth && is_const(true_val, true)) =>
                (cond, if truth { true_val } else { false_val }),
            Instruction::BinOp { op: IrBinOp::And, lhs, rhs, .. } if truth => (lhs, rhs),
            Instruction::BinOp { op: IrBinOp::Or, lhs, rhs, .. } if !truth => (lhs, rhs),
            _ => return,
        };
        if depth > 0 {
            for o in [a, b] {
                if let Operand::Value(x) = o {
                    self.facts_of(*x, truth, v, depth - 1, out);
                }
            }
        }
    }

    /// Whether `x` has the bits of `v`: `v` itself, a copy, or a cast
    /// between types of one width.
    fn same_bits(&self, x: Value, v: Value) -> bool {
        x == v || match self.def(x) {
            Some(Instruction::Copy { src: Operand::Value(s), .. }) => *s == v,
            Some(Instruction::Cast { src: Operand::Value(s), from_ty, to_ty, .. }) => *s == v
                && matches!((Interp::of(*from_ty), Interp::of(*to_ty), self.interp(v, MAX_DEPTH)),
                            (Some(f), Some(t), Some(o)) if f.w == t.w && t.w == o.w),
            _ => false,
        }
    }

    /// The range a value's definition gives it, in `own`.
    fn defined(&mut self, v: Value, b: usize, own: Interp, d: u32) -> Option<R> {
        let inst = self.def(v)?;
        match inst {
            Instruction::Copy { src, .. } => self.operand(src, b, own, d),
            Instruction::Cmp { .. } => Some(R { lo: 0, hi: 1 }),
            Instruction::Cast { src, from_ty, to_ty, .. } => {
                let (from, to) = (Interp::of(*from_ty)?, Interp::of(*to_ty)?);
                if to.w <= from.w {
                    // Truncation keeps the low bits.
                    self.operand(src, b, to, d)
                } else {
                    // Extension keeps the number the source's bits stand for.
                    self.operand(src, b, from, d)?.view(Interp { w: to.w, signed: from.signed }, to)
                }
            }
            Instruction::BinOp { op, lhs, rhs, ty, .. } => {
                let t = Interp::of(*ty)?;
                self.binop(*op, lhs, rhs, t, b, d)
            }
            Instruction::Select { true_val, false_val, .. } => {
                let x = self.operand(true_val, b, own, d)?;
                let y = self.operand(false_val, b, own, d)?;
                Some(x.join(y))
            }
            Instruction::Phi { incoming, .. } => {
                let mut out: Option<R> = None;
                for (op, label) in incoming {
                    let p = *self.labels.get(label)?;
                    let x = self.operand(op, p, own, d)?;
                    out = Some(out.map_or(x, |o| o.join(x)));
                }
                out
            }
            _ => None,
        }
    }

    fn binop(&mut self, op: IrBinOp, lhs: &Operand, rhs: &Operand, t: Interp, b: usize, d: u32) -> Option<R> {
        let u = t.with_sign(false);
        let exact = |r: R| t.holds(r).then_some(r);
        match op {
            IrBinOp::Add | IrBinOp::Sub | IrBinOp::Mul => {
                let x = self.operand(lhs, b, t, d)?;
                let y = self.operand(rhs, b, t, d)?;
                exact(match op {
                    IrBinOp::Add => R { lo: x.lo + y.lo, hi: x.hi + y.hi },
                    IrBinOp::Sub => R { lo: x.lo - y.hi, hi: x.hi - y.lo },
                    _ => {
                        let p = [x.lo.checked_mul(y.lo)?, x.lo.checked_mul(y.hi)?,
                                 x.hi.checked_mul(y.lo)?, x.hi.checked_mul(y.hi)?];
                        R { lo: *p.iter().min()?, hi: *p.iter().max()? }
                    }
                })
            }
            IrBinOp::And => {
                let x = self.operand(lhs, b, t, d);
                let y = self.operand(rhs, b, t, d);
                let nonneg = |r: Option<R>| r.filter(|r| r.lo >= 0).map(|r| r.hi);
                match (nonneg(x), nonneg(y)) {
                    (Some(a), Some(c)) => Some(R { lo: 0, hi: a.min(c) }),
                    (Some(a), None) | (None, Some(a)) => Some(R { lo: 0, hi: a }),
                    (None, None) => None,
                }
            }
            IrBinOp::Or | IrBinOp::Xor => {
                let x = self.operand(lhs, b, t, d)?;
                let y = self.operand(rhs, b, t, d)?;
                if x.lo < 0 || y.lo < 0 {
                    return None;
                }
                let hi = x.hi.max(y.hi);
                let bits = 128 - hi.leading_zeros();
                exact(R { lo: 0, hi: (1i128 << bits) - 1 })
            }
            IrBinOp::URem | IrBinOp::UDiv => {
                let y = self.operand(rhs, b, u, d)?;
                if y.lo <= 0 {
                    return None;
                }
                let x = self.operand(lhs, b, u, d).unwrap_or(u.full());
                exact(if op == IrBinOp::URem { R { lo: 0, hi: x.hi.min(y.hi - 1) } }
                      else { R { lo: x.lo / y.hi, hi: x.hi / y.lo } })
            }
            IrBinOp::SRem | IrBinOp::SDiv if t.signed => {
                let y = self.operand(rhs, b, t, d)?;
                if y.lo <= 0 {
                    return None;
                }
                let x = self.operand(lhs, b, t, d).unwrap_or(t.full());
                if op == IrBinOp::SRem {
                    let m = y.hi - 1;
                    Some(if x.lo >= 0 { R { lo: 0, hi: x.hi.min(m) } } else { R { lo: (-m).max(x.lo), hi: m.min(x.hi.max(0)) } })
                } else {
                    let q = [x.lo / y.lo, x.lo / y.hi, x.hi / y.lo, x.hi / y.hi];
                    exact(R { lo: *q.iter().min()?, hi: *q.iter().max()? })
                }
            }
            IrBinOp::LShr | IrBinOp::AShr | IrBinOp::Shl => {
                let k = self.operand(rhs, b, u, d)?;
                if k.lo != k.hi || k.lo >= t.w as i128 {
                    return None;
                }
                let k = k.lo as u32;
                match op {
                    IrBinOp::LShr => {
                        let x = self.operand(lhs, b, u, d).unwrap_or(u.full());
                        exact(R { lo: x.lo >> k, hi: x.hi >> k })
                    }
                    IrBinOp::AShr if t.signed => {
                        let x = self.operand(lhs, b, t, d).unwrap_or(t.full());
                        Some(R { lo: x.lo >> k, hi: x.hi >> k })
                    }
                    IrBinOp::Shl => {
                        let x = self.operand(lhs, b, t, d)?;
                        if x.lo < 0 {
                            return None;
                        }
                        exact(R { lo: x.lo << k, hi: x.hi << k })
                    }
                    _ => None,
                }
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const I32: Interp = Interp { w: 32, signed: true };
    const U32: Interp = Interp { w: 32, signed: false };
    const I64: Interp = Interp { w: 64, signed: true };

    #[test]
    fn wrap_reads_low_bits() {
        assert_eq!(I32.wrap(0xffff_ffff), -1);
        assert_eq!(U32.wrap(-1), 0xffff_ffff);
        assert_eq!(I32.wrap(1 << 32), 0);
    }

    #[test]
    fn reread_across_sign_boundary() {
        // Signed [0, INT_MAX] is unsigned [0, INT_MAX].
        assert_eq!(R { lo: 0, hi: I32.max() }.reread(I32, U32), Some(R { lo: 0, hi: 0x7fff_ffff }));
        // Signed [INT_MIN, -1] is unsigned [2^31, UINT_MAX].
        assert_eq!(R { lo: I32.min(), hi: -1 }.reread(I32, U32), Some(R { lo: 0x8000_0000, hi: 0xffff_ffff }));
        // Unsigned [11, UINT_MAX] covers both halves of signed.
        assert_eq!(R { lo: 11, hi: U32.max() }.reread(U32, I32), Some(I32.full()));
    }

    #[test]
    fn view_needs_every_number_to_mean_the_same() {
        assert_eq!(R { lo: 0, hi: 10 }.view(I64, U32), Some(R { lo: 0, hi: 10 }));
        assert_eq!(R { lo: -1, hi: 10 }.view(I64, U32), None);
        // No reading bits above the value's own type.
        assert_eq!(R { lo: 0, hi: 10 }.view(I32, I64), None);
    }

    #[test]
    fn compare_decides_only_separated_ranges() {
        let a = R { lo: 0, hi: 0x7fff_fffe };
        assert_eq!(compare(IrCmpOp::Ult, a, R::point(0xffff_ffff)), Some(true));
        assert_eq!(compare(IrCmpOp::Ult, a, R::point(0x7fff_fffe)), None);
        assert_eq!(compare(IrCmpOp::Uge, a, R::point(0x7fff_ffff)), Some(false));
        assert_eq!(compare(IrCmpOp::Eq, R::point(3), R::point(3)), Some(true));
        assert_eq!(compare(IrCmpOp::Ne, R { lo: 1, hi: 4 }, R::point(5)), Some(true));
    }
}

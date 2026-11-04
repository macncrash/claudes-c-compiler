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
//! comparison is always true. A comparison of two values is also decided by
//! a dominating comparison of the same two (`x != y` under `x == y`). A
//! comparison so decided becomes a constant; constant folding and CFG
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
    let threaded = thread_decided_tests(func);
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
                        folds.push((b, i, Fold::Decided(result)));
                    } else if let Some(op) = ranges.zero_test(*op, lhs, rhs, *ty, b) {
                        folds.push((b, i, Fold::ZeroTest(op)));
                    }
                }
            }
        }
        folds
    };
    for &(b, i, fold) in &folds {
        let inst = &mut func.blocks[b].instructions[i];
        let Instruction::Cmp { dest, op, lhs, rhs, .. } = inst else { unreachable!() };
        match fold {
            Fold::Decided(result) => {
                *inst = Instruction::Copy { dest: *dest, src: Operand::Const(IrConst::I32(result as i32)) };
            }
            Fold::ZeroTest(new_op) => {
                if matches!(lhs, Operand::Const(_)) {
                    std::mem::swap(lhs, rhs);
                }
                *op = new_op;
            }
        }
    }
    folds.len() + threaded
}

/// Whether each `(operand, block)` is never negative there, read at `ty`
/// signed (div_by_const's signed divisions of values a dominating test
/// bounds below, vdbeRecordCompareString's `(serial_type-12)/2` under
/// `serial_type>=12`).
pub(crate) fn never_negative(func: &IrFunction, at: &[(Operand, usize, IrType)]) -> Vec<bool> {
    if at.is_empty() {
        return Vec::new();
    }
    let mut ranges = Ranges::new(func);
    at.iter().map(|(op, b, ty)| {
        let Some(it) = Interp::of(*ty).map(|it| it.with_sign(true)) else { return false };
        if ranges.cfg.idom[*b] == usize::MAX {
            return false;
        }
        if let Some(r) = ranges.operand(op, *b, it, MAX_DEPTH) {
            return r.lo >= 0;
        }
        // A narrower signed value in a wider division, which reads it
        // sign-extended (the convention div_by_const's `_in_i64`
        // expansions rest on): never negative at its own width.
        let Operand::Value(v) = op else { return false };
        matches!(ranges.interp(*v, MAX_DEPTH), Some(own) if own.signed && own.w < it.w)
            && ranges.value(*v, *b, MAX_DEPTH).is_some_and(|r| r.lo >= 0)
    }).collect()
}

/// What range_fold_function does to a comparison.
#[derive(Clone, Copy)]
enum Fold {
    /// Always this result.
    Decided(bool),
    /// A signed `x > 0` or `x <= 0` of an x that is never negative, as the
    /// test for zero (`x != 0` or `x == 0`) it is: `cbnz`/`cbz` on AArch64
    /// instead of `cmp` and `b.gt`/`b.le` (SQLite's `pRoot->nCell > 0` of
    /// a u16 field).
    ZeroTest(IrCmpOp),
}

/// A block that only tests one of its phis against a constant, entered from
/// a predecessor whose own branch on the incoming value decides the test:
/// that edge goes straight to the successor the test picks, and the block
/// keeps its other predecessors.
///
///   P:  c1 = x >= 262; condbr c1, B, F       P:  c1 = x >= 262; condbr c1, S, F
///   B:  p = phi [x, P], [y, Q]          =>   B:  p = phi [y, Q]
///       c2 = p >= 3; condbr c2, S, T             c2 = p >= 3; condbr c2, S, T
///
/// deflate_fast tests s->lookahead >= MIN_MATCH after the MIN_LOOKAHEAD test
/// that skips fill_window, where it merges with the value fill_window left.
/// A constant incoming value decides the test on its own (deflate_fast's
/// hash_head = NIL when too few bytes are left, then hash_head != NIL).
/// The phis may be read only by the test and by the successors' phis, which
/// get the value from the predecessor.
pub(crate) fn thread_decided_tests(func: &mut IrFunction) -> usize {
    // The facts below go stale once edges move: start over after each block.
    let mut total = 0;
    for _ in 0..64 {
        let n = thread_once(func);
        if n == 0 {
            break;
        }
        total += n;
    }
    total
}

fn thread_once(func: &mut IrFunction) -> usize {
    let cfg = analysis::CfgAnalysis::build(func);
    let labels: FxHashMap<crate::ir::reexports::BlockId, usize> =
        func.blocks.iter().enumerate().map(|(i, b)| (b.label, i)).collect();
    let mut cmps: FxHashMap<u32, (IrCmpOp, Operand, Operand, IrType)> = FxHashMap::default();
    // Where each value is read: a phi reads its incoming value at the end
    // of the predecessor it comes from.
    let mut uses: FxHashMap<u32, Vec<usize>> = FxHashMap::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Instruction::Cmp { dest, op, lhs, rhs, ty } = inst {
                cmps.insert(dest.0, (*op, *lhs, *rhs, *ty));
            }
            if let Instruction::Phi { incoming, .. } = inst {
                for (o, l) in incoming {
                    if let (Operand::Value(v), Some(&pb)) = (o, labels.get(l)) {
                        uses.entry(v.0).or_default().push(pb);
                    }
                }
                continue;
            }
            inst.for_each_used_value(|v| uses.entry(v).or_default().push(b));
        }
        block.terminator.for_each_used_value(|v| uses.entry(v).or_default().push(b));
    }
    // The test x op k as (x, op, k in its interpretation, interpretation).
    let test = |op: IrCmpOp, lhs: &Operand, rhs: &Operand, ty: IrType| -> Option<(Value, IrCmpOp, i128, Interp)> {
        let (x, op, k) = match (lhs, rhs) {
            (Operand::Value(x), Operand::Const(k)) => (*x, op, k),
            (Operand::Const(k), Operand::Value(x)) => (*x, swapped(op), k),
            _ => return None,
        };
        let it = cmp_interp(op, ty)?;
        Some((x, op, it.wrap(const_interp(k)?.0), it))
    };
    let mut changes = 0;
    for b in 0..func.blocks.len() {
        let block = &func.blocks[b];
        let Terminator::CondBranch { cond: Operand::Value(c), true_label: bt, false_label: bf } = block.terminator else { continue };
        let nphis = block.instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
        let [Instruction::Cmp { dest, op, lhs, rhs, ty }] = &block.instructions[nphis..] else { continue };
        if *dest != c || uses.get(&c.0).map_or(0, |u| u.len()) != 1 || bt == bf {
            continue;
        }
        let Some((p, op2, k2, it2)) = test(*op, lhs, rhs, *ty) else { continue };
        let Some(Instruction::Phi { incoming, .. }) = block.instructions[..nphis].iter().find(|i| i.dest() == Some(p)) else { continue };
        let phi_dests: Vec<Value> = block.instructions[..nphis].iter().filter_map(|i| i.dest()).collect();
        let blabel = block.label;
        // Predecessor edges that decide the test.
        let mut moves: Vec<(usize, crate::ir::reexports::BlockId)> = Vec::new();
        for (x, pl) in incoming {
            let Some(&pi) = labels.get(pl) else { continue };
            // A constant decides the test whatever the edge.
            if let Operand::Const(k) = x {
                let Some((k, _)) = const_interp(k) else { continue };
                let Some(result) = compare(op2, R::point(it2.wrap(k)), R::point(k2)) else { continue };
                let to = if result { bt } else { bf };
                let edges_to_b = match func.blocks[pi].terminator {
                    Terminator::Branch(l) => l == blabel,
                    Terminator::CondBranch { true_label, false_label, .. } =>
                        (true_label == blabel) != (false_label == blabel) && true_label != to && false_label != to,
                    _ => false,
                };
                if edges_to_b && to != blabel {
                    moves.push((pi, to));
                }
                continue;
            }
            let Operand::Value(x) = x else { continue };
            let Terminator::CondBranch { cond: Operand::Value(pc), true_label, false_label } = func.blocks[pi].terminator else { continue };
            if (true_label == blabel) == (false_label == blabel) {
                continue;
            }
            let Some(&(op1, ref l1, ref r1, ty1)) = cmps.get(&pc.0) else { continue };
            let Some((x1, op1, k1, it1)) = test(op1, l1, r1, ty1) else { continue };
            if x1 != *x || it1 != it2 {
                continue;
            }
            let op1 = if true_label == blabel { op1 } else { negated(op1) };
            let Some(r) = constrain(it1.full(), op1, k1) else { continue };
            let Some(result) = compare(op2, r, R::point(k2)) else { continue };
            let to = if result { bt } else { bf };
            // The predecessor's other edge must not already go there.
            let other = if true_label == blabel { false_label } else { true_label };
            if to != blabel && other != to {
                moves.push((pi, to));
            }
        }
        // Once to has another predecessor, the block dominates only what its
        // other successor dominates, if the block is that one's only
        // predecessor: the phis may be read only in the block and there (a
        // phi reads its value at the end of the predecessor it comes from).
        let to_ok = |to: crate::ir::reexports::BlockId| {
            let other = labels[&if to == bt { bf } else { bt }];
            let sole = cfg.preds.row(other) == [b as u32];
            let under_other = |mut u: usize| loop {
                if u == other {
                    return sole;
                }
                if u == b || cfg.idom[u] == u || cfg.idom[u] == usize::MAX {
                    return false;
                }
                u = cfg.idom[u];
            };
            phi_dests.iter().all(|d| uses.get(&d.0).is_none_or(|u| u.iter().all(|&ub| ub == b || under_other(ub))))
        };
        moves.retain(|&(_, to)| to_ok(to));
        for (pi, to) in moves {
            let pl = func.blocks[pi].label;
            // The phis' values on the edge from P.
            let from_p: FxHashMap<u32, Operand> = func.blocks[b].instructions[..nphis].iter().filter_map(|i| match i {
                Instruction::Phi { dest, incoming, .. } => incoming.iter().find(|(_, l)| *l == pl).map(|(o, _)| (dest.0, *o)),
                _ => None,
            }).collect();
            if from_p.len() != nphis {
                continue;
            }
            match &mut func.blocks[pi].terminator {
                Terminator::CondBranch { true_label, false_label, .. } => {
                    if *true_label == blabel { *true_label = to; } else { *false_label = to; }
                }
                Terminator::Branch(l) => *l = to,
                _ => {}
            }
            for inst in &mut func.blocks[b].instructions[..nphis] {
                if let Instruction::Phi { incoming, .. } = inst {
                    incoming.retain(|(_, l)| *l != pl);
                }
            }
            let ti = labels[&to];
            for inst in &mut func.blocks[ti].instructions {
                if let Instruction::Phi { incoming, .. } = inst {
                    if let Some(&(o, _)) = incoming.iter().find(|(_, l)| *l == blabel) {
                        let o = match o {
                            Operand::Value(v) => from_p.get(&v.0).copied().unwrap_or(o),
                            k => k,
                        };
                        incoming.push((o, pl));
                    }
                }
            }
            changes += 1;
        }
        if changes > 0 {
            return changes;
        }
    }
    changes
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

/// `a q b` given `a k b`, when that decides it: each comparison as the set
/// of orderings (less, equal, greater) it accepts. Equality is the same in
/// either signedness; signed and unsigned orderings say nothing of each other.
fn implied(k: IrCmpOp, q: IrCmpOp) -> Option<bool> {
    let set = |op: IrCmpOp| match op {
        IrCmpOp::Eq => (0b010, None),
        IrCmpOp::Ne => (0b101, None),
        IrCmpOp::Slt => (0b100, Some(true)),
        IrCmpOp::Sle => (0b110, Some(true)),
        IrCmpOp::Sgt => (0b001, Some(true)),
        IrCmpOp::Sge => (0b011, Some(true)),
        IrCmpOp::Ult => (0b100, Some(false)),
        IrCmpOp::Ule => (0b110, Some(false)),
        IrCmpOp::Ugt => (0b001, Some(false)),
        IrCmpOp::Uge => (0b011, Some(false)),
    };
    let ((ks, ksign), (qs, qsign)) = (set(k), set(q));
    if matches!((ksign, qsign), (Some(a), Some(b)) if a != b) {
        return None;
    }
    if ks & !qs == 0 {
        Some(true)
    } else if ks & qs == 0 {
        Some(false)
    } else {
        None
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
        if let (Operand::Value(x), Operand::Value(y)) = (lhs, rhs) {
            if let Some(r) = self.related(op, *x, *y, ty, b) {
                return Some(r);
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

    /// Eq or Ne for a signed `x > 0` or `x <= 0` (either way round) in block
    /// `b` whose x is never negative there.
    fn zero_test(&mut self, op: IrCmpOp, lhs: &Operand, rhs: &Operand, ty: IrType, b: usize) -> Option<IrCmpOp> {
        let (x, op) = match (lhs, rhs) {
            (x @ Operand::Value(_), Operand::Const(k)) if k.is_zero() => (x, op),
            (Operand::Const(k), x @ Operand::Value(_)) if k.is_zero() => (x, swapped(op)),
            _ => return None,
        };
        let new_op = match op {
            IrCmpOp::Sgt => IrCmpOp::Ne,
            IrCmpOp::Sle => IrCmpOp::Eq,
            _ => return None,
        };
        let r = self.operand(x, b, cmp_interp(op, ty)?, MAX_DEPTH)?;
        (r.lo >= 0).then_some(new_op)
    }

    /// `x op y` decided by a dominating branch on a comparison of the same
    /// two values: `x != y` under `x == y`, `y < x` under `x < y`.
    fn related(&self, op: IrCmpOp, x: Value, y: Value, ty: IrType, b: usize) -> Option<bool> {
        let mut cur = b;
        for _ in 0..MAX_DOM_STEPS {
            let up = self.cfg.idom[cur];
            if up == usize::MAX || up == cur {
                break;
            }
            if let &[p] = self.cfg.preds.row(cur) {
                if let Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label }
                    = &self.func.blocks[p as usize].terminator {
                    let known = match self.def(*c) {
                        Some(Instruction::Cmp { op: k, lhs: Operand::Value(l), rhs: Operand::Value(r), ty: kt, .. })
                            if true_label != false_label && *kt == ty => {
                            if (*l, *r) == (x, y) { Some(*k) } else if (*l, *r) == (y, x) { Some(swapped(*k)) } else { None }
                        }
                        _ => None,
                    };
                    if let Some(k) = known {
                        let k = if self.labels.get(true_label) == Some(&cur) { k } else { negated(k) };
                        if let Some(r) = implied(k, op) {
                            return Some(r);
                        }
                    }
                }
            }
            cur = up;
        }
        None
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
                let r = self.value(*v, b, depth)?;
                r.view(own, it).or_else(|| (self.widens_exactly(*v, own, it) && it.holds(r)).then_some(r))
            }
        }
    }

    /// Whether `v`, read at the wider `it`, is the number it is at its own
    /// `own`: a loaded value, which the load extends to the whole register
    /// by its type's signedness (`ldrsb x`, `ldrh w`), read signed or from
    /// an unsigned type.
    fn widens_exactly(&self, v: Value, own: Interp, it: Interp) -> bool {
        it.w > own.w && (it.signed || !own.signed)
            && matches!(self.def(v), Some(Instruction::Load { ty, .. }) if ty.is_integer() && ty.size() < 8)
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
        // Facts read at another width say nothing about `v`, except at a
        // width a load made it exactly (vdbeRecordCompareString's
        // `serial_type<12` of a loaded signed char).
        for (op, it, k) in self.dominating_facts(v, b) {
            if it.w == own.w {
                r = if op == IrCmpOp::Ne {
                    r.without(own.wrap(k))?
                } else {
                    r.meet(constrain(it.full(), op, k)?.reread(it, own)?)?
                };
            } else if self.widens_exactly(v, own, it) {
                // The fact's numbers are v's own (those outside own's
                // range never occur).
                r = if op == IrCmpOp::Ne {
                    if own.holds(R::point(k)) { r.without(k)? } else { r }
                } else {
                    r.meet(constrain(it.full(), op, k)?)?
                };
            }
        }
        Some(r)
    }

    /// `x`'s range (in its own interpretation) where block `p` branches to
    /// block `to`: at the end of `p`, narrowed by `p`'s branch condition on
    /// that edge. A phi's incoming value is only taken along its edge:
    /// substrFunc's `for( ; p1>0; p1--)`, rotated, enters the loop's phi on
    /// `p1 > 0` from the guard and `p1 - 1 > 0` from the latch, so the phi
    /// is positive, its decrement never negative, and the latch's test a
    /// test for zero.
    fn on_edge(&mut self, x: Value, p: usize, to: usize, d: u32) -> Option<R> {
        let own = self.interp(x, MAX_DEPTH)?;
        let mut r = self.value(x, p, d)?;
        if let Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label } = &self.func.blocks[p].terminator {
            let (t, f) = (self.labels.get(true_label).copied(), self.labels.get(false_label).copied());
            if t != f && (t == Some(to) || f == Some(to)) {
                let mut facts = Vec::new();
                self.facts_of(*c, t == Some(to), x, 4, &mut facts);
                for (op, it, k) in facts.into_iter().filter(|f| f.1.w == own.w) {
                    r = if op == IrCmpOp::Ne {
                        r.without(own.wrap(k))?
                    } else {
                        r.meet(constrain(it.full(), op, k)?.reread(it, own)?)?
                    };
                }
            }
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
                let (op, other, x) = match (lhs, rhs) {
                    (Operand::Value(l), o) if self.same_bits(*l, v) || self.widened_from(*l, v) => (*op, o, *l),
                    (o, Operand::Value(r)) if self.same_bits(*r, v) || self.widened_from(*r, v) => (swapped(*op), o, *r),
                    _ => return,
                };
                let (Operand::Const(k), Some(it)) = (other, cmp_interp(op, *ty)) else { return };
                let Some((k, _)) = const_interp(k) else { return };
                let op = if truth { op } else { negated(op) };
                if x != v && !self.same_bits(x, v) {
                    // A widening that keeps v's number, compared at the
                    // width it reaches: a fact on v's own number when the
                    // compare's reading keeps it too and k is one of v's.
                    let (Some(own), Some(xt), k) = (self.interp(v, MAX_DEPTH), self.interp(x, MAX_DEPTH), it.wrap(k)) else { return };
                    if xt.w == it.w && (it.signed || !own.signed) && own.holds(R::point(k)) {
                        out.push((op, own, k));
                    }
                    return;
                }
                out.push((op, it, it.wrap(k)));
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

    /// Whether `x` is `v` extended to a wider type by `v`'s own signedness
    /// (a cast from `v`'s type), which keeps its number.
    fn widened_from(&self, x: Value, v: Value) -> bool {
        match self.def(x) {
            Some(Instruction::Cast { src: Operand::Value(s), from_ty, to_ty, .. }) if *s == v =>
                matches!((Interp::of(*from_ty), Interp::of(*to_ty), self.interp(v, MAX_DEPTH)),
                         (Some(f), Some(t), Some(o)) if f == o && t.w > f.w),
            _ => false,
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
                    let x = match op {
                        Operand::Value(x) => {
                            let to = self.defs.get(&v.0)?.0;
                            self.on_edge(*x, p, to, d)?.view(self.interp(*x, MAX_DEPTH)?, own)?
                        }
                        _ => self.operand(op, p, own, d)?,
                    };
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

    use crate::ir::reexports::{BasicBlock, BlockId};

    /// b0: c = x >= 262 ? b1 : b4;  b4: y = 7 -> b1;
    /// b1: p = phi [x, b0], [y, b4]; t = p >= 3 ? b2 : b3;
    /// b2 and b3 go on to b5, all returning; block reader returns p.
    fn lookahead(reader: u32) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let k = |n| Operand::Const(IrConst::I32(n));
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let ret = |l: u32| Terminator::Return(Some(if reader == l { v(3) } else { k(l as i32) }));
        let end = |l: u32| if reader == 5 { Terminator::Branch(BlockId(5)) } else { ret(l) };
        let mut func = IrFunction::new("t".to_string(), IrType::U32, vec![], false);
        func.blocks = vec![
            block(0, vec![Instruction::Cmp { dest: Value(2), op: IrCmpOp::Uge, lhs: v(1), rhs: k(262), ty: IrType::U32 }],
                  Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(4) }),
            block(1, vec![
                Instruction::Phi { dest: Value(3), ty: IrType::U32, incoming: vec![(v(1), BlockId(0)), (v(5), BlockId(4))] },
                Instruction::Cmp { dest: Value(4), op: IrCmpOp::Uge, lhs: v(3), rhs: k(3), ty: IrType::U32 },
            ], Terminator::CondBranch { cond: v(4), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, vec![], end(2)),
            block(3, vec![], end(3)),
            block(4, vec![Instruction::Copy { dest: Value(5), src: k(7) }], Terminator::Branch(BlockId(1))),
            block(5, vec![], ret(5)),
        ];
        func.next_value_id = 6;
        func
    }

    #[test]
    fn edge_whose_branch_decides_the_test_skips_the_merge() {
        // x >= 262 on b0's edge decides p >= 3: b0 may go straight to b2
        // when p is read only where b1 still dominates, in b3 (b1 its only
        // predecessor).
        let mut func = lookahead(3);
        assert_eq!(thread_decided_tests(&mut func), 1);
        assert!(matches!(func.blocks[0].terminator, Terminator::CondBranch { true_label: BlockId(2), .. }));
        let Instruction::Phi { incoming, .. } = &func.blocks[1].instructions[0] else { panic!() };
        assert_eq!(incoming.len(), 1);
        // Not when b2, reached from b0 then, reads p, nor b5 after both.
        for reader in [2, 5] {
            let mut func = lookahead(reader);
            assert_eq!(thread_decided_tests(&mut func), 0, "p read in b{}", reader);
        }
    }

    /// bi_reverse's count in gen_codes: b0: len = (int)*(u16 *)q; skip the
    /// loop when `len == 0` (if `guarded`); b1: p = phi [len, b0], [n, b1];
    /// n = p - 1; `n > 0` loops back.
    fn countdown(guarded: bool) -> IrFunction {
        use crate::common::types::{AddressSpace, AliasClass};
        let v = |n| Operand::Value(Value(n));
        let k = |n| Operand::Const(IrConst::I32(n));
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let entry = if guarded {
            Terminator::CondBranch { cond: v(2), true_label: BlockId(3), false_label: BlockId(1) }
        } else {
            Terminator::Branch(BlockId(1))
        };
        let mut func = IrFunction::new("t".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![
            block(0, vec![
                Instruction::Load { dest: Value(0), ptr: Value(9), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cast { dest: Value(1), src: v(0), from_ty: IrType::U16, to_ty: IrType::I32 },
                Instruction::Cmp { dest: Value(2), op: IrCmpOp::Eq, lhs: v(1), rhs: k(0), ty: IrType::I32 },
            ], entry),
            block(1, vec![
                Instruction::Phi { dest: Value(3), ty: IrType::I32, incoming: vec![(v(1), BlockId(0)), (v(4), BlockId(1))] },
                Instruction::BinOp { dest: Value(4), op: IrBinOp::Sub, lhs: v(3), rhs: k(1), ty: IrType::I32 },
                Instruction::Cmp { dest: Value(5), op: IrCmpOp::Sgt, lhs: v(4), rhs: k(0), ty: IrType::I32 },
            ], Terminator::CondBranch { cond: v(5), true_label: BlockId(1), false_label: BlockId(2) }),
            block(2, vec![], Terminator::Return(None)),
            block(3, vec![], Terminator::Return(None)),
        ];
        func.next_value_id = 10;
        func
    }

    #[test]
    fn phi_value_narrowed_by_the_branch_on_its_edge() {
        // len != 0 on b0's edge and n > 0 on b1's: p >= 1, n >= 0, and the
        // loop's test is a test for zero.
        let op = |func: &IrFunction| match func.blocks[1].instructions[2] {
            Instruction::Cmp { op, .. } => op,
            _ => panic!(),
        };
        let mut func = countdown(true);
        range_fold_function(&mut func);
        assert_eq!(op(&func), IrCmpOp::Ne);
        // Without the guard len may be 0, and n then -1.
        let mut func = countdown(false);
        range_fold_function(&mut func);
        assert_eq!(op(&func), IrCmpOp::Sgt);
    }

    /// b0: x = load i8; c = (int)x <`signed` 12 (or `unsigned`); condbr c, b1, b2;
    /// b2: y = (long)x - 12; t = y < 0 -- and `guarded` false leaves out b0's test.
    fn serial_type(signed: bool, guarded: bool) -> IrFunction {
        use crate::common::types::{AddressSpace, AliasClass};
        let v = |n| Operand::Value(Value(n));
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let op = if signed { IrCmpOp::Slt } else { IrCmpOp::Ult };
        let entry = if guarded {
            Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(2) }
        } else {
            Terminator::Branch(BlockId(2))
        };
        let mut func = IrFunction::new("t".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![
            block(0, vec![
                Instruction::Load { dest: Value(0), ptr: Value(9), ty: IrType::I8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::Cast { dest: Value(1), src: v(0), from_ty: IrType::I8, to_ty: IrType::I32 },
                Instruction::Cmp { dest: Value(2), op, lhs: v(1), rhs: Operand::Const(IrConst::I32(12)), ty: IrType::I32 },
            ], entry),
            block(1, vec![], Terminator::Return(Some(Operand::Const(IrConst::I32(-1))))),
            block(2, vec![
                Instruction::Cast { dest: Value(3), src: v(0), from_ty: IrType::I8, to_ty: IrType::I64 },
                Instruction::BinOp { dest: Value(4), op: IrBinOp::Sub, lhs: v(3), rhs: Operand::Const(IrConst::I64(12)), ty: IrType::I64 },
                Instruction::Cmp { dest: Value(5), op: IrCmpOp::Slt, lhs: v(4), rhs: Operand::Const(IrConst::I64(0)), ty: IrType::I64 },
            ], Terminator::Return(Some(v(5)))),
        ];
        func.next_value_id = 10;
        func
    }

    #[test]
    fn fact_on_a_widened_value_holds_for_the_value() {
        // (int)x >= 12 on b2's edge: x >= 12, so x - 12 is never negative.
        let mut func = serial_type(true, true);
        range_fold_function(&mut func);
        assert!(matches!(func.blocks[2].instructions[2], Instruction::Copy { src: Operand::Const(IrConst::I32(0)), .. }));
        assert_eq!(never_negative(&serial_type(true, true), &[(Operand::Value(Value(4)), 2, IrType::I64)]), vec![true]);
        // An unsigned reading of the widened signed char says nothing of
        // its number (-1 is 0xffffffff), and without the test x may be -128.
        for (signed, guarded) in [(false, true), (true, false)] {
            let mut func = serial_type(signed, guarded);
            range_fold_function(&mut func);
            assert!(matches!(func.blocks[2].instructions[2], Instruction::Cmp { .. }), "{signed} {guarded}");
            assert_eq!(never_negative(&serial_type(signed, guarded), &[(Operand::Value(Value(4)), 2, IrType::I64)]), vec![false]);
        }
    }

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

    #[test]
    fn implied_by_same_operands() {
        use IrCmpOp::*;
        assert_eq!(implied(Eq, Ne), Some(false));
        assert_eq!(implied(Eq, Sle), Some(true));
        assert_eq!(implied(Eq, Ult), Some(false));
        assert_eq!(implied(Slt, Sgt), Some(false));
        assert_eq!(implied(Slt, Ne), Some(true));
        assert_eq!(implied(Ugt, Eq), Some(false));
        assert_eq!(implied(Sle, Slt), None);
        assert_eq!(implied(Ne, Slt), None);
        // x <s y says nothing of x <u y (x = -1, y = 0).
        assert_eq!(implied(Slt, Ult), None);
        assert_eq!(implied(Slt, Ugt), None);
    }
}

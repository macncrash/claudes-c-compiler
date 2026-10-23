//! Cancel terms between the offsets of a GEP chain.
//!
//!   %q = (x / 6)                              %q = (x / 6)
//!   %r = sub %x, (mul %q, 6)                  (still computed for other uses)
//!   %a = gep %p, (mul %q, 48)             =>  %o = shl %x, 3
//!   %b = gep %a, (shl %r, 3)                  %b = gep %p, %o
//!
//! `p->pieces[pc / 6][pc % 6]` with 8-byte elements in rows of 6 is the
//! element at `8 * pc`, but the address is built from the quotient and the
//! remainder, whose division expansion leaves `pc % 6` as `pc - (pc / 6) * 6`.
//! Summed as a linear combination of values (through adds, subtracts, and
//! multiplies and left shifts by constants, all on I64, where they are exact
//! modulo 2^64), the chain's offsets are `48 q + 8 (x - 6 q) = 8 x`. When
//! the sum has a single term left (or none), the GEP is rebuilt from the
//! chain's root as `gep root, x * k` plus a constant, and the quotient
//! arithmetic it no longer needs dies. Casts, loads and everything else are
//! opaque values. Runs once, late; DCE follows.

use crate::common::fx_hash::FxHashMap;
use crate::common::source::Span;
use crate::common::types::IrType;
use crate::ir::reexports::{Instruction, IrBinOp, IrConst, IrFunction, Operand, Value};

/// Most nested operations expanded under one offset.
const MAX_DEPTH: usize = 6;
/// Most distinct values in a linear combination.
const MAX_TERMS: usize = 8;
/// Most GEPs in a chain.
const MAX_CHAIN: usize = 4;

/// A linear combination: value -> coefficient, plus a constant.
#[derive(Default, Clone)]
struct Lin {
    terms: Vec<(u32, i64)>,
    konst: i64,
}

impl Lin {
    fn add(&mut self, other: &Lin, scale: i64) {
        self.konst = self.konst.wrapping_add(other.konst.wrapping_mul(scale));
        for &(v, c) in &other.terms {
            let c = c.wrapping_mul(scale);
            match self.terms.iter_mut().find(|(w, _)| *w == v) {
                Some(t) => t.1 = t.1.wrapping_add(c),
                None => self.terms.push((v, c)),
            }
        }
        self.terms.retain(|&(_, c)| c != 0);
    }
}

pub(crate) fn cancel_function(func: &mut IrFunction) -> usize {
    let mut def: FxHashMap<u32, Instruction> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let (Some(d), Instruction::BinOp { .. } | Instruction::GetElementPtr { .. }) = (inst.dest(), inst) {
            def.insert(d.0, inst.clone());
        }
    }
    let mut rewrites: Vec<(usize, usize, Vec<Instruction>)> = Vec::new();
    let mut next_id = func.next_value_id;
    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            let Instruction::GetElementPtr { dest, base, offset, ty } = inst else { continue };
            // The chain's offsets, outermost first, and its root.
            let mut offsets = vec![*offset];
            let mut root = *base;
            while let Some(Instruction::GetElementPtr { base, offset, .. }) = def.get(&root.0) {
                if offsets.len() >= MAX_CHAIN {
                    break;
                }
                offsets.push(*offset);
                root = *base;
            }
            let mut sum = Lin::default();
            let mut whole = true;
            for op in &offsets {
                match linear(&def, op, 0) {
                    Some(l) => sum.add(&l, 1),
                    None => whole = false,
                }
            }
            // Worth it only when something cancelled: fewer values remain
            // than the offsets read directly, and at most one.
            let direct = offsets.iter().filter(|o| matches!(o, Operand::Value(_))).count();
            if !whole || sum.terms.len() > 1 || sum.terms.len() >= direct {
                continue;
            }
            let mut out = Vec::new();
            let mut fresh = || { let v = Value(next_id); next_id += 1; v };
            let mut cur = root;
            if let Some(&(v, c)) = sum.terms.first() {
                let scaled = if c == 1 {
                    Operand::Value(Value(v))
                } else {
                    let s = fresh();
                    out.push(if c > 0 && (c as u64).is_power_of_two() {
                        Instruction::BinOp { dest: s, op: IrBinOp::Shl, lhs: Operand::Value(Value(v)),
                            rhs: Operand::Const(IrConst::I64(c.trailing_zeros() as i64)), ty: IrType::I64 }
                    } else {
                        Instruction::BinOp { dest: s, op: IrBinOp::Mul, lhs: Operand::Value(Value(v)),
                            rhs: Operand::Const(IrConst::I64(c)), ty: IrType::I64 }
                    });
                    Operand::Value(s)
                };
                if sum.konst == 0 {
                    out.push(Instruction::GetElementPtr { dest: *dest, base: cur, offset: scaled, ty: *ty });
                    rewrites.push((bi, ii, out));
                    continue;
                }
                let t = fresh();
                out.push(Instruction::GetElementPtr { dest: t, base: cur, offset: scaled, ty: *ty });
                cur = t;
            }
            out.push(Instruction::GetElementPtr { dest: *dest, base: cur, offset: Operand::Const(IrConst::I64(sum.konst)), ty: *ty });
            rewrites.push((bi, ii, out));
        }
    }
    func.next_value_id = next_id;
    let n = rewrites.len();
    for (bi, ii, out) in rewrites.into_iter().rev() {
        let block = &mut func.blocks[bi];
        let k = out.len();
        block.instructions.splice(ii..=ii, out);
        if !block.source_spans.is_empty() {
            let span = block.source_spans.get(ii).copied().unwrap_or_else(Span::dummy);
            block.source_spans.splice(ii..=ii, std::iter::repeat_n(span, k));
        }
    }
    n
}

/// `op` as a linear combination, expanding I64 adds, subtracts, and
/// multiplies and left shifts by constants; None past MAX_TERMS values.
fn linear(def: &FxHashMap<u32, Instruction>, op: &Operand, depth: usize) -> Option<Lin> {
    let v = match op {
        Operand::Const(c) => return Some(Lin { terms: Vec::new(), konst: c.to_i64()? }),
        Operand::Value(v) => *v,
    };
    let leaf = Some(Lin { terms: vec![(v.0, 1)], konst: 0 });
    if depth >= MAX_DEPTH {
        return leaf;
    }
    let Some(Instruction::BinOp { op, lhs, rhs, ty: IrType::I64 | IrType::U64, .. }) = def.get(&v.0) else { return leaf };
    let konst = |o: &Operand| match o { Operand::Const(c) => c.to_i64(), Operand::Value(_) => None };
    let l = match op {
        IrBinOp::Add | IrBinOp::Sub => {
            let mut l = linear(def, lhs, depth + 1)?;
            let r = linear(def, rhs, depth + 1)?;
            l.add(&r, if *op == IrBinOp::Add { 1 } else { -1 });
            l
        }
        IrBinOp::Mul => match (konst(lhs), konst(rhs)) {
            (_, Some(k)) => { let mut l = Lin::default(); l.add(&linear(def, lhs, depth + 1)?, k); l }
            (Some(k), _) => { let mut l = Lin::default(); l.add(&linear(def, rhs, depth + 1)?, k); l }
            _ => return leaf,
        },
        IrBinOp::Shl => match konst(rhs) {
            Some(k) if (0..63).contains(&k) => { let mut l = Lin::default(); l.add(&linear(def, lhs, depth + 1)?, 1i64 << k); l }
            _ => return leaf,
        },
        _ => return leaf,
    };
    (l.terms.len() <= MAX_TERMS).then_some(l)
}

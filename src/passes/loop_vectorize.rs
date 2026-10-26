//! Vectorize counted loops that rewrite each element of one array in place
//! with a single lane operation (AArch64 NEON).
//!
//!   h: %n = phi [%n0, pre], [%n1, h]          g: %c = cmp uge %n0, 8
//!      %p = phi [%p0, pre], [%p1, h]             condbr %c, v, h
//!      %p1 = sub %p, 2                        v: %vn = phi [%n0, g], [%vn1, v]
//!      %x = load u16 %p1                         %vp = phi [%p0, g], [%vp1, v]
//!      %y = f(%x, invariants)          =>        %a = gep %vp, -16
//!      store u16 %y -> %p1                       vecop f, u16 x 8, %a -> %a
//!      %n1 = sub %n, 1                           %vp1 = gep %vp, -16
//!      condbr %n1, h, exit                       %vn1 = sub %vn, 8
//!                                                condbr (cmp uge %vn1, 8), v, x
//!                                             x: condbr %vn1, h, exit
//!                                             h: (as before, also entered from x
//!                                                with %vn1 and %vp1)
//!
//! The vector loop does eight (16 bytes over the element size) iterations at
//! a time while that many remain, and the original loop the rest. f is one
//! operation of the element against a loop-invariant value, computed in a
//! type at least as wide as the element and stored truncated: an add, sub,
//! multiply or bitwise op (the low bits of each depend only on the low bits
//! of the operands), or `x >= w ? x - w : 0` on the zero-extended element,
//! which is a saturating subtract of w clamped to the element's maximum
//! (zlib's slide_hash). Every iteration touches a different element and the
//! loop does nothing else, so doing eight at once keeps the result.

use crate::common::fx_hash::FxHashSet;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{
    BasicBlock, BlockId, Instruction, IntrinsicOp, IrBinOp, IrCmpOp, IrConst, IrFunction, IrModule, Operand,
    Terminator, Value, VecElem, VecOpKind,
};
use super::loop_analysis;

pub(crate) fn vectorize_module(module: &mut IrModule) -> usize {
    let mut next_label = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    module.functions.iter_mut()
        .filter(|f| !f.is_declaration && f.blocks.len() > 1)
        .map(|f| vectorize_function(f, &mut next_label))
        .sum()
}

fn vectorize_function(func: &mut IrFunction, next_label: &mut u32) -> usize {
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let mut plans = Vec::new();
    for lp in loop_analysis::merge_loops_by_header(loops) {
        if lp.body.len() != 1 {
            continue;
        }
        let Some(pre) = loop_analysis::find_preheader(lp.header, &lp.body, &cfg.preds) else { continue };
        if let Some(plan) = analyze(func, lp.header, pre) {
            plans.push(plan);
        }
    }
    let count = plans.len();
    for plan in plans {
        rewrite(func, plan, next_label);
    }
    count
}

/// What the stored value is, lane by lane, in terms of the loaded element.
#[derive(Clone, Copy)]
enum Lane {
    /// The element itself; `zext` when the value also equals the element
    /// zero-extended (not just in its low bits).
    Elem { zext: bool },
    /// `op(elem, inv)`, or `op(inv, elem)` when `rev`, in the low bits.
    Op { op: VecOpKind, inv: Operand, rev: bool },
    /// `elem >= inv ? elem - inv : 0`, with inv compared in `ty`.
    SatSub { inv: Operand, ty: IrType },
}

struct Plan {
    header: usize,
    pre: usize,
    exit: BlockId,
    counter_ty: IrType,
    counter_init: Operand,
    ptr_phi: Value,
    ptr_init: Operand,
    /// Bytes from the pointer phi to the element accessed, and per iteration.
    access_off: i64,
    step: i64,
    elem: VecElem,
    lane: Lane,
}

fn int_bits(ty: IrType) -> Option<u32> {
    match ty {
        IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32 | IrType::U32
        | IrType::I64 | IrType::U64 => Some(ty.size() as u32 * 8),
        _ => None,
    }
}

fn vec_elem(ty: IrType) -> Option<VecElem> {
    Some(match ty {
        IrType::I8 => VecElem::I8,
        IrType::U8 => VecElem::U8,
        IrType::I16 => VecElem::I16,
        IrType::U16 => VecElem::U16,
        IrType::I32 => VecElem::I32,
        IrType::U32 => VecElem::U32,
        IrType::I64 => VecElem::I64,
        IrType::U64 => VecElem::U64,
        _ => return None,
    })
}

/// `base + k` for a constant k: a GEP, or an add or subtract of an integer.
fn step_of(inst: &Instruction, base: Value) -> Option<i64> {
    let is_base = |op: &Operand| matches!(op, Operand::Value(v) if *v == base);
    let k = |op: &Operand| match op {
        Operand::Const(c) => c.to_i64(),
        Operand::Value(_) => None,
    };
    match inst {
        Instruction::GetElementPtr { base: b, offset, .. } if *b == base => k(offset),
        Instruction::BinOp { op: IrBinOp::Add, lhs, rhs, .. } if is_base(lhs) => k(rhs),
        Instruction::BinOp { op: IrBinOp::Add, lhs, rhs, .. } if is_base(rhs) => k(lhs),
        Instruction::BinOp { op: IrBinOp::Sub, lhs, rhs, .. } if is_base(lhs) => k(rhs).map(|s| -s),
        _ => None,
    }
}

fn analyze(func: &IrFunction, header: usize, pre: usize) -> Option<Plan> {
    let block = &func.blocks[header];
    let (h_label, pre_label) = (block.label, func.blocks[pre].label);
    let defined: FxHashSet<u32> = block.instructions.iter().filter_map(|i| i.dest()).map(|v| v.0).collect();
    let invariant = |op: &Operand| match op {
        Operand::Value(v) => !defined.contains(&v.0),
        Operand::Const(_) => true,
    };

    // Exactly two phis: a counter and a pointer.
    let phis: Vec<(Value, IrType, Operand, Value)> = block.instructions.iter().filter_map(|i| match i {
        Instruction::Phi { dest, ty, incoming } if incoming.len() == 2 => {
            let init = incoming.iter().find(|(_, l)| *l == pre_label)?.0;
            let Operand::Value(next) = incoming.iter().find(|(_, l)| *l == h_label)?.0 else { return None };
            Some((*dest, *ty, init, next))
        }
        _ => None,
    }).collect();
    let phi_count = block.instructions.iter().filter(|i| matches!(i, Instruction::Phi { .. })).count();
    if phis.len() != 2 || phi_count != 2 {
        return None;
    }
    let def = |v: Value| block.instructions.iter().position(|i| i.dest() == Some(v));

    // The exit test: the counter stepped down by one, nonzero to go on.
    let Terminator::CondBranch { cond: Operand::Value(cond), true_label, false_label } = block.terminator else { return None };
    if true_label != h_label || false_label == h_label {
        return None;
    }
    let mut test_at = None;
    let counter_next = match def(cond).map(|k| &block.instructions[k]) {
        Some(Instruction::Cmp { op: IrCmpOp::Ne, lhs: Operand::Value(v), rhs: Operand::Const(c), .. })
            if c.to_i64() == Some(0) => { test_at = def(cond); *v }
        _ => cond,
    };
    let &(counter, counter_ty, counter_init, _) = phis.iter().find(|p| p.3 == counter_next)?;
    int_bits(counter_ty)?;
    let counter_at = def(counter_next)?;
    match &block.instructions[counter_at] {
        Instruction::BinOp { ty, .. } if int_bits(*ty) == int_bits(counter_ty) && step_of(&block.instructions[counter_at], counter) == Some(-1) => {}
        _ => return None,
    }
    let &(ptr_phi, ptr_ty, ptr_init, ptr_next) = phis.iter().find(|p| p.0 != counter)?;
    if ptr_ty != IrType::Ptr || !invariant(&counter_init) || !invariant(&ptr_init) {
        return None;
    }
    let ptr_at = def(ptr_next)?;
    let step = step_of(&block.instructions[ptr_at], ptr_phi)?;

    // One load and one store of the element, at the same address.
    let mut load = None;
    let mut store = None;
    for (k, inst) in block.instructions.iter().enumerate() {
        match inst {
            Instruction::Load { dest, ptr, ty, volatile: false, .. } if load.is_none() => load = Some((k, *dest, *ptr, *ty)),
            Instruction::Store { val, ptr, ty, volatile: false, .. } if store.is_none() => store = Some((k, *val, *ptr, *ty)),
            Instruction::Load { .. } | Instruction::Store { .. } => return None,
            _ => {}
        }
    }
    let (load_at, x, addr, ty) = load?;
    let (store_at, val, store_addr, store_ty) = store?;
    if store_addr != addr || store_ty != ty || load_at > store_at {
        return None;
    }
    let mut elem = vec_elem(ty)?;
    if step != ty.size() as i64 && step != -(ty.size() as i64) {
        return None;
    }
    let access_off = if addr == ptr_phi { 0 } else if addr == ptr_next { step } else { return None };

    // Every other instruction computes the stored value.
    let mut map: Vec<usize> = Vec::new();
    for (k, inst) in block.instructions.iter().enumerate() {
        if matches!(inst, Instruction::Phi { .. }) || [load_at, store_at, counter_at, ptr_at].contains(&k) || Some(k) == test_at {
            continue;
        }
        if !matches!(inst, Instruction::BinOp { .. } | Instruction::Cast { .. } | Instruction::Cmp { .. }
            | Instruction::Select { .. } | Instruction::Copy { .. }) {
            return None;
        }
        map.push(k);
    }
    let mut seen = FxHashSet::default();
    let lane = lane_of(block, &val, (x, ty.is_unsigned()), int_bits(ty)?, &invariant, &mut seen)?;
    if matches!(lane, Lane::Elem { .. }) || seen.len() != map.len() {
        return None;
    }
    match lane {
        Lane::Op { op: VecOpKind::Mul, .. } if ty.size() == 8 => return None,
        // The lanes saturate as unsigned values.
        Lane::SatSub { .. } => elem = vec_elem(match ty.size() {
            1 => IrType::U8,
            2 => IrType::U16,
            4 => IrType::U32,
            _ => IrType::U64,
        })?,
        _ => {}
    }

    // Nothing the loop defines is used after it, and the exit takes no
    // values from it (it gains a predecessor).
    let used_outside = func.blocks.iter().enumerate().filter(|(b, _)| *b != header).any(|(_, other)| {
        other.instructions.iter().any(|i| i.used_values().iter().any(|v| defined.contains(v)))
            || other.terminator.used_values().iter().any(|v| defined.contains(v))
    });
    let exit = func.blocks.iter().find(|b| b.label == false_label)?;
    if used_outside || exit.instructions.iter().any(|i| matches!(i, Instruction::Phi { .. })) {
        return None;
    }
    Some(Plan {
        header, pre, exit: false_label, counter_ty, counter_init, ptr_phi, ptr_init,
        access_off, step, elem, lane,
    })
}

/// What `op` is in terms of the loaded element `x.0` (of `bits` bits,
/// loaded unsigned when `x.1`), every instruction it goes through recorded
/// in `seen`.
fn lane_of(
    block: &BasicBlock, op: &Operand, x: (Value, bool), bits: u32, invariant: &dyn Fn(&Operand) -> bool,
    seen: &mut FxHashSet<u32>,
) -> Option<Lane> {
    let Operand::Value(v) = op else { return None };
    if *v == x.0 {
        return Some(Lane::Elem { zext: x.1 });
    }
    let inst = block.instructions.iter().find(|i| i.dest() == Some(*v))?;
    seen.insert(v.0);
    match inst {
        Instruction::Copy { src, .. } => lane_of(block, src, x, bits, invariant, seen),
        Instruction::Cast { src, from_ty, to_ty, .. } => {
            let (from, to) = (int_bits(*from_ty)?, int_bits(*to_ty)?);
            if from < bits || to < bits {
                return None;
            }
            let inner = lane_of(block, src, x, bits, invariant, seen)?;
            Some(match inner {
                // Widening keeps a zero extension, and so does narrowing to
                // at least the element's width.
                Lane::Elem { zext } => Lane::Elem { zext: zext && (to <= from || from_ty.is_unsigned()) },
                other => other,
            })
        }
        Instruction::BinOp { op, lhs, rhs, ty, .. } => {
            if int_bits(*ty)? < bits {
                return None;
            }
            let op = match op {
                IrBinOp::Add => VecOpKind::Add,
                IrBinOp::Sub => VecOpKind::Sub,
                IrBinOp::Mul => VecOpKind::Mul,
                IrBinOp::And => VecOpKind::And,
                IrBinOp::Or => VecOpKind::Or,
                IrBinOp::Xor => VecOpKind::Xor,
                _ => return None,
            };
            if invariant(rhs) {
                let Lane::Elem { .. } = lane_of(block, lhs, x, bits, invariant, seen)? else { return None };
                Some(Lane::Op { op, inv: *rhs, rev: false })
            } else if invariant(lhs) {
                let Lane::Elem { .. } = lane_of(block, rhs, x, bits, invariant, seen)? else { return None };
                Some(Lane::Op { op, inv: *lhs, rev: true })
            } else {
                None
            }
        }
        // elem >= w ? elem - w : 0, on the zero-extended element.
        Instruction::Select { cond: Operand::Value(c), true_val, false_val, ty, .. } => {
            int_bits(*ty).filter(|&b| b >= bits)?;
            let cmp = block.instructions.iter().find(|i| i.dest() == Some(*c))?;
            seen.insert(c.0);
            let Instruction::Cmp { op, lhs, rhs, ty: cmp_ty, .. } = cmp else { return None };
            let (diff, zero) = match op {
                IrCmpOp::Uge | IrCmpOp::Ugt => (true_val, false_val),
                IrCmpOp::Ult | IrCmpOp::Ule => (false_val, true_val),
                _ => return None,
            };
            if !matches!(zero, Operand::Const(z) if z.to_i64() == Some(0)) || !invariant(rhs) {
                return None;
            }
            if !matches!(lane_of(block, lhs, x, bits, invariant, seen)?, Lane::Elem { zext: true }) {
                return None;
            }
            let Lane::Op { op: VecOpKind::Sub, inv, rev: false } = lane_of(block, diff, x, bits, invariant, seen)? else {
                return None;
            };
            same(&inv, rhs).then_some(Lane::SatSub { inv, ty: *cmp_ty })
        }
        _ => None,
    }
}

fn same(a: &Operand, b: &Operand) -> bool {
    match (a, b) {
        (Operand::Value(x), Operand::Value(y)) => x == y,
        (Operand::Const(x), Operand::Const(y)) => x.to_i64().is_some() && x.to_i64() == y.to_i64(),
        _ => false,
    }
}

fn rewrite(func: &mut IrFunction, plan: Plan, next_label: &mut u32) {
    let lanes = 16 / plan.elem.size() as i64;
    let mut next_id = func.next_value_id;
    let mut fresh = || { let v = Value(next_id); next_id += 1; v };
    let mut label = || { let l = BlockId(*next_label); *next_label += 1; l };
    let (g, v, x) = (label(), label(), label());
    let h_label = func.blocks[plan.header].label;
    let pre_label = func.blocks[plan.pre].label;
    let span = func.blocks[plan.header].source_spans.first().copied();
    let spans = |n: usize| span.map(|s| vec![s; n]).unwrap_or_default();
    let count = |n: i64| Operand::Const(IrConst::I64(n));

    // g: the operand broadcast to every lane (a saturating subtract's
    // clamped to the element's maximum), then whether a vector step runs.
    let mut g_insts = Vec::new();
    let (op, inv, rev) = match plan.lane {
        Lane::Op { op, inv, rev } => (op, inv, rev),
        Lane::SatSub { inv, ty } => {
            let max = u64::MAX >> (64 - plan.elem.size() * 8);
            let inv = if int_bits(ty).is_some_and(|b| b as usize > plan.elem.size() * 8) {
                let (over, clamped) = (fresh(), fresh());
                g_insts.push(Instruction::Cmp { dest: over, op: IrCmpOp::Ugt, lhs: inv, rhs: count(max as i64), ty });
                g_insts.push(Instruction::Select {
                    dest: clamped, cond: Operand::Value(over), true_val: count(max as i64), false_val: inv, ty,
                });
                Operand::Value(clamped)
            } else {
                inv
            };
            (VecOpKind::SatSub, inv, false)
        }
        Lane::Elem { .. } => unreachable!(),
    };
    let go = fresh();
    g_insts.push(Instruction::Cmp { dest: go, op: IrCmpOp::Uge, lhs: plan.counter_init, rhs: count(lanes), ty: plan.counter_ty });

    // v: one vector operation per step, over the elements of `lanes`
    // iterations (the lowest address first).
    let (vn, vp, vn1, vp1, more) = (fresh(), fresh(), fresh(), fresh(), fresh());
    let low = plan.access_off + if plan.step < 0 { (lanes - 1) * plan.step } else { 0 };
    let mut v_insts = vec![
        Instruction::Phi { dest: vn, ty: plan.counter_ty, incoming: vec![(plan.counter_init, g), (Operand::Value(vn1), v)] },
        Instruction::Phi { dest: vp, ty: IrType::Ptr, incoming: vec![(plan.ptr_init, g), (Operand::Value(vp1), v)] },
    ];
    let at = if low == 0 {
        vp
    } else {
        let a = fresh();
        v_insts.push(Instruction::GetElementPtr { dest: a, base: vp, offset: count(low), ty: IrType::I8 });
        a
    };
    let elems = Operand::Value(at);
    let args = if rev { vec![inv, elems] } else { vec![elems, inv] };
    v_insts.push(Instruction::Intrinsic {
        dest: None,
        op: IntrinsicOp::VecOp { op, elem: plan.elem, bytes: 16, splat: if rev { 1 } else { 2 } },
        dest_ptr: Some(at),
        args,
    });
    v_insts.push(Instruction::GetElementPtr { dest: vp1, base: vp, offset: count(lanes * plan.step), ty: IrType::I8 });
    v_insts.push(Instruction::BinOp { dest: vn1, op: IrBinOp::Sub, lhs: Operand::Value(vn), rhs: count(lanes), ty: plan.counter_ty });
    v_insts.push(Instruction::Cmp { dest: more, op: IrCmpOp::Uge, lhs: Operand::Value(vn1), rhs: count(lanes), ty: plan.counter_ty });

    // The header continues from g or x with the scalar loop's state.
    for inst in &mut func.blocks[plan.header].instructions {
        if let Instruction::Phi { dest, incoming, .. } = inst {
            let from_x = if *dest == plan.ptr_phi { vp1 } else { vn1 };
            for (_, l) in incoming.iter_mut() {
                if *l == pre_label {
                    *l = g;
                }
            }
            incoming.push((Operand::Value(from_x), x));
        }
    }
    match &mut func.blocks[plan.pre].terminator {
        Terminator::Branch(t) => *t = g,
        Terminator::CondBranch { true_label, false_label, .. } => {
            for t in [true_label, false_label] {
                if *t == h_label {
                    *t = g;
                }
            }
        }
        _ => unreachable!("a preheader branches to its loop"),
    }
    let (gn, vn_) = (g_insts.len(), v_insts.len());
    func.blocks.push(BasicBlock {
        label: g, instructions: g_insts, source_spans: spans(gn),
        terminator: Terminator::CondBranch { cond: Operand::Value(go), true_label: v, false_label: h_label },
    });
    func.blocks.push(BasicBlock {
        label: v, instructions: v_insts, source_spans: spans(vn_),
        terminator: Terminator::CondBranch { cond: Operand::Value(more), true_label: v, false_label: x },
    });
    func.blocks.push(BasicBlock {
        label: x, instructions: Vec::new(), source_spans: Vec::new(),
        terminator: Terminator::CondBranch { cond: Operand::Value(vn1), true_label: h_label, false_label: plan.exit },
    });
    func.next_value_id = next_id;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AddressSpace;

    /// slide_hash's loop over `n` (Value 1) halfwords below Value 0, less
    /// Value 2; `extra_load` adds a second load from the element.
    fn slide(extra_load: bool) -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let v = |n| Operand::Value(Value(n));
        let k = |n| Operand::Const(IrConst::I64(n));
        let mut body = vec![
            Instruction::Phi { dest: Value(10), ty: IrType::U32, incoming: vec![(v(1), BlockId(0)), (v(20), BlockId(1))] },
            Instruction::Phi { dest: Value(11), ty: IrType::Ptr, incoming: vec![(v(0), BlockId(0)), (v(12), BlockId(1))] },
            Instruction::BinOp { dest: Value(12), op: IrBinOp::Sub, lhs: v(11), rhs: k(2), ty: IrType::I64 },
            Instruction::Load { dest: Value(13), ptr: Value(12), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false },
            Instruction::Cast { dest: Value(14), src: v(13), from_ty: IrType::U16, to_ty: IrType::U32 },
            Instruction::Cmp { dest: Value(15), op: IrCmpOp::Uge, lhs: v(14), rhs: v(2), ty: IrType::U32 },
            Instruction::BinOp { dest: Value(16), op: IrBinOp::Sub, lhs: v(14), rhs: v(2), ty: IrType::U32 },
            Instruction::Select { dest: Value(17), cond: v(15), true_val: v(16), false_val: k(0), ty: IrType::U32 },
            Instruction::Cast { dest: Value(18), src: v(17), from_ty: IrType::U32, to_ty: IrType::U16 },
            Instruction::Store { val: v(18), ptr: Value(12), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false },
            Instruction::BinOp { dest: Value(20), op: IrBinOp::Sub, lhs: v(10), rhs: k(1), ty: IrType::U32 },
        ];
        if extra_load {
            body.insert(4, Instruction::Load { dest: Value(21), ptr: Value(12), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false });
        }
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, body, Terminator::CondBranch { cond: v(20), true_label: BlockId(1), false_label: BlockId(2) }),
            block(2, vec![], Terminator::Return(None)),
        ];
        func.next_value_id = 22;
        func
    }

    #[test]
    fn slide_becomes_a_saturating_subtract() {
        let mut func = slide(false);
        let mut next_label = 3;
        assert_eq!(vectorize_function(&mut func, &mut next_label), 1);
        assert_eq!(func.blocks.len(), 6);
        assert!(matches!(func.blocks[0].terminator, Terminator::Branch(BlockId(3))));
        // The guard clamps w to 0xffff and checks for eight elements.
        let g = &func.blocks[3].instructions;
        assert!(matches!(g[1], Instruction::Select { true_val: Operand::Const(IrConst::I64(0xffff)), .. }), "{g:?}");
        assert!(matches!(g[2], Instruction::Cmp { op: IrCmpOp::Uge, rhs: Operand::Const(IrConst::I64(8)), .. }), "{g:?}");
        // The vector step: 16 bytes below the pointer, eight halfwords.
        let body = &func.blocks[4].instructions;
        let Instruction::GetElementPtr { dest: at, offset: Operand::Const(IrConst::I64(-16)), .. } = body[2] else { panic!("{body:?}") };
        let Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: VecOpKind::SatSub, elem: VecElem::U16, bytes: 16, splat: 2 }, dest_ptr: Some(d), .. } = body[3]
            else { panic!("{body:?}") };
        assert_eq!(d, at);
        // The scalar loop is entered from the guard and after the vector loop.
        let Instruction::Phi { incoming, .. } = &func.blocks[1].instructions[0] else { panic!() };
        let labels: Vec<u32> = incoming.iter().map(|(_, l)| l.0).collect();
        assert_eq!(labels, [3, 1, 5]);
    }

    #[test]
    fn loop_with_another_load_kept() {
        let mut func = slide(true);
        assert_eq!(vectorize_function(&mut func, &mut 3), 0);
    }
}

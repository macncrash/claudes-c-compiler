//! Full unrolling of small loops that run a constant number of times.
//!
//!   h: %k = phi [1, pre], [%k1, b]          h: %k = copy 1
//!      %c = cmp slt %k, 8                       br b
//!      condbr %c, b, exit               =>  b: <body, k = 1>
//!   b: <body>                                  <body, k = 2> ... <body, k = 7>
//!      %k1 = add %k, 1                         br exit
//!      br h
//!
//! zlib's braided CRC steps through the 8 bytes of each word with
//! `for (k = 1; k < W; k++)`, a variable shift and table row per byte.
//! Unrolled, each copy's `k` is a constant: the shift and the mask fold into
//! a bit-field extract and the row into the load's address, as clang does.
//!
//! Only the simplest shape: a header holding phis and one compare of a phi
//! with a constant, branching to a single body block (straight-line code,
//! no calls, atomics or inline asm) that branches back, the phi stepping by
//! a constant in the body. The trip count is found by running the
//! loop on its constants (the induction variable may go through casts:
//! an int compared and stepped at 64 bits); at most MAX_TRIPS iterations
//! and MAX_UNROLLED instructions in all. The header's own instructions
//! after its phis (casts of the counter, the compare) are copied into each
//! iteration too. Each copy reads the values the previous copy gave
//! the phis (through copies, which copy propagation removes), and uses
//! after the loop read the values after the last copy.

use crate::common::fx_hash::FxHashMap;
use crate::common::source::Span;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{BlockId, Instruction, IrBinOp, IrFunction, Operand, Terminator, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};
use super::loop_analysis;

/// Most iterations unrolled.
const MAX_TRIPS: u64 = 16;
/// Most instructions the unrolled body may hold, not counting copies, casts
/// and symbol addresses (most fold away or cost nothing).
const MAX_UNROLLED: usize = 600;

pub(crate) fn unroll_function(func: &mut IrFunction) -> usize {
    let mut total = 0;
    // Each unrolling changes the CFG; find the next loop afresh.
    for _ in 0..32 {
        if func.blocks.len() < 3 || !unroll_one(func) {
            break;
        }
        total += 1;
    }
    total
}

/// A value truncated to `ty` and extended back by its signedness.
fn wrap(v: i64, ty: IrType) -> i64 {
    match (ty.size(), ty.is_unsigned()) {
        (1, true) => v as u8 as i64,
        (1, false) => v as i8 as i64,
        (2, true) => v as u16 as i64,
        (2, false) => v as i16 as i64,
        (4, true) => v as u32 as i64,
        (4, false) => v as i32 as i64,
        _ => v,
    }
}

fn konst(op: &Operand) -> Option<i64> {
    match op {
        Operand::Const(c) => c.to_i64_exact(),
        Operand::Value(_) => None,
    }
}

/// Whether the body may be copied: straight-line code without side effects
/// beyond plain memory accesses.
fn copyable(inst: &Instruction) -> bool {
    matches!(inst, Instruction::BinOp { .. } | Instruction::UnaryOp { .. } | Instruction::Cmp { .. }
        | Instruction::Cast { .. } | Instruction::Copy { .. } | Instruction::GetElementPtr { .. }
        | Instruction::Select { .. } | Instruction::GlobalAddr { .. }
        | Instruction::Load { volatile: false, .. } | Instruction::Store { volatile: false, .. })
}

/// An instruction without memory access or side effects.
fn pure(inst: &Instruction) -> bool {
    copyable(inst) && !matches!(inst, Instruction::Load { .. } | Instruction::Store { .. })
}

/// Whether v is used outside blocks h and b.
fn used_outside(func: &IrFunction, h: usize, b: usize, v: Value) -> bool {
    func.blocks.iter().enumerate().filter(|&(k, _)| k != h && k != b).any(|(_, block)| {
        let mut used = false;
        for inst in &block.instructions {
            inst.for_each_used_value(|u| used |= u == v.0);
        }
        block.terminator.for_each_used_value(|u| used |= u == v.0);
        used
    })
}

/// Evaluate the integer copies, casts, operations and compares of insts
/// whose operands are known, adding their values to known (each held as
/// its type holds it: sign- or zero-extended).
fn evaluate(insts: &[Instruction], known: &mut FxHashMap<u32, i64>) {
    for inst in insts {
        let val = |o: &Operand, known: &FxHashMap<u32, i64>| match o {
            Operand::Const(c) => c.to_i64_exact(),
            Operand::Value(v) => known.get(&v.0).copied(),
        };
        let r = match inst {
            Instruction::Copy { dest, src } => val(src, known).map(|x| (*dest, x)),
            Instruction::Cast { dest, src, from_ty, to_ty } if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() <= 8 => {
                val(src, known).map(|x| (*dest, wrap(x, *to_ty)))
            }
            Instruction::BinOp { dest, op, lhs, rhs, ty } if ty.is_integer() && ty.size() <= 8 => {
                match (val(lhs, known), val(rhs, known)) {
                    (Some(a), Some(b)) => binop(*op, a, b, *ty).map(|x| (*dest, wrap(x, *ty))),
                    _ => None,
                }
            }
            Instruction::Cmp { dest, op, lhs, rhs, ty } if ty.is_integer() && ty.size() <= 8 => {
                match (val(lhs, known), val(rhs, known)) {
                    (Some(a), Some(b)) => Some((*dest, op.eval_i64(wrap(a, *ty), wrap(b, *ty)) as i64)),
                    _ => None,
                }
            }
            _ => None,
        };
        if let Some((d, x)) = r {
            known.insert(d.0, x);
        }
    }
}

/// The integer operations evaluate handles, at the width of ty.
fn binop(op: IrBinOp, a: i64, b: i64, ty: IrType) -> Option<i64> {
    let bits = ty.size() as u32 * 8;
    let unsigned = |x: i64| if bits == 64 { x as u64 } else { x as u64 & ((1u64 << bits) - 1) };
    Some(match op {
        IrBinOp::Add => a.wrapping_add(b),
        IrBinOp::Sub => a.wrapping_sub(b),
        IrBinOp::Mul => a.wrapping_mul(b),
        IrBinOp::And => a & b,
        IrBinOp::Or => a | b,
        IrBinOp::Xor => a ^ b,
        IrBinOp::Shl if (0..bits as i64).contains(&b) => a.wrapping_shl(b as u32),
        IrBinOp::LShr if (0..bits as i64).contains(&b) => (unsigned(a) >> b) as i64,
        IrBinOp::AShr if (0..bits as i64).contains(&b) => a >> b,
        _ => return None,
    })
}

fn set_dest(inst: &mut Instruction, v: Value) {
    match inst {
        Instruction::BinOp { dest, .. } | Instruction::UnaryOp { dest, .. } | Instruction::Cmp { dest, .. }
        | Instruction::Cast { dest, .. } | Instruction::Copy { dest, .. } | Instruction::GetElementPtr { dest, .. }
        | Instruction::Select { dest, .. } | Instruction::GlobalAddr { dest, .. } | Instruction::Load { dest, .. } => {
            *dest = v
        }
        _ => {}
    }
}

/// Find one loop of the supported shape and unroll it.
fn unroll_one(func: &mut IrFunction) -> bool {
    let cfg = analysis::CfgAnalysis::build(func);
    let loops = loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    let max_id = func.max_value_id() as usize;
    // Uses of each value, to make sure the header's compare feeds only its branch.
    let mut uses = vec![0u32; max_id + 1];
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += 1 });
        }
        block.terminator.for_each_used_value(|v| if let Some(u) = uses.get_mut(v as usize) { *u += 1 });
    }
    for lp in &loops {
        if lp.body.len() != 2 {
            continue;
        }
        let h = lp.header;
        let Some(&b) = lp.body.iter().find(|&&x| x != h) else { continue };
        if cfg.preds.len(h) != 2 || cfg.preds.row(b) != [h as u32] {
            continue;
        }
        let Some(&p) = cfg.preds.row(h).iter().find(|&&x| x as usize != b) else { continue };
        let p = p as usize;
        let (hl, bl, pl) = (func.blocks[h].label, func.blocks[b].label, func.blocks[p].label);
        let Terminator::CondBranch { cond: Operand::Value(c), true_label, false_label } = func.blocks[h].terminator else { continue };
        let (stay_on_true, exit) = if true_label == bl { (true, false_label) } else if false_label == bl { (false, true_label) } else { continue };
        if exit == hl || !matches!(func.blocks[b].terminator, Terminator::Branch(t) if t == hl) {
            continue;
        }
        // The header: phis, then pure instructions ending in the compare,
        // whose values are used only in the loop.
        let hinsts = &func.blocks[h].instructions;
        let nphis = hinsts.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
        let (phis, hpure) = hinsts.split_at(nphis);
        if uses.get(c.0 as usize) != Some(&1) || !hpure.iter().all(pure)
            || !hpure.iter().any(|i| i.dest() == Some(c))
            || hpure.iter().filter_map(|i| i.dest()).any(|d| used_outside(func, h, b, d))
        {
            continue;
        }
        // Each phi: (dest, type, value from before the loop, value from the body).
        let mut carried: Vec<(Value, Operand, Operand)> = Vec::new();
        let mut known: FxHashMap<u32, i64> = FxHashMap::default();
        for inst in phis {
            let Instruction::Phi { dest, incoming, ty } = inst else { continue };
            let from = |l| incoming.iter().find(|(_, x)| *x == l).map(|(o, _)| *o);
            match (incoming.len(), from(pl), from(bl)) {
                (2, Some(init), Some(next)) => {
                    if let Some(k) = konst(&init) {
                        known.insert(dest.0, wrap(k, *ty));
                    }
                    carried.push((*dest, init, next));
                }
                _ => break,
            }
        }
        if carried.len() != phis.len() {
            continue;
        }
        let binsts = &func.blocks[b].instructions;
        if binsts.is_empty() || !binsts.iter().all(copyable) {
            continue;
        }
        // Run the loop on the values known from constants: the branch must
        // be decided at every check.
        let phi_ty: FxHashMap<u32, IrType> = phis.iter().filter_map(|i| match i {
            Instruction::Phi { dest, ty, .. } => Some((dest.0, *ty)),
            _ => None,
        }).collect();
        let mut trips = 0u64;
        let decided = loop {
            evaluate(hpure, &mut known);
            let Some(&cv) = known.get(&c.0) else { break false };
            if (cv != 0) != stay_on_true {
                break true;
            }
            trips += 1;
            if trips > MAX_TRIPS {
                break false;
            }
            evaluate(binsts, &mut known);
            let next: FxHashMap<u32, i64> = carried.iter().filter_map(|(d, _, n)| {
                let k = match n {
                    Operand::Const(c) => c.to_i64_exact(),
                    Operand::Value(v) => known.get(&v.0).copied(),
                }?;
                Some((d.0, wrap(k, phi_ty[&d.0])))
            }).collect();
            known = next;
        };
        let size = hpure.iter().chain(binsts).filter(|i| !matches!(i,
            Instruction::Copy { .. } | Instruction::Cast { .. } | Instruction::GlobalAddr { .. })).count();
        if !decided || trips == 0 || size * trips as usize > MAX_UNROLLED {
            continue;
        }
        unroll(func, h, b, exit, &carried, trips as usize);
        return true;
    }
    false
}

fn unroll(func: &mut IrFunction, h: usize, b: usize, exit: BlockId, carried: &[(Value, Operand, Operand)], trips: usize) {
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    // Maps are indexed by the values the loop had, all below next_id.
    let map_size = next_id as usize + 1;
    let mut fresh = || {
        next_id += 1;
        Value(next_id - 1)
    };
    let hl = func.blocks[h].label;
    let bl = func.blocks[b].label;
    // Each copy runs the header's instructions after its phis, then the body.
    let nphis = func.blocks[h].instructions.iter().take_while(|i| matches!(i, Instruction::Phi { .. })).count();
    let hspans_all = std::mem::take(&mut func.blocks[h].source_spans);
    let mut body: Vec<Instruction> = func.blocks[h].instructions.split_off(nphis);
    let hparallel = hspans_all.len() == nphis + body.len();
    let mut body_spans: Vec<Span> = if hparallel { hspans_all[nphis..].to_vec() } else { Vec::new() };
    let bspans = std::mem::take(&mut func.blocks[b].source_spans);
    let binsts = std::mem::take(&mut func.blocks[b].instructions);
    let parallel = hparallel && bspans.len() == binsts.len();
    body_spans.extend(bspans.iter().copied());
    body.extend(binsts);
    if !parallel {
        body_spans.clear();
    }
    let span = body_spans.first().copied().unwrap_or(Span::new(0, 0, 0));
    // The value an operand of the previous copy has, through `map`.
    let through = |op: &Operand, map: &[Option<Operand>]| match op {
        Operand::Value(v) => map.get(v.0 as usize).copied().flatten().unwrap_or(*op),
        Operand::Const(_) => *op,
    };
    let mut insts: Vec<Instruction> = Vec::with_capacity(body.len() * trips + carried.len() * trips);
    let mut spans: Vec<Span> = Vec::new();
    let mut prev: Vec<Option<Operand>> = vec![None; map_size];
    for i in 0..trips {
        let mut map: Vec<Option<Operand>> = vec![None; map_size];
        if i > 0 {
            // The phis' values in this copy: what the previous copy gave them.
            for (d, _, next) in carried {
                let v = fresh();
                insts.push(Instruction::Copy { dest: v, src: through(next, &prev) });
                spans.push(span);
                map[d.0 as usize] = Some(Operand::Value(v));
            }
        }
        for (j, inst) in body.iter().enumerate() {
            let mut inst = inst.clone();
            if i > 0 {
                replace_operands_in_instruction(&mut inst, &map);
                if let Some(d) = inst.dest() {
                    let v = fresh();
                    set_dest(&mut inst, v);
                    map[d.0 as usize] = Some(Operand::Value(v));
                }
            }
            insts.push(inst);
            spans.push(if parallel { body_spans[j] } else { span });
        }
        prev = map;
    }
    // The phis' values after the loop.
    let mut after: Vec<Option<Operand>> = vec![None; map_size];
    for (d, _, next) in carried {
        let v = fresh();
        insts.push(Instruction::Copy { dest: v, src: through(next, &prev) });
        spans.push(span);
        after[d.0 as usize] = Some(Operand::Value(v));
    }
    let keep_spans = parallel;
    func.blocks[b].instructions = insts;
    func.blocks[b].source_spans = if keep_spans { spans } else { Vec::new() };
    func.blocks[b].terminator = Terminator::Branch(exit);
    // The header sets the phis to their values before the loop.
    let hspans = !hspans_all.is_empty();
    let hspan = hspans_all.first().copied().unwrap_or(span);
    func.blocks[h].instructions = carried.iter().map(|(d, init, _)| Instruction::Copy { dest: *d, src: *init }).collect();
    func.blocks[h].source_spans = if hspans { vec![hspan; carried.len()] } else { Vec::new() };
    func.blocks[h].terminator = Terminator::Branch(bl);
    // Everything after the loop reads the values after the last copy, and
    // the exit's phis come from the body now.
    for (k, block) in func.blocks.iter_mut().enumerate() {
        if k == h || k == b {
            continue;
        }
        for inst in &mut block.instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                for (_, l) in incoming.iter_mut() {
                    if *l == hl && block.label == exit {
                        *l = bl;
                    }
                }
            }
            replace_operands_in_instruction(inst, &after);
        }
        replace_operands_in_terminator(&mut block.terminator, &after);
    }
    func.next_value_id = next_id;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::{BasicBlock, IrCmpOp, IrConst};

    fn v(n: u32) -> Operand {
        Operand::Value(Value(n))
    }

    /// entry -> h: k = phi [1, entry], [k1, b]; s = phi [0, entry], [s1, b];
    /// c = k < 4 -> b: t = shl s, 1; s1 = add t, k; k1 = add k, 1 -> h; exit: ret s.
    fn sum_loop() -> IrFunction {
        let mut func = IrFunction::new("t".to_string(), IrType::I32, vec![], false);
        let i32c = |k| Operand::Const(IrConst::I32(k));
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock {
                label: BlockId(1),
                instructions: vec![
                    Instruction::Phi { dest: Value(1), ty: IrType::I32, incoming: vec![(i32c(1), BlockId(0)), (v(4), BlockId(2))] },
                    Instruction::Phi { dest: Value(2), ty: IrType::I32, incoming: vec![(i32c(0), BlockId(0)), (v(5), BlockId(2))] },
                    Instruction::Cmp { dest: Value(3), op: IrCmpOp::Slt, lhs: v(1), rhs: i32c(4), ty: IrType::I32 },
                ],
                terminator: Terminator::CondBranch { cond: v(3), true_label: BlockId(2), false_label: BlockId(3) },
                source_spans: Vec::new(),
            },
            BasicBlock {
                label: BlockId(2),
                instructions: vec![
                    Instruction::BinOp { dest: Value(6), op: IrBinOp::Shl, lhs: v(2), rhs: i32c(1), ty: IrType::I32 },
                    Instruction::BinOp { dest: Value(5), op: IrBinOp::Add, lhs: v(6), rhs: v(1), ty: IrType::I32 },
                    Instruction::BinOp { dest: Value(4), op: IrBinOp::Add, lhs: v(1), rhs: i32c(1), ty: IrType::I32 },
                ],
                terminator: Terminator::Branch(BlockId(1)),
                source_spans: Vec::new(),
            },
            BasicBlock { label: BlockId(3), instructions: vec![], terminator: Terminator::Return(Some(v(2))), source_spans: Vec::new() },
        ];
        func.next_value_id = 7;
        func
    }

    /// Runs the straight-line function, evaluating BinOps and Copies.
    fn run(func: &IrFunction) -> i64 {
        let mut vals = vec![0i64; func.next_value_id as usize + 1];
        let get = |vals: &[i64], o: &Operand| match o {
            Operand::Value(v) => vals[v.0 as usize],
            Operand::Const(c) => c.to_i64_exact().unwrap(),
        };
        let mut b = 0;
        loop {
            for inst in &func.blocks[b].instructions {
                match inst {
                    Instruction::Copy { dest, src } => vals[dest.0 as usize] = get(&vals, src),
                    Instruction::BinOp { dest, op, lhs, rhs, .. } => {
                        let (l, r) = (get(&vals, lhs), get(&vals, rhs));
                        vals[dest.0 as usize] = match op {
                            IrBinOp::Add => l + r,
                            IrBinOp::Shl => l << r,
                            _ => unreachable!(),
                        };
                    }
                    Instruction::Cmp { .. } => {}
                    _ => unreachable!("{:?}", inst),
                }
            }
            match &func.blocks[b].terminator {
                Terminator::Branch(l) => b = func.blocks.iter().position(|x| x.label == *l).unwrap(),
                Terminator::Return(Some(o)) => return get(&vals, o),
                t => unreachable!("{:?}", t),
            }
        }
    }

    #[test]
    fn constant_trip_loop_unrolled() {
        let mut func = sum_loop();
        assert_eq!(unroll_function(&mut func), 1);
        // k = 1, 2, 3: s = ((0*2+1)*2+2)*2+3 = 11.
        assert_eq!(run(&func), 11);
        assert!(matches!(func.blocks[1].terminator, Terminator::Branch(BlockId(2))));
        assert!(matches!(func.blocks[2].terminator, Terminator::Branch(BlockId(3))));
        assert_eq!(func.blocks[2].instructions.iter().filter(|i| matches!(i, Instruction::BinOp { op: IrBinOp::Shl, .. })).count(), 3);
    }

    #[test]
    fn long_or_unknown_loops_left_alone() {
        let mut func = sum_loop();
        if let Instruction::Cmp { rhs, .. } = &mut func.blocks[1].instructions[2] {
            *rhs = Operand::Const(IrConst::I32(100));
        }
        assert_eq!(unroll_function(&mut func), 0);
        let mut func = sum_loop();
        if let Instruction::Cmp { rhs, .. } = &mut func.blocks[1].instructions[2] {
            *rhs = v(2);
        }
        assert_eq!(unroll_function(&mut func), 0);
    }
}

//! Move a loop counter's step below the counter's last use in its block.
//!
//!   loop: %i = phi [0, pre], [%n, latch]      loop: %i = phi [0, pre], [%n, latch]
//!         %n = add %i, 1                  =>        %x = load [p, %i]
//!         %x = load [p, %i]                         %n = add %i, 1
//!
//! `p[i++]` steps the counter before the old value is read, so both are
//! live at once: the copy phi elimination puts on the back edge needs its
//! own register and, when the latch branches conditionally, its own block
//! (a move and a jump every iteration). After the move the step can take
//! the counter's register and the copy disappears.
//!
//! Applies to a phi's incoming value that is an add or subtract of the phi
//! and a constant in the phi's block, when every other use of the phi is an
//! instruction of that block (not a phi or the terminator); or to such a
//! step in any block that some phi takes (one merging paths before the
//! latch), when the phi is dead after that block. An access at a constant
//! offset from the phi (`[in, #1]`) reads it where the access is. The step goes
//! as late as possible: just before its first read in the block, or at the
//! end (before a compare the branch tests), which must be after the phi's
//! last use. (Right after that use, an
//! address computed from the counter could not fold into its access.)
//!
//! `sink_addresses` (addr_sink) likewise moves an indexed address down to
//! the load or store using it.

use crate::ir::reexports::{Instruction, IrBinOp, IrFunction, Operand, Value};

pub(crate) fn sink_steps(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    // Uses of each value: (block, instruction index), None for a terminator.
    let mut uses: Vec<Vec<Option<(usize, usize)>>> = vec![Vec::new(); max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| uses[v as usize].push(Some((b, i))));
        }
        block.terminator.for_each_used_value(|v| uses[v as usize].push(None));
    }
    let cfg = crate::ir::analysis::CfgAnalysis::build(func);
    let mut def_block = vec![usize::MAX; max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                def_block[d.0 as usize] = b;
            }
        }
    }
    // Steps of phis feeding some phi from anywhere: through a merge
    // (inflate_fast's `*out++ = here->val` for a literal, joining the
    // other codes' paths before the loop's latch), by phi -> steps.
    let mut is_phi = vec![false; max_id + 1];
    let mut feeds_phi = vec![false; max_id + 1];
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::Phi { dest, incoming, .. } = inst {
                is_phi[dest.0 as usize] = true;
                for (op, _) in incoming {
                    if let Operand::Value(v) = op {
                        feeds_phi[v.0 as usize] = true;
                    }
                }
            }
        }
    }
    let mut steps_of: crate::common::fx_hash::FxHashMap<u32, Vec<(Value, usize)>> = Default::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            if let Instruction::BinOp { dest, op: IrBinOp::Add | IrBinOp::Sub, lhs: Operand::Value(l), rhs: Operand::Const(_), .. } = inst {
                if is_phi[l.0 as usize] && feeds_phi[dest.0 as usize] {
                    steps_of.entry(l.0).or_default().push((*dest, b));
                }
            }
        }
    }
    let mut moved = 0;
    for h in 0..func.blocks.len() {
        // (phi, step, block stepping) in this block.
        let mut pairs: Vec<(Value, Value, usize)> = Vec::new();
        for inst in &func.blocks[h].instructions {
            let Instruction::Phi { dest, incoming, .. } = inst else { continue };
            for (op, from) in incoming {
                let Operand::Value(s) = op else { continue };
                let sb = def_block[s.0 as usize];
                if sb == h || (sb != usize::MAX && func.blocks[sb].label == *from) {
                    pairs.push((*dest, *s, sb));
                }
            }
            for &(s, sb) in steps_of.get(&dest.0).into_iter().flatten() {
                if !pairs.iter().any(|&(_, t, _)| t == s) {
                    pairs.push((*dest, s, sb));
                }
            }
        }
        for (phi, step, b) in pairs {
            let insts = &func.blocks[b].instructions;
            let Some(at) = insts.iter().position(|i| match i {
                Instruction::BinOp { dest, op: IrBinOp::Add | IrBinOp::Sub, lhs: Operand::Value(l), rhs: Operand::Const(_), .. } => *dest == step && *l == phi,
                Instruction::BinOp { dest, op: IrBinOp::Add, lhs: Operand::Const(_), rhs: Operand::Value(r), .. } => *dest == step && *r == phi,
                _ => false,
            }) else { continue };
            // Every other use of the phi: a non-phi instruction of this block,
            // or (for a step in the latch) one the phi's value is dead after
            // the latch for.
            let mut last = None;
            let mut ok = true;
            for u in &uses[phi.0 as usize] {
                match *u {
                    Some((ub, ui)) if ub == b && !matches!(insts[ui], Instruction::Phi { .. }) => {
                        if ui != at {
                            last = last.max(Some(ui));
                        }
                        // A constant offset from the phi is folded into the
                        // accesses using it (`[in, #1]`), which read the
                        // phi there: inflate_fast's `hold += *in++ << bits`
                        // twice, stepping in before the second load.
                        if let Instruction::GetElementPtr { dest, offset: Operand::Const(_), .. } = insts[ui] {
                            for g in uses[dest.0 as usize].iter().flatten() {
                                if g.0 == b {
                                    last = last.max(Some(g.1));
                                }
                            }
                        }
                    }
                    _ if b != h => {}
                    _ => ok = false,
                }
            }
            ok = ok && (b == h || dead_after(func, &cfg, phi, b, h));
            let Some(last_use) = last.filter(|&l| ok && l > at) else { continue };
            // As late as the step's own first read in the block allows (an
            // address computed from the counter then folds into its access).
            // Not between the block's last compare and the branch on it.
            let n = insts.len();
            let end = match (insts.last(), &func.blocks[b].terminator) {
                (Some(Instruction::Cmp { dest, .. }), crate::ir::reexports::Terminator::CondBranch { cond: Operand::Value(c), .. })
                    if dest == c => n - 1,
                _ => n,
            };
            let first_read = uses[step.0 as usize].iter()
                .filter_map(|u| u.filter(|&(ub, ui)| ub == b && ui > at).map(|(_, ui)| ui))
                .min()
                .unwrap_or(end)
                .min(end);
            if first_read <= last_use {
                continue;
            }
            let last = first_read - 1;
            let block = &mut func.blocks[b];
            let spans = block.source_spans.len() == block.instructions.len();
            let inst = block.instructions.remove(at);
            block.instructions.insert(last, inst);
            if spans {
                let span = block.source_spans.remove(at);
                block.source_spans.insert(last, span);
            }
            // Positions between moved up by one.
            for list in uses.iter_mut() {
                for u in list.iter_mut().flatten() {
                    if u.0 == b {
                        if u.1 == at {
                            u.1 = last;
                        } else if u.1 > at && u.1 <= last {
                            u.1 -= 1;
                        }
                    }
                }
            }
            moved += 1;
        }
    }
    moved
}

/// Whether the phi of block h, stepped in block b on the back edge to h,
/// is read nowhere after b before h defines it again: a use in a block
/// reached from b without passing h, a terminator of b, or a phi taking it
/// from b or such a block. (zbench's `data[i++] = *p`, tested at the
/// loop's head and stored in the latch, and read after the loop only where
/// the head left it.)
fn dead_after(func: &IrFunction, cfg: &crate::ir::analysis::CfgAnalysis, phi: Value, b: usize, h: usize) -> bool {
    let mut seen = vec![false; func.blocks.len()];
    let mut stack: Vec<usize> = cfg.succs.row(b).iter().map(|&s| s as usize).filter(|&s| s != h).collect();
    while let Some(x) = stack.pop() {
        if seen[x] {
            continue;
        }
        seen[x] = true;
        stack.extend(cfg.succs.row(x).iter().map(|&s| s as usize).filter(|&s| s != h));
    }
    if seen[b] {
        return false;
    }
    seen[b] = true;
    let label_of: crate::common::fx_hash::FxHashMap<_, _> = func.blocks.iter().enumerate().map(|(i, bl)| (bl.label, i)).collect();
    let mut read = false;
    for (x, block) in func.blocks.iter().enumerate() {
        for inst in &block.instructions {
            match inst {
                Instruction::Phi { incoming, .. } => {
                    read |= incoming.iter().any(|(op, from)| matches!(op, Operand::Value(v) if *v == phi)
                        && label_of.get(from).is_none_or(|&p| seen[p]));
                }
                _ if x != b && seen[x] => inst.for_each_used_value(|v| read |= v == phi.0),
                _ => {}
            }
        }
        if seen[x] {
            block.terminator.for_each_used_value(|v| read |= v == phi.0);
        }
    }
    !read
}

/// Move an indexed address (`gep p, %i`) whose only use is a load or store a
/// few instructions later in its block down to just before that access
/// (AArch64). `p[i] = expr` computes the address before the value, so `p`
/// and `i` die at the GEP and the value may take their registers; then the
/// backend can't fold the add into the access as `[p, i]`
/// (compress_block's `s->pending_buf[s->pending++] = (Byte)(s->bi_buf >> 8)`).
/// Not past a call, which would keep both live across it.
/// Move the instruction at from to position to, keeping spans in step.
fn move_inst(block: &mut crate::ir::reexports::BasicBlock, from: usize, to: usize) {
    let inst = block.instructions.remove(from);
    block.instructions.insert(to, inst);
    if block.source_spans.len() == block.instructions.len() {
        let span = block.source_spans.remove(from);
        block.source_spans.insert(to, span);
    }
}

pub(crate) fn sink_addresses(func: &mut IrFunction) -> usize {
    /// How far an address moves.
    const WINDOW: usize = 16;
    let max_id = func.max_value_id() as usize;
    let mut use_count = vec![0u32; max_id + 1];
    for block in &func.blocks {
        for inst in &block.instructions {
            inst.for_each_used_value(|v| if let Some(c) = use_count.get_mut(v as usize) { *c += 1 });
        }
        block.terminator.for_each_used_value(|v| if let Some(c) = use_count.get_mut(v as usize) { *c += 1 });
    }
    let mut moved = 0;
    for block in &mut func.blocks {
        // From the end, so the instructions a move shifts are already done.
        for at in (0..block.instructions.len()).rev() {
            let Instruction::GetElementPtr { dest, offset: Operand::Value(_), .. } = block.instructions[at] else { continue };
            if use_count.get(dest.0 as usize) != Some(&1) {
                continue;
            }
            let mut access = None;
            for (k, inst) in block.instructions.iter().enumerate().skip(at + 1).take(WINDOW) {
                match inst {
                    Instruction::Load { ptr, .. } if *ptr == dest => access = Some(k),
                    Instruction::Store { ptr, val, .. } if *ptr == dest && !matches!(val, Operand::Value(x) if *x == dest) => access = Some(k),
                    Instruction::Call { .. } | Instruction::CallIndirect { .. } => break,
                    _ => {
                        let mut reads = false;
                        inst.for_each_used_value(|v| reads |= v == dest.0);
                        if !reads {
                            continue;
                        }
                    }
                }
                break;
            }
            let Some(k) = access else { continue };
            let mut gep = at;
            if k > at + 1 {
                move_inst(block, at, k - 1);
                gep = k - 1;
                moved += 1;
            }
            // A doubled or shifted index read only here comes along, so the
            // backend scales it in the access (`[x14, x13, lsl #1]`) rather
            // than keeping the scaled value in a register of its own.
            let Instruction::GetElementPtr { offset: Operand::Value(o), .. } = block.instructions[gep] else { continue };
            if use_count.get(o.0 as usize) != Some(&1) {
                continue;
            }
            let Some(d) = block.instructions[..gep].iter().rposition(|i| i.dest() == Some(o)) else { continue };
            let scaled = matches!(block.instructions[d],
                Instruction::BinOp { op: IrBinOp::Add, lhs: Operand::Value(a), rhs: Operand::Value(b), .. } if a == b)
                || matches!(block.instructions[d], Instruction::BinOp { op: IrBinOp::Shl, rhs: Operand::Const(_), .. });
            if scaled && d + 1 < gep {
                move_inst(block, d, gep - 1);
                moved += 1;
            }
        }
        // Likewise a shift by a constant read only by an add, subtract or
        // logical operation of its width: next to it, the shift's source
        // is still live there and the backend shifts the operand
        // (`and x12, x14, x12, lsr #16`, deflate's UPDATE_HASH). Apart, the
        // source's register may be reused in between.
        for at in (0..block.instructions.len()).rev() {
            let Instruction::BinOp { dest, op: IrBinOp::Shl | IrBinOp::LShr | IrBinOp::AShr, rhs: Operand::Const(_), ty, .. } = block.instructions[at] else { continue };
            if use_count.get(dest.0 as usize) != Some(&1) {
                continue;
            }
            let mut user = None;
            for (k, inst) in block.instructions.iter().enumerate().skip(at + 1).take(WINDOW) {
                if matches!(inst, Instruction::Call { .. } | Instruction::CallIndirect { .. }) {
                    break;
                }
                let mut reads = false;
                inst.for_each_used_value(|v| reads |= v == dest.0);
                if reads {
                    user = Some(k);
                    break;
                }
            }
            let Some(k) = user else { continue };
            let fits = match &block.instructions[k] {
                Instruction::BinOp { op: IrBinOp::Add | IrBinOp::And | IrBinOp::Or | IrBinOp::Xor, ty: t, .. } => (t.size() == 8) == (ty.size() == 8),
                Instruction::BinOp { op: IrBinOp::Sub, rhs: Operand::Value(r), ty: t, .. } => *r == dest && (t.size() == 8) == (ty.size() == 8),
                _ => false,
            };
            if fits && k > at + 1 {
                move_inst(block, at, k - 1);
                moved += 1;
            }
        }
    }
    moved
}

/// Move a load of the word after one an earlier load of its block read
/// (`[p, #k + size]` after `[p, #k]`, both 4 or 8 bytes) up next to it, with
/// its address, when nothing between may write memory (AArch64). The
/// peephole then makes the two one `ldp`: the braided CRC loads its five
/// words each next to the xor of the one before, all into one register.
/// Each load joins one pair, as an ldp takes two, and moves past at most
/// four instructions: moved further, the loads held registers longer and
/// SQLite's speedtest ran more instructions.
pub(crate) fn group_loads(func: &mut IrFunction) -> usize {
    use crate::common::types::{AddressSpace, IrType};
    /// Lines a load moves up past at most.
    const WINDOW: usize = 4;
    let mut const_gep: crate::common::fx_hash::FxHashMap<u32, (Value, i64)> = Default::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = inst {
            if let Some(k) = c.to_i64() {
                const_gep.insert(dest.0, (*base, k));
            }
        }
    }
    let loc = |p: Value| const_gep.get(&p.0).copied().unwrap_or((p, 0));
    let plain = |ty: &IrType, volatile: bool, seg: &AddressSpace| {
        !volatile && *seg == AddressSpace::Default && (ty.is_integer() || *ty == IrType::Ptr) && matches!(ty.size(), 4 | 8)
    };
    let mut moved = 0;
    for block in &mut func.blocks {
        // Loads already in a pair (an ldp takes two).
        let mut paired: Vec<Value> = Vec::new();
        let mut j = 0;
        while j < block.instructions.len() {
            let Instruction::Load { dest, ptr, ty, volatile, seg_override, .. } = block.instructions[j] else { j += 1; continue };
            if !plain(&ty, volatile, &seg_override) || paired.contains(&dest) {
                j += 1;
                continue;
            }
            let (root, off) = loc(ptr);
            // The partner: the nearest earlier load of [root, off - size].
            let mut partner = None;
            for i in (j.saturating_sub(WINDOW)..j).rev() {
                match &block.instructions[i] {
                    Instruction::Load { dest: d, ptr: p, ty: t, volatile: v, seg_override: s, .. }
                        if plain(t, *v, s) && t.size() == ty.size() && loc(*p) == (root, off - ty.size() as i64) => {
                        partner = (!paired.contains(d)).then_some((i, *d));
                        break;
                    }
                    Instruction::Load { .. } | Instruction::BinOp { .. } | Instruction::UnaryOp { .. } | Instruction::Cmp { .. }
                    | Instruction::Cast { .. } | Instruction::Copy { .. } | Instruction::GetElementPtr { .. }
                    | Instruction::Select { .. } | Instruction::GlobalAddr { .. } => {}
                    _ => break,
                }
            }
            let Some((i, d)) = partner else { j += 1; continue };
            paired.extend([d, dest]);
            if i + 1 == j {
                j += 1;
                continue;
            }
            // The address: the root itself, or a constant GEP of it defined
            // before the partner or between (then it moves too).
            let gep_at = block.instructions[i + 1..j].iter().position(|x| x.dest() == Some(ptr)).map(|g| g + i + 1);
            let root_ok = block.instructions[i + 1..j].iter().all(|x| x.dest() != Some(root));
            if !root_ok {
                j += 1;
                continue;
            }
            if let Some(g) = gep_at {
                if !matches!(block.instructions[g], Instruction::GetElementPtr { .. }) {
                    j += 1;
                    continue;
                }
                // (The load first, which moves the GEP down one.)
                move_inst(block, j, i + 1);
                move_inst(block, g + 1, i + 1);
            } else {
                move_inst(block, j, i + 1);
            }
            moved += 1;
            j += 1;
        }
    }
    moved
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::common::types::{AddressSpace, IrType};
    use crate::ir::reexports::{BasicBlock, BlockId, CallInfo, IrCmpOp, IrConst, Terminator};

    /// `do s += p[i++]; while (i != n)`: the step first, then the index.
    fn scan(extra_use: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut body = vec![
            Instruction::Phi { dest: Value(10), ty: IrType::I64, incoming: vec![(Operand::Const(IrConst::I64(0)), BlockId(0)), (v(11), BlockId(1))] },
            Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
            Instruction::GetElementPtr { dest: Value(12), base: Value(0), offset: v(10), ty: IrType::Ptr },
            Instruction::Load { dest: Value(13), ptr: Value(12), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            Instruction::Cmp { dest: Value(14), op: IrCmpOp::Ne, lhs: v(11), rhs: v(1), ty: IrType::I64 },
        ];
        if extra_use {
            body.insert(2, Instruction::BinOp { dest: Value(15), op: IrBinOp::Mul, lhs: v(11), rhs: v(1), ty: IrType::I64 });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: body, terminator: Terminator::CondBranch { cond: v(14), true_label: BlockId(1), false_label: BlockId(2) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(None), source_spans: Vec::new() },
        ];
        func
    }

    #[test]
    fn step_moves_after_the_counters_last_use() {
        let mut func = scan(false);
        assert_eq!(sink_steps(&mut func), 1);
        // After the load (the address folds into it), before the compare.
        assert!(matches!(func.blocks[1].instructions[..], [
            Instruction::Phi { .. }, Instruction::GetElementPtr { .. }, Instruction::Load { .. },
            Instruction::BinOp { dest: Value(11), .. }, Instruction::Cmp { .. },
        ]), "{:?}", func.blocks[1].instructions);
        // Read before the counter's last use: it stays.
        let mut func = scan(true);
        assert_eq!(sink_steps(&mut func), 0);
    }

    /// `while (i < n) out[i++] = c` unrotated: the counter tested in the
    /// header, stepped and stored at in the latch; the latch also leaves
    /// to a block reading the counter when `read_after`.
    fn latch_step(read_after: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let latch_exit = if read_after {
            Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(4) }
        } else {
            Terminator::Branch(BlockId(1))
        };
        let mut func = IrFunction::new("test".to_string(), IrType::I64, vec![], false);
        func.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![
                Instruction::Phi { dest: Value(10), ty: IrType::I64, incoming: vec![(Operand::Const(IrConst::I64(0)), BlockId(0)), (v(11), BlockId(2))] },
                Instruction::Cmp { dest: Value(14), op: IrCmpOp::Ult, lhs: v(10), rhs: v(1), ty: IrType::I64 },
            ], terminator: Terminator::CondBranch { cond: v(14), true_label: BlockId(2), false_label: BlockId(3) }, source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![
                Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::GetElementPtr { dest: Value(12), base: Value(0), offset: v(10), ty: IrType::Ptr },
                Instruction::Store { val: v(3), ptr: Value(12), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            ], terminator: latch_exit, source_spans: Vec::new() },
            BasicBlock { label: BlockId(3), instructions: vec![], terminator: Terminator::Return(Some(v(10))), source_spans: Vec::new() },
            BasicBlock { label: BlockId(4), instructions: vec![], terminator: Terminator::Return(Some(v(10))), source_spans: Vec::new() },
        ];
        func
    }

    /// inflate_fast's refill, `hold += *in++; hold += *in++` when bits are
    /// short: the pointer's step (v11) reaches the loop's phi (v10) through
    /// the merge after the refill (v20), and its second load reads
    /// `[in, #1]` after the step.
    fn refill() -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let load = |dest, ptr| Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let block = |l, instructions, terminator| BasicBlock { label: BlockId(l), instructions, terminator, source_spans: Vec::new() };
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![
            block(0, vec![], Terminator::Branch(BlockId(1))),
            block(1, vec![
                Instruction::Phi { dest: Value(10), ty: IrType::Ptr, incoming: vec![(v(0), BlockId(0)), (v(20), BlockId(3))] },
            ], Terminator::CondBranch { cond: v(1), true_label: BlockId(2), false_label: BlockId(3) }),
            block(2, vec![
                Instruction::GetElementPtr { dest: Value(12), base: Value(10), offset: Operand::Const(IrConst::I64(1)), ty: IrType::Ptr },
                load(13, 10),
                Instruction::BinOp { dest: Value(11), op: IrBinOp::Add, lhs: v(10), rhs: Operand::Const(IrConst::I64(2)), ty: IrType::I64 },
                load(14, 12),
            ], Terminator::Branch(BlockId(3))),
            block(3, vec![
                Instruction::Phi { dest: Value(20), ty: IrType::Ptr, incoming: vec![(v(11), BlockId(2)), (v(10), BlockId(1))] },
            ], Terminator::CondBranch { cond: v(2), true_label: BlockId(1), false_label: BlockId(4) }),
            block(4, vec![], Terminator::Return(None)),
        ];
        func
    }

    #[test]
    fn step_through_a_merge_moves_after_an_access_at_an_offset_of_the_counter() {
        let mut func = refill();
        assert_eq!(sink_steps(&mut func), 1);
        assert!(matches!(func.blocks[2].instructions[..], [
            Instruction::GetElementPtr { .. }, Instruction::Load { .. }, Instruction::Load { .. },
            Instruction::BinOp { dest: Value(11), .. },
        ]), "{:?}", func.blocks[2].instructions);
    }

    #[test]
    fn step_in_the_latch_moves_after_the_counters_last_use() {
        let mut func = latch_step(false);
        assert_eq!(sink_steps(&mut func), 1);
        assert!(matches!(func.blocks[2].instructions[..], [
            Instruction::GetElementPtr { .. }, Instruction::Store { .. }, Instruction::BinOp { dest: Value(11), .. },
        ]), "{:?}", func.blocks[2].instructions);
        // The old value is read after the latch: both stay live there.
        let mut func = latch_step(true);
        assert_eq!(sink_steps(&mut func), 0);
    }

    /// `a = p[0]; x ^= a; b = p[1]`, with a store to p[1] in between
    /// when `store`.
    fn two_words(store: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let load = |dest, ptr| Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::I64, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        let mut body = vec![
            load(10, 0),
            Instruction::BinOp { dest: Value(11), op: IrBinOp::Xor, lhs: v(1), rhs: v(10), ty: IrType::I64 },
            Instruction::GetElementPtr { dest: Value(12), base: Value(0), offset: Operand::Const(IrConst::I64(8)), ty: IrType::Ptr },
            load(13, 12),
        ];
        if store {
            body.insert(3, Instruction::Store { val: v(11), ptr: Value(12), ty: IrType::I64, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: body, terminator: Terminator::Return(None), source_spans: Vec::new() }];
        func
    }

    #[test]
    fn load_of_the_next_word_moves_up_to_its_partner() {
        let mut func = two_words(false);
        assert_eq!(group_loads(&mut func), 1);
        assert!(matches!(func.blocks[0].instructions[..], [
            Instruction::Load { dest: Value(10), .. }, Instruction::GetElementPtr { .. },
            Instruction::Load { dest: Value(13), .. }, Instruction::BinOp { .. },
        ]), "{:?}", func.blocks[0].instructions);
        let mut func = two_words(true);
        assert_eq!(group_loads(&mut func), 0);
    }

    /// `out[pos] = *q >> 8`: the address first, then the value it stores
    /// (with a call between them when `call`).
    fn store_after_value(call: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let mut body = vec![
            Instruction::GetElementPtr { dest: Value(10), base: Value(0), offset: v(1), ty: IrType::Ptr },
            Instruction::Load { dest: Value(11), ptr: Value(2), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            Instruction::BinOp { dest: Value(12), op: IrBinOp::LShr, lhs: v(11), rhs: Operand::Const(IrConst::I64(8)), ty: IrType::I64 },
            Instruction::Store { val: v(12), ptr: Value(10), ty: IrType::U8, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
        ];
        if call {
            let info = CallInfo {
                dest: None, args: vec![], arg_types: vec![], return_type: IrType::Void, is_variadic: false, num_fixed_args: 0,
                struct_arg_sizes: vec![], struct_arg_aligns: vec![], struct_arg_classes: Vec::new(),
                struct_arg_riscv_float_classes: Vec::new(), is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
            };
            body.insert(1, Instruction::Call { func: "f".to_string(), info });
        }
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks = vec![BasicBlock { label: BlockId(0), instructions: body, terminator: Terminator::Return(None), source_spans: Vec::new() }];
        func
    }

    #[test]
    fn address_moves_down_to_its_store() {
        let mut func = store_after_value(false);
        assert_eq!(sink_addresses(&mut func), 1);
        assert!(matches!(func.blocks[0].instructions[..], [
            Instruction::Load { .. }, Instruction::BinOp { .. }, Instruction::GetElementPtr { .. }, Instruction::Store { .. },
        ]), "{:?}", func.blocks[0].instructions);
        // Not past a call.
        let mut func = store_after_value(true);
        assert_eq!(sink_addresses(&mut func), 0);
    }
}

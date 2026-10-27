//! Replace a load by the values its location holds on every path to it,
//! joined by phis where paths meet.
//!
//!   b1: store %a -> s+8              b1: store %a -> s+8
//!       br b3                            br b3
//!   b2: store %b -> s+8         =>   b2: store %b -> s+8
//!       br b3                            br b3
//!   b3: %v = load s+8                b3: %v = phi [%a, b1], [%b, b2]
//!
//! GVN reuses a load or a stored value only along dominator-tree paths and
//! forgets everything at a merge, so zlib's send_bits, whose two arms both
//! end by storing s->bi_buf and s->bi_valid, reloads both fields in the next
//! send. Here a load's value is found walking backward from it: within a
//! block, a store to the same location (the same root pointer, constant
//! byte offset and type) gives the stored value and a load of it its
//! result; at the top of a block with one predecessor the walk goes on at
//! the end of that one, and a block with several gets a phi of what each
//! predecessor holds at its end (when all of them hold something). Anything
//! that may write the location stops the walk: a store through another
//! root or overlapping it (but not, with strict aliasing, one of another
//! alias class than the load's), a call that may write memory, atomics,
//! inline asm. A walk that comes back to a block it is still answering for (a
//! loop) fails, and at most MAX_STEPS blocks are entered per function and
//! MAX_SCAN instructions looked at in each.
//! Values are found on the unchanged function and applied afterwards.
//!
//! A phi of an 8- or 16-bit value would need extending at each use, which
//! its load did for free. But when each use of such a load is an extension
//! to one 32- or 64-bit type whose uses read only the low 8 or 16 bits
//! (zlib's 16-bit bi_buf: or-ed into and truncated to store, or shifted
//! down 8 for the high byte), any wider value with those low bits will do:
//! the value a store truncated. The walk then looks for stores of such
//! truncations (or of constants), joins those wide values, and the
//! extensions read the result, upper bits unmasked:
//!
//!   b1: %t = cast I32->U16 %a; store %t -> s+8
//!   b3: %v = load U16 s+8              b3: %w = phi I32 [%a, b1], [%b, b2]
//!       %e = cast U16->I32 %v      =>      %e = copy %w

use crate::common::fx_hash::FxHashMap;
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::analysis;
use crate::ir::reexports::{BlockId, Instruction, IrConst, IrFunction, IrModule, Operand, Terminator, Value};
use super::gvn::pointer_locations;
use super::narrow::reads_low_bits;
use super::mod_ref::ModRef;

/// Most blocks scanned per function.
const MAX_STEPS: usize = 4096;
/// Most instructions looked at walking back through one block.
const MAX_SCAN: usize = 256;

/// A location: root pointer, byte offset, type, for a narrow one the wide
/// type whose truncations it is followed through (see above), and the
/// alias class of the load looking for it.
type Loc = (u32, i64, IrType, Option<IrType>, AliasClass);
/// A phi to add: block, dest, type, incoming.
type NewPhi = (usize, Value, IrType, Vec<(Operand, BlockId)>);

pub(crate) fn load_phis_module(module: &mut IrModule, changed: &mut [bool]) -> usize {
    let mr = ModRef::compute(module);
    let mut total = 0;
    for (i, func) in module.functions.iter_mut().enumerate() {
        if func.is_declaration || func.blocks.len() < 2 {
            continue;
        }
        let n = load_phis(func, &mr);
        if n > 0 {
            changed[i] = true;
            total += n;
        }
    }
    total
}

/// A load or store whose value phis may carry: a 32- or 64-bit integer or
/// pointer (a narrower value would need extending at each use, which its
/// load does for free).
fn plain(ty: &IrType, volatile: bool, seg: &AddressSpace) -> bool {
    !volatile && *seg == AddressSpace::Default && (ty.is_integer() || *ty == IrType::Ptr) && matches!(ty.size(), 4 | 8)
}

enum Answer {
    Pending,
    Known(Option<Operand>),
}

struct Finder<'a> {
    func: &'a IrFunction,
    mr: &'a ModRef,
    locs: FxHashMap<u32, (Value, i64)>,
    /// Truncations: dest -> (source, its type).
    truncs: FxHashMap<u32, (Operand, IrType)>,
    /// A truncation's source cast to the other wide type, added right after
    /// the truncation: (truncation, wide type) -> the cast's value.
    recast: FxHashMap<(u32, IrType), Value>,
    casts: Vec<(Value, Instruction)>,
    preds: Vec<Vec<usize>>,
    /// Value at the start of a block.
    start: FxHashMap<(usize, Loc), Answer>,
    phis: Vec<NewPhi>,
    next_id: u32,
    steps: usize,
}

impl Finder<'_> {
    fn location(&self, ptr: Value, ty: IrType, alias: AliasClass) -> Loc {
        let (root, off) = self.locs.get(&ptr.0).copied().unwrap_or((ptr, 0));
        (root.0, off, ty, None, alias)
    }

    /// The wide value a store of `val` leaves at a narrow location followed
    /// at type `wide`: the source of a truncation, or a constant.
    /// (A source of the other width is cast to it: the flush path of
    /// send_bits computes at 64 bits what the other path does at 32.)
    fn widened(&mut self, val: &Operand, wide: IrType) -> Option<Operand> {
        match val {
            Operand::Const(c) => {
                let k = c.to_i64()?;
                Some(Operand::Const(if wide.size() == 8 { IrConst::I64(k) } else { IrConst::I32(k as i32) }))
            }
            Operand::Value(v) => {
                let &(src, ty) = self.truncs.get(&v.0)?;
                if ty == wide {
                    return Some(src);
                }
                if !matches!(ty.size(), 4 | 8) {
                    return None;
                }
                if let Some(&c) = self.recast.get(&(v.0, wide)) {
                    return Some(Operand::Value(c));
                }
                let c = Value(self.next_id);
                self.next_id += 1;
                self.recast.insert((v.0, wide), c);
                self.casts.push((*v, Instruction::Cast { dest: c, src, from_ty: ty, to_ty: wide }));
                Some(Operand::Value(c))
            }
        }
    }

    /// What `inst` does to `loc`: Some(Some(value)) when it leaves the
    /// location holding a known value, Some(None) when it may write it,
    /// None when it leaves it alone.
    fn effect(&mut self, inst: &Instruction, loc: Loc) -> Option<Option<Operand>> {
        let (root, off, ty, wide, class) = loc;
        let apart = |o: i64, t: IrType| o + t.size() as i64 <= off || off + ty.size() as i64 <= o;
        match inst {
            Instruction::Load { dest, ptr, ty: t, volatile, seg_override, .. } => {
                let (r, o, ..) = self.location(*ptr, *t, class);
                // (A narrow load's wide value is not at hand: unknown.)
                (r == root && o == off && *t == ty && plain(t, *volatile, seg_override))
                    .then_some(if wide.is_none() { Some(Operand::Value(*dest)) } else { None })
            }
            Instruction::Store { val, ptr, ty: t, volatile, seg_override, alias } => {
                let (r, o, ..) = self.location(*ptr, *t, class);
                if let (Some(w), true) = (wide, r == root && o == off && *t == ty && !*volatile && *seg_override == AddressSpace::Default) {
                    Some(self.widened(val, w))
                } else if r == root && o == off && *t == ty && plain(t, *volatile, seg_override) {
                    Some(Some(*val))
                } else if (r == root && apart(o, *t)) || !alias.may_alias(class) {
                    // (Strict aliasing: a store of another type cannot write
                    // what the load reads.)
                    None
                } else {
                    Some(None)
                }
            }
            Instruction::Call { .. } | Instruction::CallIndirect { .. } => {
                (!self.mr.writes_nothing(inst)).then_some(None)
            }
            Instruction::Memcpy { .. }
            | Instruction::AtomicRmw { .. }
            | Instruction::AtomicCmpxchg { .. }
            | Instruction::AtomicStore { .. }
            | Instruction::Fence { .. }
            | Instruction::InlineAsm { .. }
            | Instruction::VaStart { .. }
            | Instruction::VaEnd { .. }
            | Instruction::VaCopy { .. }
            | Instruction::VaArg { .. }
            | Instruction::VaArgStruct { .. }
            | Instruction::DynAlloca { .. }
            | Instruction::StackRestore { .. } => Some(None),
            Instruction::Intrinsic { dest_ptr: Some(_), .. } => Some(None),
            _ => None,
        }
    }

    /// The value of `loc` just before instruction `end` of block `b`.
    fn before(&mut self, b: usize, end: usize, loc: Loc) -> Option<Operand> {
        let func = self.func;
        for inst in func.blocks[b].instructions[..end].iter().rev().take(MAX_SCAN) {
            if let Some(v) = self.effect(inst, loc) {
                return v;
            }
        }
        if end > MAX_SCAN {
            return None;
        }
        self.at_start(b, loc)
    }

    fn at_start(&mut self, b: usize, loc: Loc) -> Option<Operand> {
        match self.start.get(&(b, loc)) {
            Some(Answer::Known(v)) => return *v,
            Some(Answer::Pending) => return None,
            None => {}
        }
        self.steps += 1;
        if self.steps > MAX_STEPS || self.preds[b].is_empty() {
            self.start.insert((b, loc), Answer::Known(None));
            return None;
        }
        self.start.insert((b, loc), Answer::Pending);
        let preds = self.preds[b].clone();
        let mut incoming = Vec::with_capacity(preds.len());
        for &p in &preds {
            let n = self.func.blocks[p].instructions.len();
            match self.before(p, n, loc) {
                Some(v) => incoming.push((v, self.func.blocks[p].label)),
                None => break,
            }
        }
        let answer = if incoming.len() < preds.len() {
            None
        } else if incoming.iter().all(|(v, _)| same(v, &incoming[0].0)) {
            Some(incoming[0].0)
        } else {
            let dest = Value(self.next_id);
            self.next_id += 1;
            self.phis.push((b, dest, loc.3.unwrap_or(loc.2), incoming));
            Some(Operand::Value(dest))
        };
        self.start.insert((b, loc), Answer::Known(answer));
        answer
    }
}

fn same(a: &Operand, b: &Operand) -> bool {
    match (a, b) {
        (Operand::Value(x), Operand::Value(y)) => x == y,
        (Operand::Const(x), Operand::Const(y)) => x.to_hash_key() == y.to_hash_key(),
        _ => false,
    }
}

fn load_phis(func: &mut IrFunction, mr: &ModRef) -> usize {
    let cfg = analysis::CfgAnalysis::build(func);
    let n = func.blocks.len();
    // Predecessors each reaching the block once, by a branch that can take
    // a phi's edge (not an indirect branch); others get no phis.
    let mut preds: Vec<Vec<usize>> = (0..n).map(|b| cfg.preds.row(b).iter().map(|&p| p as usize).collect()).collect();
    for (b, ps) in preds.iter_mut().enumerate() {
        let mut sorted = ps.clone();
        sorted.sort_unstable();
        sorted.dedup();
        let reachable = b == 0 || cfg.idom[b] != usize::MAX;
        if !reachable || sorted.len() != ps.len()
            || ps.iter().any(|&p| matches!(func.blocks[p].terminator, Terminator::IndirectBranch { .. })) {
            ps.clear();
        }
    }
    let truncs = func.blocks.iter().flat_map(|b| &b.instructions).filter_map(|i| match i {
        Instruction::Cast { dest, src, from_ty, to_ty } if from_ty.is_integer() && to_ty.is_integer() && to_ty.size() < from_ty.size() =>
            Some((dest.0, (*src, *from_ty))),
        _ => None,
    }).collect();
    let mut finder = Finder {
        func,
        mr,
        locs: pointer_locations(func),
        truncs,
        recast: FxHashMap::default(),
        casts: Vec::new(),
        preds,
        start: FxHashMap::default(),
        phis: Vec::new(),
        next_id: func.next_value_id.max(func.max_value_id() + 1),
        steps: 0,
    };
    // Each value's uses, for narrow loads: (block, instruction), None for a
    // terminator.
    let max_id = func.max_value_id() as usize;
    let mut uses: Vec<Vec<Option<(usize, usize)>>> = vec![Vec::new(); max_id + 1];
    for (b, block) in func.blocks.iter().enumerate() {
        for (i, inst) in block.instructions.iter().enumerate() {
            inst.for_each_used_value(|v| uses[v as usize].push(Some((b, i))));
        }
        block.terminator.for_each_used_value(|v| uses[v as usize].push(None));
    }
    // A narrow load's extensions, all to one type whose uses read only the
    // loaded bits: that type.
    let wide_reads = |load: Value, ty: IrType| -> Option<IrType> {
        let bits = ty.size() * 8;
        let mut wide = None;
        for u in &uses[load.0 as usize] {
            let (ub, ui) = (*u)?;
            let Instruction::Cast { dest, from_ty, to_ty, .. } = func.blocks[ub].instructions[ui] else { return None };
            if from_ty != ty || !to_ty.is_integer() || !matches!(to_ty.size(), 4 | 8) || wide.is_some_and(|w| w != to_ty)
                || !reads_low_bits(func, &uses, dest.0, bits, 0) {
                return None;
            }
            wide = Some(to_ty);
        }
        wide
    };
    // Loads whose value is found: (block, index, value); and narrow loads'
    // extensions given their wide value.
    let mut found: Vec<(usize, usize, Operand)> = Vec::new();
    let mut widened: Vec<(Value, Operand)> = Vec::new();
    for b in 0..n {
        for (i, inst) in func.blocks[b].instructions.iter().enumerate() {
            let Instruction::Load { dest, ptr, ty, volatile, seg_override, alias } = inst else { continue };
            if plain(ty, *volatile, seg_override) {
                let loc = finder.location(*ptr, *ty, *alias);
                if let Some(v) = finder.before(b, i, loc) {
                    found.push((b, i, v));
                }
            } else if !*volatile && *seg_override == AddressSpace::Default && ty.is_integer() && ty.size() <= 2 {
                let Some(wide) = wide_reads(*dest, *ty) else { continue };
                let (root, off, ..) = finder.location(*ptr, *ty, *alias);
                if let Some(v) = finder.before(b, i, (root, off, *ty, Some(wide), *alias)) {
                    for u in uses[dest.0 as usize].iter().flatten() {
                        if let Instruction::Cast { dest: e, .. } = func.blocks[u.0].instructions[u.1] {
                            widened.push((e, v));
                        }
                    }
                }
            }
        }
    }
    let Finder { phis, next_id, casts, .. } = finder;
    // Phis and casts are made for every walk that got far enough, used or
    // not: DCE removes the unused ones only after a change.
    if found.is_empty() && widened.is_empty() {
        return 0;
    }
    for (b, i, v) in &found {
        let Instruction::Load { dest, .. } = func.blocks[*b].instructions[*i] else { unreachable!() };
        func.blocks[*b].instructions[*i] = Instruction::Copy { dest, src: *v };
    }
    let widened_count = widened.len();
    if !widened.is_empty() {
        let widened: FxHashMap<u32, Operand> = widened.into_iter().map(|(e, v)| (e.0, v)).collect();
        for inst in func.blocks.iter_mut().flat_map(|b| &mut b.instructions) {
            if let Instruction::Cast { dest, .. } = *inst {
                if let Some(&v) = widened.get(&dest.0) {
                    *inst = Instruction::Copy { dest, src: v };
                }
            }
        }
    }
    // Casts right after the truncations they read (made only when used).
    for (t, cast) in casts {
        let Some((b, at)) = func.blocks.iter().enumerate()
            .find_map(|(b, block)| block.instructions.iter().position(|i| i.dest() == Some(t)).map(|k| (b, k)))
        else { continue };
        let block = &mut func.blocks[b];
        block.instructions.insert(at + 1, cast);
        if block.source_spans.len() + 1 == block.instructions.len() {
            let span = block.source_spans[at];
            block.source_spans.insert(at + 1, span);
        }
    }
    for (b, dest, ty, incoming) in phis {
        let block = &mut func.blocks[b];
        block.instructions.insert(0, Instruction::Phi { dest, ty, incoming });
        if !block.source_spans.is_empty() {
            let span = block.source_spans[0];
            block.source_spans.insert(0, span);
        }
    }
    func.next_value_id = next_id;
    found.len() + widened_count
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::ir::reexports::BasicBlock;

    fn field(dest: u32, base: u32, off: i64) -> Instruction {
        Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: Operand::Const(crate::ir::reexports::IrConst::I64(off)), ty: IrType::I32 }
    }

    fn load(dest: u32, ptr: u32) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn store(val: u32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Store { val: Operand::Value(Value(val)), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn block(label: u32, instructions: Vec<Instruction>, terminator: Terminator) -> BasicBlock {
        BasicBlock { label: BlockId(label), instructions, terminator, source_spans: Vec::new() }
    }

    /// entry -> T | F -> merge, %0 the struct; T stores %1 to field 8, F
    /// stores %2 there and then `f_tail`; the merge reads field 8.
    fn diamond(f_tail: Vec<Instruction>) -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let br = |l| Terminator::Branch(BlockId(l));
        let mut f = vec![field(11, 0, 8), store(2, 11, IrType::I32)];
        f.extend(f_tail);
        func.blocks = vec![
            block(0, vec![], Terminator::CondBranch { cond: Operand::Value(Value(3)), true_label: BlockId(1), false_label: BlockId(2) }),
            block(1, vec![field(10, 0, 8), store(1, 10, IrType::I32), field(13, 0, 4), store(1, 13, IrType::I32)], br(3)),
            block(2, f, br(3)),
            block(3, vec![field(12, 0, 8), load(20, 12)], Terminator::Return(Some(Operand::Value(Value(20))))),
        ];
        func.next_value_id = 30;
        func
    }

    #[test]
    fn stored_values_meet_in_a_phi() {
        let mut func = diamond(vec![]);
        assert_eq!(load_phis(&mut func, &ModRef::default()), 1);
        let merge = &func.blocks[3].instructions;
        let Instruction::Phi { dest, ref incoming, .. } = merge[0] else { panic!("{:?}", merge[0]) };
        assert!(matches!(incoming[..], [(Operand::Value(Value(1)), BlockId(1)), (Operand::Value(Value(2)), BlockId(2))]), "{:?}", incoming);
        assert!(matches!(merge[2], Instruction::Copy { dest: Value(20), src: Operand::Value(v) } if v == dest));
    }

    #[test]
    fn store_through_another_pointer_or_overlapping_stops_it() {
        for tail in [vec![store(4, 5, IrType::I8)], vec![field(14, 0, 6), store(4, 14, IrType::I64)]] {
            let mut func = diamond(tail);
            assert_eq!(load_phis(&mut func, &ModRef::default()), 0);
            assert!(matches!(func.blocks[3].instructions[1], Instruction::Load { .. }));
        }
    }

    #[test]
    fn field_not_written_in_a_loop_is_its_value_before() {
        // entry: load field 8 -> %1; loop: store to field 4, load field 8.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks = vec![
            block(0, vec![field(10, 0, 8), load(1, 10)], Terminator::Branch(BlockId(1))),
            block(1, vec![field(11, 0, 4), store(1, 11, IrType::I32), load(2, 10)],
                Terminator::CondBranch { cond: Operand::Value(Value(2)), true_label: BlockId(1), false_label: BlockId(2) }),
            block(2, vec![], Terminator::Return(Some(Operand::Value(Value(2))))),
        ];
        func.next_value_id = 20;
        assert_eq!(load_phis(&mut func, &ModRef::default()), 1);
        let body = &func.blocks[1].instructions;
        let Instruction::Phi { dest, ref incoming, .. } = body[0] else { panic!("{:?}", body[0]) };
        assert!(matches!(incoming[..], [(Operand::Value(Value(1)), BlockId(0)), (Operand::Value(Value(2)), BlockId(1))]), "{:?}", incoming);
        assert!(matches!(body[3], Instruction::Copy { dest: Value(2), src: Operand::Value(v) } if v == dest));
    }

    /// Both arms store a 16-bit truncation of an I32 (%1, %2) to field 8;
    /// the merge loads it, extends it to I32 and either ors it into a value
    /// truncated and stored again (low bits only) or compares it (all bits).
    fn narrow_diamond(compared: bool) -> IrFunction {
        let v = |n| Operand::Value(Value(n));
        let trunc = |dest, src| Instruction::Cast { dest: Value(dest), src: v(src), from_ty: IrType::I32, to_ty: IrType::U16 };
        let mut merge = vec![
            field(12, 0, 8),
            Instruction::Load { dest: Value(20), ptr: Value(12), ty: IrType::U16, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            Instruction::Cast { dest: Value(21), src: v(20), from_ty: IrType::U16, to_ty: IrType::I32 },
        ];
        if compared {
            merge.push(Instruction::Cmp { dest: Value(22), op: crate::ir::reexports::IrCmpOp::Ugt, lhs: v(21), rhs: v(3), ty: IrType::I32 });
        } else {
            merge.extend([
                Instruction::BinOp { dest: Value(22), op: crate::ir::reexports::IrBinOp::Or, lhs: v(21), rhs: v(3), ty: IrType::I32 },
                trunc(23, 22),
                store(23, 12, IrType::U16),
            ]);
        }
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let br = |l| Terminator::Branch(BlockId(l));
        func.blocks = vec![
            block(0, vec![], Terminator::CondBranch { cond: v(3), true_label: BlockId(1), false_label: BlockId(2) }),
            block(1, vec![field(10, 0, 8), trunc(13, 1), store(13, 10, IrType::U16)], br(3)),
            block(2, vec![field(11, 0, 8), trunc(14, 2), store(14, 11, IrType::U16)], br(3)),
            block(3, merge, Terminator::Return(Some(v(if compared { 22 } else { 3 })))),
        ];
        func.next_value_id = 30;
        func
    }

    #[test]
    fn narrow_field_read_low_joins_the_wide_values_stored() {
        let mut func = narrow_diamond(false);
        assert_eq!(load_phis(&mut func, &ModRef::default()), 1);
        let merge = &func.blocks[3].instructions;
        let Instruction::Phi { dest, ty: IrType::I32, ref incoming } = merge[0] else { panic!("{:?}", merge[0]) };
        assert!(matches!(incoming[..], [(Operand::Value(Value(1)), BlockId(1)), (Operand::Value(Value(2)), BlockId(2))]), "{:?}", incoming);
        assert!(matches!(merge[3], Instruction::Copy { dest: Value(21), src: Operand::Value(w) } if w == dest), "{:?}", merge[3]);
        // Compared whole, the stored values' high bits would show.
        let mut func = narrow_diamond(true);
        assert_eq!(load_phis(&mut func, &ModRef::default()), 0);
    }
}

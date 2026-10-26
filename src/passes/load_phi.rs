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
//! root or overlapping it, a call that may write memory, atomics, inline
//! asm. A walk that comes back to a block it is still answering for (a
//! loop) fails, and at most MAX_STEPS blocks are entered per function and
//! MAX_SCAN instructions looked at in each.
//! Values are found on the unchanged function and applied afterwards.

use crate::common::fx_hash::FxHashMap;
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis;
use crate::ir::reexports::{BlockId, Instruction, IrFunction, IrModule, Operand, Terminator, Value};
use super::gvn::pointer_locations;
use super::mod_ref::ModRef;

/// Most blocks scanned per function.
const MAX_STEPS: usize = 4096;
/// Most instructions looked at walking back through one block.
const MAX_SCAN: usize = 256;

/// A location: root pointer, byte offset, type.
type Loc = (u32, i64, IrType);
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
    preds: Vec<Vec<usize>>,
    /// Value at the start of a block.
    start: FxHashMap<(usize, Loc), Answer>,
    phis: Vec<NewPhi>,
    next_id: u32,
    steps: usize,
}

impl Finder<'_> {
    fn location(&self, ptr: Value, ty: IrType) -> Loc {
        let (root, off) = self.locs.get(&ptr.0).copied().unwrap_or((ptr, 0));
        (root.0, off, ty)
    }

    /// What `inst` does to `loc`: Some(Some(value)) when it leaves the
    /// location holding a known value, Some(None) when it may write it,
    /// None when it leaves it alone.
    fn effect(&self, inst: &Instruction, loc: Loc) -> Option<Option<Operand>> {
        let (root, off, ty) = loc;
        let apart = |o: i64, t: IrType| o + t.size() as i64 <= off || off + ty.size() as i64 <= o;
        match inst {
            Instruction::Load { dest, ptr, ty: t, volatile, seg_override } => {
                let (r, o, _) = self.location(*ptr, *t);
                (r == root && o == off && *t == ty && plain(t, *volatile, seg_override))
                    .then_some(Some(Operand::Value(*dest)))
            }
            Instruction::Store { val, ptr, ty: t, volatile, seg_override } => {
                let (r, o, _) = self.location(*ptr, *t);
                if r == root && o == off && *t == ty && plain(t, *volatile, seg_override) {
                    Some(Some(*val))
                } else if r == root && apart(o, *t) {
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
            self.phis.push((b, dest, loc.2, incoming));
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
    let mut finder = Finder {
        func,
        mr,
        locs: pointer_locations(func),
        preds,
        start: FxHashMap::default(),
        phis: Vec::new(),
        next_id: func.next_value_id.max(func.max_value_id() + 1),
        steps: 0,
    };
    // Loads whose value is found: (block, index, value).
    let mut found: Vec<(usize, usize, Operand)> = Vec::new();
    for b in 0..n {
        for (i, inst) in func.blocks[b].instructions.iter().enumerate() {
            let Instruction::Load { ptr, ty, volatile, seg_override, .. } = inst else { continue };
            if !plain(ty, *volatile, seg_override) {
                continue;
            }
            let loc = finder.location(*ptr, *ty);
            if let Some(v) = finder.before(b, i, loc) {
                found.push((b, i, v));
            }
        }
    }
    let Finder { phis, next_id, .. } = finder;
    for (b, i, v) in &found {
        let Instruction::Load { dest, .. } = func.blocks[*b].instructions[*i] else { unreachable!() };
        func.blocks[*b].instructions[*i] = Instruction::Copy { dest, src: *v };
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
    found.len()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::reexports::BasicBlock;

    fn field(dest: u32, base: u32, off: i64) -> Instruction {
        Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: Operand::Const(crate::ir::reexports::IrConst::I64(off)), ty: IrType::I32 }
    }

    fn load(dest: u32, ptr: u32) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false }
    }

    fn store(val: u32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Store { val: Operand::Value(Value(val)), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false }
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
}

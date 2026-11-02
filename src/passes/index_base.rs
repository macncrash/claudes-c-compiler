//! A local array indexed in a loop gets its address in a value.
//!
//! The backend computes a local's address where an instruction needs it:
//! a constant offset folds into the access (`ldr x0, [sp, #72]`), but an
//! index does not, so `w[j]` in a loop built `sp + 64` again at every
//! iteration:
//!
//!   loop:  add x1, sp, #64; ldrb w13, [x1, x10]; ...
//!
//! scalar-bench's word splitter stored each letter to `w[len]` and hashed
//! the word from it, and its interpreter indexed its register array so.
//! Here, after loop rotation and phi elimination, a local whose address a
//! loop indexes (`gep %w, %j` with a variable offset) is copied into a
//! value at the end of each predecessor outside the loop, and the loop's
//! indexed GEPs read that value, which like any other may take a register.
//! The loop is the largest holding the GEP of at most MAX_LOOP_BLOCKS
//! blocks (else the innermost): the word splitter's character loop, not
//! only its hashing loop, so one value serves the store of each letter and
//! the hash of each word. Runs after phi elimination, where a value may be
//! defined in several blocks.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Operand, Value};
use super::loop_analysis::{find_natural_loops, merge_loops_by_header};

/// The largest loop given a local's value: in larger ones (SQLite's
/// sqlite3_str_vappendf, whose format loop is the function) the value
/// lives across the whole body and its calls, and spills.
const MAX_LOOP_BLOCKS: usize = 64;

pub(crate) fn index_bases_in_values(module: &mut IrModule) -> usize {
    module.functions.iter_mut().filter(|f| !f.is_declaration).map(index_bases_in_function).sum()
}

pub(crate) fn index_bases_in_function(func: &mut IrFunction) -> usize {
    let allocas: FxHashSet<u32> = func.blocks.first().map_or_else(FxHashSet::default, |b| b.instructions.iter()
        .filter_map(|i| match i { Instruction::Alloca { dest, volatile: false, .. } => Some(dest.0), _ => None })
        .collect());
    if allocas.is_empty() || func.blocks.len() < 2 {
        return 0;
    }
    // Blocks with a GEP indexing a local by a variable.
    let indexed = |b: &crate::ir::reexports::BasicBlock| b.instructions.iter().any(|i| matches!(i,
        Instruction::GetElementPtr { base, offset: Operand::Value(_), .. } if allocas.contains(&base.0)));
    if !func.blocks.iter().any(indexed) {
        return 0;
    }
    let cfg = CfgAnalysis::build(func);
    let mut loops = merge_loops_by_header(find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom));
    // Each block's loop for the values: the largest loop holding it of at
    // most MAX_LOOP_BLOCKS blocks, else the innermost.
    loops.sort_by_key(|l| l.body.len());
    let mut target: Vec<Option<usize>> = vec![None; func.blocks.len()];
    for (b, t) in target.iter_mut().enumerate() {
        let holding: Vec<usize> = (0..loops.len()).filter(|&i| loops[i].body.contains(&b)).collect();
        *t = holding.iter().rev().copied().find(|&i| loops[i].body.len() <= MAX_LOOP_BLOCKS).or(holding.first().copied());
    }
    let mut next_id = func.next_value_id.max(func.max_value_id() + 1);
    let mut count = 0;
    for (li, l) in loops.iter().enumerate() {
        let mine: Vec<usize> = l.body.iter().copied().filter(|&b| target[b] == Some(li)).collect();
        let mut bases: Vec<u32> = Vec::new();
        for &b in &mine {
            for inst in &func.blocks[b].instructions {
                if let Instruction::GetElementPtr { base, offset: Operand::Value(_), .. } = inst {
                    if allocas.contains(&base.0) && !bases.contains(&base.0) {
                        bases.push(base.0);
                    }
                }
            }
        }
        if bases.is_empty() {
            continue;
        }
        let entries: Vec<usize> = cfg.preds.row(l.header).iter().map(|&p| p as usize)
            .filter(|p| !l.body.contains(p)).collect();
        if entries.is_empty() || entries.contains(&l.header) {
            continue;
        }
        bases.sort_unstable();
        let mut values: FxHashMap<u32, Value> = FxHashMap::default();
        for a in bases {
            values.insert(a, Value(next_id));
            next_id += 1;
        }
        let mut order: Vec<(&u32, &Value)> = values.iter().collect();
        order.sort_unstable_by_key(|(a, _)| **a);
        for &p in &entries {
            let block = &mut func.blocks[p];
            for (&a, &v) in &order {
                block.instructions.push(Instruction::Copy { dest: v, src: Operand::Value(Value(a)) });
                if !block.source_spans.is_empty() {
                    let span = *block.source_spans.last().unwrap();
                    block.source_spans.push(span);
                }
            }
        }
        for &b in &mine {
            for inst in &mut func.blocks[b].instructions {
                if let Instruction::GetElementPtr { base, offset: Operand::Value(_), .. } = inst {
                    if let Some(&v) = values.get(&base.0) {
                        *base = v;
                        count += 1;
                    }
                }
            }
        }
    }
    func.next_value_id = next_id;
    count
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::{AddressSpace, AliasClass, IrType};
    use crate::ir::reexports::{BasicBlock, BlockId, IrCmpOp, IrConst, Terminator};

    /// entry: w = alloca 16; j = 0 -> loop
    /// loop:  p = gep w, j; load p; q = gep w, 4; load q; j = j + 1; j < 16 ? loop : exit
    /// The indexed GEP reads a copy of w made in entry; the constant one stays.
    #[test]
    fn indexed_local_gets_a_value_before_the_loop() {
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        let load = |d: u32, p: u32| Instruction::Load { dest: Value(d), ptr: Value(p), ty: IrType::U8,
            seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY };
        f.blocks = vec![
            BasicBlock { label: BlockId(0), instructions: vec![
                Instruction::Alloca { dest: Value(1), ty: IrType::Ptr, size: 16, align: 1, volatile: false },
                Instruction::Copy { dest: Value(2), src: Operand::Const(IrConst::I64(0)) },
            ], terminator: Terminator::Branch(BlockId(1)), source_spans: Vec::new() },
            BasicBlock { label: BlockId(1), instructions: vec![
                Instruction::GetElementPtr { dest: Value(3), base: Value(1), offset: Operand::Value(Value(2)), ty: IrType::I8 },
                load(4, 3),
                Instruction::GetElementPtr { dest: Value(5), base: Value(1), offset: Operand::Const(IrConst::I64(4)), ty: IrType::I8 },
                load(6, 5),
                Instruction::BinOp { dest: Value(2), op: crate::ir::reexports::IrBinOp::Add, lhs: Operand::Value(Value(2)),
                    rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::Cmp { dest: Value(7), op: IrCmpOp::Ult, lhs: Operand::Value(Value(2)),
                    rhs: Operand::Const(IrConst::I64(16)), ty: IrType::I64 },
            ], terminator: Terminator::CondBranch { cond: Operand::Value(Value(7)), true_label: BlockId(1), false_label: BlockId(2) },
               source_spans: Vec::new() },
            BasicBlock { label: BlockId(2), instructions: vec![], terminator: Terminator::Return(None), source_spans: Vec::new() },
        ];
        f.next_value_id = 8;
        assert_eq!(index_bases_in_function(&mut f), 1);
        assert!(matches!(f.blocks[0].instructions.last(),
            Some(Instruction::Copy { dest: Value(8), src: Operand::Value(Value(1)) })));
        assert!(matches!(f.blocks[1].instructions[0], Instruction::GetElementPtr { base: Value(8), .. }));
        assert!(matches!(f.blocks[1].instructions[2], Instruction::GetElementPtr { base: Value(1), .. }));
    }
}

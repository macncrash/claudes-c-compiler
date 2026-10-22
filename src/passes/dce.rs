//! Dead Code Elimination (DCE) pass.
//!
//! Removes instructions whose results are never used by any other instruction
//! or terminator. This is a backwards dataflow analysis: we build use-counts
//! for all values, then remove instructions that define unused values and
//! transitively propagate the removal via a worklist.
//!
//! Side-effecting instructions (stores, calls) are never removed.
//!
//! Performance: Uses a use-count-based worklist approach instead of a fixpoint
//! loop. This processes each instruction at most twice (once for counting, once
//! for removal), giving O(n) complexity instead of O(n*k) where k is the
//! maximum dead chain length.

use crate::ir::reexports::{
    Instruction,
    IrFunction,
    Operand,
};

/// Eliminate dead code in a single function using use-count-based worklist DCE.
///
/// Algorithm:
/// 1. Build use-counts for all values (single pass)
/// 2. Build a def-map: value_id -> (block_idx, inst_idx) for non-side-effecting instructions
/// 3. Find initially-dead instructions (non-side-effecting, dest use-count == 0)
/// 4. Process worklist: for each dead instruction, decrement operands' use-counts;
///    if any drops to 0, add its defining instruction to the worklist
/// 5. Sweep: remove all dead instructions in a single pass
pub(crate) fn eliminate_dead_code(func: &mut IrFunction) -> usize {
    let max_id = func.max_value_id() as usize;
    if max_id == 0 && func.blocks.len() <= 1 {
        // Tiny function, use the simple path
        return eliminate_dead_code_simple(func, max_id);
    }

    // Step 1: Build use-counts for all values.
    // For Phi nodes, exclude self-references from the use count. A phi that only
    // references itself (e.g., phi V: [entry: 0, backedge: V]) is dead if no
    // other instruction uses V. Without this exclusion, the self-reference keeps
    // use_count at 1, preventing removal of dead phis from promoted inlined
    // parameter allocas in loops. These dead phis generate copies during phi
    // elimination that corrupt inline asm output pointer stack slots, causing
    // NULL pointer dereferences (e.g., kernel boot crash in
    // init_scattered_cpuid_features).
    let mut use_count: Vec<u32> = vec![0; max_id + 1];
    for block in &func.blocks {
        for inst in &block.instructions {
            if let Instruction::Phi { dest, incoming, .. } = inst {
                // Count uses from incoming values, but skip self-references
                for (op, _) in incoming {
                    if let Operand::Value(v) = op {
                        if v.0 != dest.0 {
                            let idx = v.0 as usize;
                            if idx < use_count.len() {
                                use_count[idx] += 1;
                            }
                        }
                    }
                }
            } else {
                inst.for_each_used_value(|id| {
                    let idx = id as usize;
                    if idx < use_count.len() {
                        use_count[idx] += 1;
                    }
                });
            }
        }
        block.terminator.for_each_used_value(|id| {
            let idx = id as usize;
            if idx < use_count.len() {
                use_count[idx] += 1;
            }
        });
    }

    // Step 2: Build def-map and identify initially-dead instructions.
    // dead[block_idx][inst_idx] = true means instruction should be removed.
    let mut dead: Vec<Vec<bool>> = func.blocks.iter()
        .map(|b| vec![false; b.instructions.len()])
        .collect();

    // Map from value_id -> (block_idx, inst_idx) for removable (non-side-effecting) defs.
    let mut def_loc: Vec<(u32, u32)> = vec![(u32::MAX, u32::MAX); max_id + 1];

    // Worklist of dead instructions to process (block_idx, inst_idx).
    let mut worklist: Vec<(u32, u32)> = Vec::new();

    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            if has_side_effects(inst) {
                continue;
            }
            if let Some(dest) = inst.dest() {
                let id = dest.0 as usize;
                if id <= max_id {
                    def_loc[id] = (bi as u32, ii as u32);
                    if use_count[id] == 0 {
                        dead[bi][ii] = true;
                        worklist.push((bi as u32, ii as u32));
                    }
                }
            }
        }
    }

    // Step 3: Process worklist - transitively remove dead chains.
    drain_dead_worklist(func, &mut worklist, &mut dead, &mut use_count, &def_loc);

    // Step 3b: Dead cycles. Use counts cannot remove values that only feed
    // each other (a = phi(.., b), b = phi(.., a), or through pure copies),
    // e.g. a loop-carried value that is never read. Mark everything reachable
    // from side-effecting instructions and terminators; the rest is dead.
    let mut live = vec![false; max_id + 1];
    let mut stack: Vec<u32> = Vec::new();
    let mark = |id: u32, live: &mut Vec<bool>, stack: &mut Vec<u32>| {
        let idx = id as usize;
        if idx < live.len() && !live[idx] {
            live[idx] = true;
            stack.push(id);
        }
    };
    for (bi, block) in func.blocks.iter().enumerate() {
        for (ii, inst) in block.instructions.iter().enumerate() {
            if !dead[bi][ii] && has_side_effects(inst) {
                inst.for_each_used_value(|id| mark(id, &mut live, &mut stack));
            }
        }
        block.terminator.for_each_used_value(|id| mark(id, &mut live, &mut stack));
    }
    while let Some(id) = stack.pop() {
        let (bi, ii) = def_loc[id as usize];
        if bi != u32::MAX {
            func.blocks[bi as usize].instructions[ii as usize]
                .for_each_used_value(|u| mark(u, &mut live, &mut stack));
        }
    }
    for (idx, &(bi, ii)) in def_loc.iter().enumerate() {
        if bi != u32::MAX && !live[idx] && !dead[bi as usize][ii as usize] {
            dead[bi as usize][ii as usize] = true;
        }
    }

    // Step 4: Sweep - remove all dead instructions.
    let mut total = 0;
    for (bi, block) in func.blocks.iter_mut().enumerate() {
        let dead_flags = &dead[bi];
        let original_len = block.instructions.len();

        // Count dead instructions
        let dead_count = dead_flags.iter().filter(|&&d| d).count();
        if dead_count == 0 {
            continue;
        }

        let has_spans = block.source_spans.len() == original_len && !block.source_spans.is_empty();
        if has_spans {
            let mut write_idx = 0;
            for read_idx in 0..original_len {
                if !dead_flags[read_idx] {
                    if write_idx != read_idx {
                        block.instructions.swap(write_idx, read_idx);
                        block.source_spans.swap(write_idx, read_idx);
                    }
                    write_idx += 1;
                }
            }
            block.instructions.truncate(write_idx);
            block.source_spans.truncate(write_idx);
        } else {
            // If source_spans is non-empty but wrong length, clear it to
            // restore the invariant (empty = no debug info for this block).
            if !block.source_spans.is_empty() && block.source_spans.len() != original_len {
                block.source_spans.clear();
            }
            let mut idx = 0;
            block.instructions.retain(|_| {
                let keep = !dead_flags[idx];
                idx += 1;
                keep
            });
        }
        total += dead_count;
    }

    total
}

/// Transitively mark dead the definitions whose last use was a dead
/// instruction on the worklist.
fn drain_dead_worklist(
    func: &IrFunction,
    worklist: &mut Vec<(u32, u32)>,
    dead: &mut [Vec<bool>],
    use_count: &mut [u32],
    def_loc: &[(u32, u32)],
) {
    while let Some((bi, ii)) = worklist.pop() {
        let inst = &func.blocks[bi as usize].instructions[ii as usize];
        // Decrement use-counts of this instruction's operands.
        inst.for_each_used_value(|id| {
            let idx = id as usize;
            if idx < use_count.len() {
                use_count[idx] = use_count[idx].saturating_sub(1);
                if use_count[idx] == 0 {
                    let (dbi, dii) = def_loc[idx];
                    if dbi != u32::MAX && !dead[dbi as usize][dii as usize] {
                        dead[dbi as usize][dii as usize] = true;
                        worklist.push((dbi, dii));
                    }
                }
            }
        });
    }
}

/// Simple fixpoint DCE for very small functions (avoids overhead of def-map + worklist).
fn eliminate_dead_code_simple(func: &mut IrFunction, max_id: usize) -> usize {
    let mut used = vec![false; max_id + 1];
    let mut total = 0;
    loop {
        for slot in used.iter_mut() {
            *slot = false;
        }
        for block in &func.blocks {
            for inst in &block.instructions {
                if let Instruction::Phi { dest, incoming, .. } = inst {
                    // Exclude self-references (same fix as main DCE path)
                    for (op, _) in incoming {
                        if let Operand::Value(v) = op {
                            if v.0 != dest.0 {
                                let idx = v.0 as usize;
                                if idx < used.len() {
                                    used[idx] = true;
                                }
                            }
                        }
                    }
                } else {
                    inst.for_each_used_value(|id| {
                        let idx = id as usize;
                        if idx < used.len() {
                            used[idx] = true;
                        }
                    });
                }
            }
            block.terminator.for_each_used_value(|id| {
                let idx = id as usize;
                if idx < used.len() {
                    used[idx] = true;
                }
            });
        }

        let mut removed = 0;
        for block in &mut func.blocks {
            let original_len = block.instructions.len();
            let has_spans = block.source_spans.len() == original_len && !block.source_spans.is_empty();

            if has_spans {
                let mut write_idx = 0;
                for read_idx in 0..original_len {
                    let keep = is_live(&block.instructions[read_idx], &used);
                    if keep {
                        if write_idx != read_idx {
                            block.instructions.swap(write_idx, read_idx);
                            block.source_spans.swap(write_idx, read_idx);
                        }
                        write_idx += 1;
                    }
                }
                block.instructions.truncate(write_idx);
                block.source_spans.truncate(write_idx);
            } else {
                if !block.source_spans.is_empty() && block.source_spans.len() != original_len {
                    block.source_spans.clear();
                }
                block.instructions.retain(|inst| is_live(inst, &used));
            }
            removed += original_len - block.instructions.len();
        }

        if removed == 0 {
            break;
        }
        total += removed;
    }
    total
}

/// Check if an instruction is live (should be retained).
#[inline]
fn is_live(inst: &Instruction, used: &[bool]) -> bool {
    if has_side_effects(inst) {
        return true;
    }
    match inst.dest() {
        Some(dest) => {
            let id = dest.0 as usize;
            id < used.len() && used[id]
        }
        None => true,
    }
}

/// Check if an instruction has side effects (must not be removed).
fn has_side_effects(inst: &Instruction) -> bool {
    matches!(inst,
        // Alloca must never be removed: codegen uses positional indexing
        // (find_param_alloca) to map function parameters to their stack slots.
        // Removing unused parameter allocas shifts indices and causes miscompilation.
        Instruction::Alloca { .. } |
        // DynAlloca modifies the stack pointer at runtime - always has side effects
        Instruction::DynAlloca { .. } |
        Instruction::Store { .. } |
        // A volatile read is an access in its own right, used or not.
        Instruction::Load { volatile: true, .. } |
        Instruction::Call { .. } |
        Instruction::CallIndirect { .. } |
        Instruction::Memcpy { .. } |
        Instruction::VaStart { .. } |
        Instruction::VaEnd { .. } |
        Instruction::VaCopy { .. } |
        Instruction::VaArg { .. } |
        Instruction::VaArgStruct { .. } |
        Instruction::AtomicRmw { .. } |
        Instruction::AtomicCmpxchg { .. } |
        Instruction::AtomicLoad { .. } |
        Instruction::AtomicStore { .. } |
        Instruction::Fence { .. } |
        Instruction::GetReturnF64Second { .. } |
        Instruction::GetReturnF32Second { .. } |
        Instruction::GetReturnF128Second { .. } |
        Instruction::SetReturnF64Second { .. } |
        Instruction::SetReturnF32Second { .. } |
        Instruction::SetReturnF128Second { .. } |
        Instruction::InlineAsm { .. } |
        // StackRestore modifies the stack pointer at runtime - must not be removed.
        // StackSave is kept alive by its use in StackRestore (normal DCE liveness).
        Instruction::StackRestore { .. }
    ) || matches!(inst, Instruction::Intrinsic { op, dest_ptr, .. } if !op.is_pure() || dest_ptr.is_some())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::{AddressSpace, IrType};
    use crate::ir::reexports::{BasicBlock, BlockId, CallInfo, IrBinOp, IrConst, Terminator, Value};

    fn make_simple_func() -> IrFunction {
        // Function with: %0 = alloca i32, %1 = add 3, 4 (dead), store 42 to %0, load from %0
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Alloca { dest: Value(0), ty: IrType::I32, size: 4, align: 0, volatile: false },
                // Dead instruction: result %1 is never used
                Instruction::BinOp {
                    dest: Value(1),
                    op: IrBinOp::Add,
                    lhs: Operand::Const(IrConst::I32(3)),
                    rhs: Operand::Const(IrConst::I32(4)),
                    ty: IrType::I32,
                },
                Instruction::Store { val: Operand::Const(IrConst::I32(42)), ptr: Value(0), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false },
                Instruction::Load { dest: Value(2), ptr: Value(0), ty: IrType::I32, seg_override: AddressSpace::Default, volatile: false },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        });
        func
    }

    #[test]
    fn test_eliminate_dead_binop() {
        let mut func = make_simple_func();
        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 1); // The dead BinOp should be removed
        // Verify the remaining instructions
        assert_eq!(func.blocks[0].instructions.len(), 3); // alloca, store, load
    }

    /// Loop carrying a value that is never read, through a phi and a copy:
    ///   b0: br b1
    ///   b1: v1 = phi [0, b0], [v2, b2]; v3 = param 0; condbr v3, b2, b3
    ///   b2: v2 = copy v1 (+ dead pair v4/v5 cycling phi to phi); br b1
    ///   b3: ret v3
    fn make_dead_cycle_func() -> IrFunction {
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);
        let block = |label: u32, instructions: Vec<Instruction>, terminator: Terminator| BasicBlock {
            label: BlockId(label), instructions, terminator, source_spans: Vec::new(),
        };
        func.blocks.push(block(0, vec![], Terminator::Branch(BlockId(1))));
        func.blocks.push(block(1, vec![
            Instruction::Phi { dest: Value(1), ty: IrType::I32, incoming: vec![
                (Operand::Const(IrConst::I32(0)), BlockId(0)), (Operand::Value(Value(2)), BlockId(2))] },
            Instruction::Phi { dest: Value(4), ty: IrType::I32, incoming: vec![
                (Operand::Const(IrConst::I32(0)), BlockId(0)), (Operand::Value(Value(5)), BlockId(2))] },
            Instruction::ParamRef { dest: Value(3), param_idx: 0, ty: IrType::I32 },
        ], Terminator::CondBranch { cond: Operand::Value(Value(3)), true_label: BlockId(2), false_label: BlockId(3) }));
        func.blocks.push(block(2, vec![
            Instruction::Copy { dest: Value(2), src: Operand::Value(Value(1)) },
            Instruction::Phi { dest: Value(5), ty: IrType::I32, incoming: vec![(Operand::Value(Value(4)), BlockId(1))] },
        ], Terminator::Branch(BlockId(1))));
        func.blocks.push(block(3, vec![], Terminator::Return(Some(Operand::Value(Value(3))))));
        func
    }

    #[test]
    fn test_dead_phi_and_copy_cycles_removed() {
        let mut func = make_dead_cycle_func();
        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 4, "v1, v2, v4, v5 form dead cycles");
        let remaining: Vec<_> = func.blocks.iter().flat_map(|b| &b.instructions).collect();
        assert_eq!(remaining.len(), 1);
        assert!(matches!(remaining[0], Instruction::ParamRef { .. }));
    }

    #[test]
    fn test_live_cycle_kept() {
        // Same loop, but the carried value is returned: nothing is dead.
        let mut func = make_dead_cycle_func();
        func.blocks[3].terminator = Terminator::Return(Some(Operand::Value(Value(1))));
        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 2, "only the unrelated v4/v5 cycle is dead");
        assert!(func.blocks[2].instructions.iter().any(|i| matches!(i, Instruction::Copy { .. })));
    }

    #[test]
    fn test_side_effects_preserved() {
        // Calls should never be removed even if result is unused
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Call {
                    func: "printf".to_string(),
                    info: CallInfo {
                        dest: Some(Value(0)),
                        args: vec![],
                        arg_types: vec![],
                        return_type: IrType::I32,
                        is_variadic: true,
                        num_fixed_args: 0,
                        struct_arg_sizes: vec![],
                        struct_arg_aligns: vec![],
                        struct_arg_classes: Vec::new(),
                        struct_arg_riscv_float_classes: Vec::new(),
                        is_sret: false,
                        is_fastcall: false,
                        ret_eightbyte_classes: Vec::new(),
                    },
                },
            ],
            terminator: Terminator::Return(None),
            source_spans: Vec::new(),
        });
        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 0); // Call should not be removed
    }

    #[test]
    fn test_transitive_dead_chain() {
        // %0 = alloca i32  (side-effecting, kept)
        // %1 = add 1, 2  (dead, only used by %2)
        // %2 = add %1, 3  (dead, only used by %3)
        // %3 = add %2, 4  (dead, not used at all)
        // return void
        // All of %1, %2, %3 should be removed in a single pass.
        let mut func = IrFunction::new("test".to_string(), IrType::Void, vec![], false);
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Alloca { dest: Value(0), ty: IrType::I32, size: 4, align: 0, volatile: false },
                Instruction::BinOp {
                    dest: Value(1),
                    op: IrBinOp::Add,
                    lhs: Operand::Const(IrConst::I32(1)),
                    rhs: Operand::Const(IrConst::I32(2)),
                    ty: IrType::I32,
                },
                Instruction::BinOp {
                    dest: Value(2),
                    op: IrBinOp::Add,
                    lhs: Operand::Value(Value(1)),
                    rhs: Operand::Const(IrConst::I32(3)),
                    ty: IrType::I32,
                },
                Instruction::BinOp {
                    dest: Value(3),
                    op: IrBinOp::Add,
                    lhs: Operand::Value(Value(2)),
                    rhs: Operand::Const(IrConst::I32(4)),
                    ty: IrType::I32,
                },
            ],
            terminator: Terminator::Return(None),
            source_spans: Vec::new(),
        });

        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 3); // All three dead BinOps should be removed
        assert_eq!(func.blocks[0].instructions.len(), 1); // Only alloca remains
    }

    #[test]
    fn test_self_referencing_phi_removed() {
        // A phi that only references itself should be considered dead.
        // This happens with promoted inlined parameter allocas in loops:
        //   loop_header: phi V = [entry: Const(0), backedge: V]
        // V is only used by itself, so it's dead.
        // Without the fix, the self-reference keeps use_count=1.
        let mut func = IrFunction::new("test".to_string(), IrType::I32, vec![], false);

        // Block 0 (entry): branch to loop header
        func.blocks.push(BasicBlock {
            label: BlockId(0),
            instructions: vec![],
            terminator: Terminator::Branch(BlockId(1)),
            source_spans: Vec::new(),
        });

        // Block 1 (loop header): self-referencing phi, unused
        func.blocks.push(BasicBlock {
            label: BlockId(1),
            instructions: vec![
                Instruction::Phi {
                    dest: Value(0),
                    ty: IrType::I64,
                    incoming: vec![
                        (Operand::Const(IrConst::I64(0)), BlockId(0)),
                        (Operand::Value(Value(0)), BlockId(2)),  // self-reference
                    ],
                },
            ],
            terminator: Terminator::CondBranch {
                cond: Operand::Const(IrConst::I32(1)),
                true_label: BlockId(2),
                false_label: BlockId(3),
            },
            source_spans: Vec::new(),
        });

        // Block 2 (loop body): branch back
        func.blocks.push(BasicBlock {
            label: BlockId(2),
            instructions: vec![],
            terminator: Terminator::Branch(BlockId(1)),
            source_spans: Vec::new(),
        });

        // Block 3 (exit): return
        func.blocks.push(BasicBlock {
            label: BlockId(3),
            instructions: vec![],
            terminator: Terminator::Return(Some(Operand::Const(IrConst::I32(0)))),
            source_spans: Vec::new(),
        });

        let removed = eliminate_dead_code(&mut func);
        assert_eq!(removed, 1, "Self-referencing dead phi should be removed");
        // Loop header should have no instructions left
        assert!(func.blocks[1].instructions.is_empty(),
                "Phi should be removed from loop header");
    }
}

//! Move constant GEP offsets past variable ones, into the memory accesses.
//!
//!   %f = gep %p, 120                          %t = gep %p, %i
//!   %a = gep %f, %i                       =>  %a = gep %t, 120
//!   load %a / store -> %a                     load %a / store -> %a
//!
//! The backend folds a constant-offset GEP into the loads and stores that
//! use it ([t, #120]), but only when those are its only uses. A field
//! address indexed by a variable (`p->board[sq]`) is a constant GEP used by
//! a variable one, so it was computed once and kept in a register for every
//! access; after the rewrite the field offset folds into each access and the
//! struct pointer is the only base left live.
//!
//! Only GEPs whose every use is the address of a load or store (the ones
//! the backend folds) are rewritten. Runs once, after the main pipeline, so
//! that GVN and LICM see the original form.

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, IrType};
use crate::ir::analysis::CfgAnalysis;
use crate::ir::reexports::{Instruction, IrFunction, Operand, Value};

pub(crate) fn reassociate_function(func: &mut IrFunction) -> usize {
    // Constant-offset GEPs: dest -> (base, offset).
    let mut const_gep: FxHashMap<u32, (Value, Operand)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GetElementPtr { dest, base, offset: offset @ Operand::Const(_), .. } = inst {
            const_gep.insert(dest.0, (*base, *offset));
        }
    }
    if const_gep.is_empty() {
        return 0;
    }

    // Values used anywhere other than as a foldable load or store address.
    let mut other_use: FxHashSet<u32> = FxHashSet::default();
    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                Instruction::Load { ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {}
                Instruction::Store { val, ty, seg_override, .. }
                    if !matches!(ty, IrType::I128 | IrType::U128) && *seg_override == AddressSpace::Default => {
                    if let Operand::Value(v) = val {
                        other_use.insert(v.0);
                    }
                }
                _ => inst.for_each_used_value(|v| { other_use.insert(v); }),
            }
        }
        block.terminator.for_each_used_value(|v| { other_use.insert(v); });
    }

    let mut next_id = if func.next_value_id > 0 { func.next_value_id } else { func.max_value_id() + 1 };
    let mut changes = 0;
    for block in &mut func.blocks {
        let mut i = 0;
        while i < block.instructions.len() {
            if let Instruction::GetElementPtr { dest, base, offset: index @ Operand::Value(_), ty } = block.instructions[i] {
                if let Some(&(root, offset)) = const_gep.get(&base.0) {
                    if !other_use.contains(&dest.0) {
                        let inner = Value(next_id);
                        next_id += 1;
                        block.instructions[i] = Instruction::GetElementPtr { dest, base: inner, offset, ty };
                        block.instructions.insert(i, Instruction::GetElementPtr { dest: inner, base: root, offset: index, ty });
                        if !block.source_spans.is_empty() {
                            let span = block.source_spans[i];
                            block.source_spans.insert(i, span);
                        }
                        changes += 1;
                        i += 1;
                    }
                }
            }
            i += 1;
        }
    }
    func.next_value_id = next_id;
    if changes > 0 {
        merge_dominated_geps(func);
    }
    changes
}

/// The rewrite makes one `gep %p, %i` per access (`rays[d][sq]` for each
/// ray d), and GVN has already run, so a GEP with the same base and offset
/// as one in a dominating block (or earlier in its own) becomes a copy of it.
fn merge_dominated_geps(func: &mut IrFunction) {
    let cfg = CfgAnalysis::build(func);
    // Depth-first over the dominator tree, with the GEPs seen on the path.
    let mut seen: FxHashMap<(u32, u32), Value> = FxHashMap::default();
    let mut stack: Vec<(usize, bool)> = vec![(0, false)];
    let mut added: Vec<Vec<(u32, u32)>> = vec![Vec::new(); func.blocks.len()];
    while let Some((b, leaving)) = stack.pop() {
        if leaving {
            for key in added[b].drain(..) {
                seen.remove(&key);
            }
            continue;
        }
        for inst in &mut func.blocks[b].instructions {
            let Instruction::GetElementPtr { dest, base, offset: Operand::Value(offset), .. } = *inst else { continue };
            let key = (base.0, offset.0);
            match seen.get(&key) {
                Some(&first) => *inst = Instruction::Copy { dest, src: Operand::Value(first) },
                None => {
                    seen.insert(key, dest);
                    added[b].push(key);
                }
            }
        }
        stack.push((b, true));
        stack.extend(cfg.dom_children[b].iter().map(|&c| (c, false)));
    }
}

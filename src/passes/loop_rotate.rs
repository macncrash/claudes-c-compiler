//! Loop rotation by header duplication.
//!
//! A `for`/`while` loop is lowered top-tested: the header computes the
//! condition and branches into the body or out, and every iteration ends
//! with an unconditional branch back to it. Copying a small header to the
//! end of each latch turns the loop bottom-tested, so an iteration runs one
//! conditional branch instead of a taken branch plus a conditional one, and
//! the original header remains as the entry guard:
//!
//!   header:  %c = cmp slt %i, %n             header:  %c = cmp slt %i, %n
//!            condbr %c, body, exit                    condbr %c, body, exit
//!   body:    ...                         =>   body:    ...
//!            %i = copy %next                          %i = copy %next
//!            br header                                %c2 = cmp slt %i, %n
//!                                                     condbr %c2, body, exit
//!
//! Runs after phi elimination, where executing the header's instructions at
//! the end of the latch is exactly what the original path did. Values the
//! header defines get fresh names in the copy; one also used outside the
//! header is turned into a variable assigned by a Copy in both places, like
//! a lowered phi.

use crate::ir::analysis;
use crate::ir::reexports::{Instruction, IrFunction, IrModule, Operand, Terminator, Value};
use super::copy_prop::{replace_operands_in_instruction, replace_operands_in_terminator};

/// Most instructions a header may have to be duplicated.
const MAX_HEADER_INSTS: usize = 6;

pub(crate) fn rotate_loops(module: &mut IrModule) {
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        rotate_function(func);
    }
}

pub(crate) fn rotate_function(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }
    let cfg = analysis::CfgAnalysis::build(func);
    let label_to_idx = analysis::build_label_map(func);
    let dominates = |a: usize, mut b: usize| loop {
        if a == b {
            return true;
        }
        let up = cfg.idom[b];
        if up == b || up == usize::MAX {
            return false;
        }
        b = up;
    };

    // Definitions per value, and whether each value is used outside the
    // block it is defined in (checked per header below).
    if func.next_value_id == 0 {
        func.next_value_id = func.blocks.iter().flat_map(|b| &b.instructions)
            .filter_map(|i| i.dest()).map(|v| v.0 + 1).max().unwrap_or(0);
    }
    let n = func.next_value_id as usize;
    let mut defs = vec![0u8; n];
    let mut use_blocks: Vec<Option<usize>> = vec![None; n];
    let mut used_in_several = vec![false; n];
    for (b, block) in func.blocks.iter().enumerate() {
        let mut note = |v: u32| {
            let v = v as usize;
            if v >= n {
                return;
            }
            match use_blocks[v] {
                None => use_blocks[v] = Some(b),
                Some(u) if u != b => used_in_several[v] = true,
                _ => {}
            }
        };
        for inst in &block.instructions {
            if let Some(d) = inst.dest() {
                if (d.0 as usize) < n {
                    defs[d.0 as usize] = defs[d.0 as usize].saturating_add(1);
                }
            }
            inst.for_each_used_value(&mut note);
        }
        block.terminator.for_each_used_value(&mut note);
    }

    let mut rotated = 0;
    for latch in 0..func.blocks.len() {
        let Terminator::Branch(target) = func.blocks[latch].terminator else { continue };
        let Some(&header) = label_to_idx.get(&target) else { continue };
        if header == latch || !dominates(header, latch) {
            continue;
        }
        let hb = &func.blocks[header];
        if !matches!(hb.terminator, Terminator::CondBranch { .. }) || hb.instructions.len() > MAX_HEADER_INSTS {
            continue;
        }
        let duplicable = hb.instructions.iter().all(|inst| {
            let simple = matches!(inst,
                Instruction::Cmp { .. } | Instruction::BinOp { .. } | Instruction::UnaryOp { .. }
                | Instruction::Cast { .. } | Instruction::Copy { .. } | Instruction::GetElementPtr { .. }
                | Instruction::Load { .. } | Instruction::Select { .. });
            simple && inst.dest().is_some_and(|d| (d.0 as usize) < defs.len())
        });
        if !duplicable {
            continue;
        }

        // A value used outside the header becomes a variable, as a lowered
        // phi is: the header computes a fresh value and copies it over, and
        // so will the duplicate. (A Copy already is such a definition.)
        let mut k = 0;
        while k < func.blocks[header].instructions.len() {
            let inst = &func.blocks[header].instructions[k];
            let d = inst.dest().unwrap().0 as usize;
            // (Values this pass created are used only in their own block.)
            let local = d >= n || (!used_in_several[d] && use_blocks[d].is_none_or(|u| u == header));
            if defs[d] == 1 && !local {
                defs[d] = 2;
                if !matches!(inst, Instruction::Copy { .. }) {
                    let fresh = Value(func.next_value_id);
                    func.next_value_id += 1;
                    defs.push(1);
                    let hb = &mut func.blocks[header];
                    set_dest(&mut hb.instructions[k], fresh);
                    hb.instructions.insert(k + 1, Instruction::Copy { dest: Value(d as u32), src: Operand::Value(fresh) });
                    if hb.source_spans.len() + 1 == hb.instructions.len() {
                        let span = hb.source_spans[k];
                        hb.source_spans.insert(k + 1, span);
                    }
                    k += 1;
                }
            }
            k += 1;
        }

        // Copy the header, renaming its single-definition values.
        let hb = &func.blocks[header];
        let mut remap: Vec<Option<Operand>> = vec![None; func.next_value_id as usize];
        let mut copies = Vec::with_capacity(hb.instructions.len());
        let mut next_id = func.next_value_id;
        for inst in &hb.instructions {
            let mut copy = inst.clone();
            replace_operands_in_instruction(&mut copy, &remap);
            let d = inst.dest().unwrap();
            if defs[d.0 as usize] == 1 {
                let fresh = Value(next_id);
                next_id += 1;
                defs.push(1);
                set_dest(&mut copy, fresh);
                remap[d.0 as usize] = Some(Operand::Value(fresh));
            }
            copies.push(copy);
        }
        let mut term = hb.terminator.clone();
        replace_operands_in_terminator(&mut term, &remap);
        let spans = hb.source_spans.clone();
        func.next_value_id = next_id;

        let lb = &mut func.blocks[latch];
        if lb.source_spans.len() == lb.instructions.len() && spans.len() == copies.len() {
            lb.source_spans.extend(spans);
        }
        lb.instructions.extend(copies);
        lb.terminator = term;
        rotated += 1;
    }
    rotated
}

/// Rename the value an instruction defines (one of the kinds rotation copies).
fn set_dest(inst: &mut Instruction, v: Value) {
    match inst {
        Instruction::Cmp { dest, .. } | Instruction::BinOp { dest, .. } | Instruction::UnaryOp { dest, .. }
        | Instruction::Cast { dest, .. } | Instruction::Copy { dest, .. } | Instruction::GetElementPtr { dest, .. }
        | Instruction::Load { dest, .. } | Instruction::Select { dest, .. } => *dest = v,
        _ => unreachable!("loop rotation copies only simple instructions"),
    }
}

//! ArmCodegen: function call operations.

use crate::ir::reexports::{Instruction, IrConst, Operand, Value};
use crate::common::types::IrType;
use crate::backend::call_abi::{CallAbiConfig, CallArgClass, compute_stack_arg_space};
use crate::backend::traits::ArchCodegen;
use super::emit::{ArmCodegen, FptrSource, callee_saved_name, ARM_ARG_REGS};

/// Register arguments only (scalars, HFAs, a skipped HFA result pointer):
/// emit_call_reg_args_impl loads each straight into its register.
fn reg_args_only(arg_classes: &[CallArgClass]) -> bool {
    arg_classes.iter().all(|c| matches!(c, CallArgClass::IntReg { .. } | CallArgClass::FloatReg { .. }
        | CallArgClass::StructHfaReg { .. } | CallArgClass::ZeroSizeSkip))
}

impl ArmCodegen {
    /// Store (`str`, before) or reload (`ldr`, after) the caller-saved
    /// registers a call's gp_call_saves lists, each in its own slot.
    pub(super) fn save_around_call(&mut self, inst: &Instruction, op: &str) {
        let Some(regs) = self.gp_call_saves.get(&(inst as *const Instruction as usize)) else { return };
        for r in regs.clone() {
            let slot = self.gp_call_save_offset + 8 * (r.0 as i64 - 10);
            if op == "str" {
                self.emit_store_to_sp(callee_saved_name(r), slot, "str");
            } else {
                self.emit_load_from_sp(callee_saved_name(r), slot, "ldr");
            }
        }
    }

    pub(super) fn call_abi_config_impl(&self) -> CallAbiConfig {
        CallAbiConfig {
            max_int_regs: 8, max_float_regs: 8,
            // AAPCS64 starts a 16-byte aligned argument at an even register;
            // Apple's arm64 ABI does not (an __int128 after an int is in x1:x2).
            align_i128_pairs: !crate::common::types::target_is_darwin(),
            f128_in_fp_regs: true, f128_in_gp_pairs: false,
            variadic_floats_in_gp: false,
            large_struct_by_ref: true,
            use_sysv_struct_classification: false,
            use_riscv_float_struct_classification: false,
            allow_struct_split_reg_stack: false,
            align_struct_pairs: false,
            sret_uses_dedicated_reg: true,
            variadic_args_on_stack: crate::common::types::target_is_darwin(),
            pack_stack_scalars: crate::common::types::target_is_darwin(),
            use_hfa_classification: true,
        }
    }

    pub(super) fn emit_call_compute_stack_space_impl(&self, arg_classes: &[CallArgClass], _arg_types: &[IrType]) -> usize {
        compute_stack_arg_space(arg_classes)
    }

    pub(super) fn emit_call_f128_pre_convert_impl(&mut self, args: &[Operand], arg_classes: &[CallArgClass],
                                   _arg_types: &[IrType], _stack_arg_space: usize) -> usize {
        let _ = (args, arg_classes);
        0
    }

    pub(super) fn emit_call_stack_args_impl(&mut self, args: &[Operand], arg_classes: &[CallArgClass],
                            _arg_types: &[IrType], stack_arg_space: usize, fptr_spill: usize, _f128_temp_space: usize) -> i64 {
        if stack_arg_space > 0 {
            self.emit_sub_sp(stack_arg_space as i64);
            let src_adjust = if self.state.has_dyn_alloca { 0 } else { stack_arg_space as i64 + fptr_spill as i64 };
            let mut stack_offset = 0i64;
            for (arg_idx, arg) in args.iter().enumerate() {
                if !arg_classes[arg_idx].is_stack() { continue; }
                let cls = arg_classes[arg_idx];
                let align = cls.stack_align() as i64;
                stack_offset = (stack_offset + align - 1) / align * align;
                match cls {
                    CallArgClass::StructStackAligned { size, align } => {
                        // Copied in 4-byte steps when it may start at a 4-byte offset.
                        match arg {
                            Operand::Value(v) => {
                                if let Some(&reg) = self.reg_assignments.get(&v.0) {
                                    let reg_name = callee_saved_name(reg);
                                    self.state.emit_fmt(format_args!("    mov x0, {}", reg_name));
                                } else if let Some(slot) = self.state.get_slot(v.0) {
                                    let adjusted = slot.0 + src_adjust;
                                    if self.state.is_alloca(v.0) {
                                        self.emit_alloca_addr("x0", v.0, adjusted);
                                    } else {
                                        self.emit_load_from_sp("x0", adjusted, "ldr");
                                    }
                                } else {
                                    self.state.emit("    mov x0, #0");
                                }
                            }
                            Operand::Const(_) => { self.operand_to_x0(arg); }
                        }
                        let (reg, step) = if align >= 8 { ("x1", 8) } else { ("w1", 4) };
                        for k in 0..size / step {
                            let off = (k * step) as i64;
                            self.emit_load_from_reg(reg, "x0", off, "ldr");
                            self.emit_store_to_raw_sp(reg, stack_offset + off, "str");
                        }
                        stack_offset += size as i64;
                    }
                    CallArgClass::StructByValStack { size } | CallArgClass::LargeStructStack { size } => {
                        let n_dwords = size.div_ceil(8);
                        match arg {
                            Operand::Value(v) => {
                                if let Some(&reg) = self.reg_assignments.get(&v.0) {
                                    let reg_name = callee_saved_name(reg);
                                    self.state.emit_fmt(format_args!("    mov x0, {}", reg_name));
                                } else if let Some(slot) = self.state.get_slot(v.0) {
                                    let adjusted = slot.0 + src_adjust;
                                    if self.state.is_alloca(v.0) {
                                        self.emit_alloca_addr("x0", v.0, adjusted);
                                    } else {
                                        self.emit_load_from_sp("x0", adjusted, "ldr");
                                    }
                                } else {
                                    self.state.emit("    mov x0, #0");
                                }
                            }
                            Operand::Const(_) => { self.operand_to_x0(arg); }
                        }
                        for qi in 0..n_dwords {
                            let src_off = (qi * 8) as i64;
                            self.emit_load_from_reg("x1", "x0", src_off, "ldr");
                            self.emit_store_to_raw_sp("x1", stack_offset + src_off, "str");
                        }
                        stack_offset += (n_dwords as i64) * 8;
                    }
                    CallArgClass::I128Stack => {
                        match arg {
                            Operand::Const(c) => {
                                if let IrConst::I128(v) = c {
                                    self.emit_load_imm64("x0", *v as u64 as i64);
                                    self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                    self.emit_load_imm64("x0", (*v >> 64) as u64 as i64);
                                    self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                                } else {
                                    self.operand_to_x0(arg);
                                    self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                    self.state.emit("    mov x0, #0");
                                    self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                                }
                            }
                            Operand::Value(v) => {
                                if let Some(slot) = self.state.get_slot(v.0) {
                                    let adjusted = slot.0 + src_adjust;
                                    if self.state.is_alloca(v.0) {
                                        self.emit_alloca_addr("x0", v.0, adjusted);
                                        self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                        self.state.emit("    mov x0, #0");
                                        self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                                    } else {
                                        self.emit_load_from_sp("x0", adjusted, "ldr");
                                        self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                        self.emit_load_from_sp("x0", adjusted + 8, "ldr");
                                        self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                                    }
                                } else {
                                    self.state.emit("    mov x0, #0");
                                    self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                    self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                                }
                            }
                        }
                        stack_offset += 16;
                    }
                    CallArgClass::F128Stack => {
                        match arg {
                            Operand::Const(c) => {
                                let bytes = match c {
                                    IrConst::LongDouble(_, f128_bytes) => *f128_bytes,
                                    _ => {
                                        let f64_val = c.to_f64().unwrap_or(0.0);
                                        crate::ir::reexports::f64_to_f128_bytes(f64_val)
                                    }
                                };
                                let lo = u64::from_le_bytes(bytes[0..8].try_into().unwrap());
                                let hi = u64::from_le_bytes(bytes[8..16].try_into().unwrap());
                                self.emit_load_imm64("x0", lo as i64);
                                self.emit_store_to_raw_sp("x0", stack_offset, "str");
                                self.emit_load_imm64("x0", hi as i64);
                                self.emit_store_to_raw_sp("x0", stack_offset + 8, "str");
                            }
                            Operand::Value(v) => {
                                let mut loaded_full = false;
                                if let Some((src_id, offset, is_indirect)) = self.state.get_f128_source(v.0) {
                                    if !is_indirect {
                                        if let Some(src_slot) = self.state.get_slot(src_id) {
                                            let adj = src_slot.0 + offset + src_adjust;
                                            self.emit_load_from_sp("q0", adj, "ldr");
                                            self.emit_store_to_raw_sp("q0", stack_offset, "str");
                                            loaded_full = true;
                                        }
                                    } else if let Some(src_slot) = self.state.get_slot(src_id) {
                                        let adj = src_slot.0 + src_adjust;
                                        self.emit_load_from_sp("x17", adj, "ldr");
                                        if offset != 0 {
                                            if offset > 0 && offset <= 4095 {
                                                self.state.emit_fmt(format_args!("    add x17, x17, #{}", offset));
                                            } else {
                                                self.load_large_imm("x16", offset);
                                                self.state.emit("    add x17, x17, x16");
                                            }
                                        }
                                        self.state.emit("    ldr q0, [x17]");
                                        self.emit_store_to_raw_sp("q0", stack_offset, "str");
                                        loaded_full = true;
                                    }
                                }
                                if !loaded_full {
                                    if let Some(&reg) = self.reg_assignments.get(&v.0) {
                                        let reg_name = callee_saved_name(reg);
                                        self.state.emit_fmt(format_args!("    mov x0, {}", reg_name));
                                    } else if let Some(slot) = self.state.get_slot(v.0) {
                                        let adjusted = slot.0 + src_adjust;
                                        if self.state.is_alloca(v.0) {
                                            self.emit_alloca_addr("x0", v.0, adjusted);
                                        } else {
                                            self.emit_load_from_sp("x0", adjusted, "ldr");
                                        }
                                    } else {
                                        self.state.emit("    mov x0, #0");
                                    }
                                    self.state.emit("    fmov d0, x0");
                                    self.state.emit("    stp x9, x10, [sp, #-16]!");
                                    self.state.emit("    bl __extenddftf2");
                                    self.state.emit("    ldp x9, x10, [sp], #16");
                                    self.emit_store_to_raw_sp("q0", stack_offset, "str");
                                }
                            }
                        }
                        stack_offset += 16;
                    }
                    CallArgClass::Stack | CallArgClass::StackPacked { .. } => {
                        match arg {
                            Operand::Value(v) => {
                                if let Some(&reg) = self.reg_assignments.get(&v.0) {
                                    let reg_name = callee_saved_name(reg);
                                    self.state.emit_fmt(format_args!("    mov x0, {}", reg_name));
                                } else if let Some(slot) = self.state.get_slot(v.0) {
                                    let adjusted = slot.0 + src_adjust;
                                    if self.state.is_alloca(v.0) {
                                        self.emit_alloca_addr("x0", v.0, adjusted);
                                    } else {
                                        self.emit_load_from_sp("x0", adjusted, "ldr");
                                    }
                                } else {
                                    self.state.emit("    mov x0, #0");
                                }
                            }
                            Operand::Const(_) => { self.operand_to_x0(arg); }
                        }
                        let (reg, instr) = match cls.stack_bytes() {
                            1 => ("w0", "strb"),
                            2 => ("w0", "strh"),
                            4 => ("w0", "str"),
                            _ => ("x0", "str"),
                        };
                        self.emit_store_to_raw_sp(reg, stack_offset, instr);
                        stack_offset += cls.stack_bytes() as i64;
                    }
                    _ => {}
                }
            }
        }
        stack_arg_space as i64 + fptr_spill as i64
    }

    pub(super) fn emit_call_reg_args_impl(&mut self, args: &[Operand], arg_classes: &[CallArgClass],
                          arg_types: &[IrType], total_sp_adjust: i64, _f128_temp_space: usize, _stack_arg_space: usize,
                          _struct_arg_riscv_float_classes: &[Option<crate::common::types::RiscvFloatClass>]) {
        let slot_adjust = if self.state.has_dyn_alloca { 0 } else { total_sp_adjust };
        let needs_adjusted_load = total_sp_adjust > 0;

        // Register args only (scalars, HFAs, a skipped HFA result pointer):
        // no source can live in x0-x7 (functions with calls never allocate
        // them), so load each GP arg straight into its register, after the
        // HFAs (through x17) and the FP args (which use x0 as scratch) and in
        // descending order (constants go through x0). Unlike staging through
        // x9-x16, this leaves the caller-saved pool intact, which
        // liveness::is_reg_args_only_call relies on.
        if reg_args_only(arg_classes) {
            self.emit_call_struct_byval_reg_args(args, arg_classes, slot_adjust, needs_adjusted_load);
            self.emit_call_fp_reg_args(args, arg_classes, arg_types, slot_adjust, needs_adjusted_load);
            let gp_args: Vec<usize> = (0..args.len())
                .filter(|&i| matches!(arg_classes[i], CallArgClass::IntReg { .. }))
                .collect();
            for (reg_idx, &i) in gp_args.iter().enumerate().rev() {
                if reg_idx < 8 {
                    self.emit_load_arg_to_reg(&args[i], ARM_ARG_REGS[reg_idx], slot_adjust, 0, needs_adjusted_load);
                }
            }
            return;
        }

        self.emit_call_gp_to_temps(args, arg_classes, slot_adjust, needs_adjusted_load);
        self.emit_call_fp_reg_args(args, arg_classes, arg_types, slot_adjust, needs_adjusted_load);
        self.emit_call_move_temps_to_arg_regs(args, arg_classes);
        self.emit_call_i128_reg_args(args, arg_classes, slot_adjust, needs_adjusted_load);
        self.emit_call_struct_byval_reg_args(args, arg_classes, slot_adjust, needs_adjusted_load);
    }

    /// Tell the peephole optimizer which GP argument registers the call
    /// reads (see peephole::CALL_USES_PREFIX); without it a call reads x0-x8.
    pub(super) fn note_call_arg_regs_impl(&mut self, mask: u32) {
        self.state.emit_fmt(format_args!("{}{:#x}", super::peephole::CALL_USES_PREFIX, mask));
    }

    /// Where an indirect call can read `func_ptr` at the `blr` without
    /// spilling it first: a callee-saved register, a caller-saved one when
    /// the arguments all go straight into their registers (the other paths
    /// stage them through x9-x16), or its stack slot.
    pub(super) fn fptr_source_for(&self, func_ptr: &Operand, arg_classes: &[CallArgClass]) -> FptrSource {
        let Operand::Value(v) = func_ptr else { return FptrSource::Spilled };
        if let Some(&reg) = self.reg_assignments.get(&v.0) {
            if reg.0 >= 19 || reg_args_only(arg_classes) {
                return FptrSource::Reg(reg);
            }
            return FptrSource::Spilled;
        }
        match self.state.get_slot(v.0) {
            Some(slot) if !self.state.is_alloca(v.0) => FptrSource::Slot(slot.0),
            _ => FptrSource::Spilled,
        }
    }

    pub(super) fn emit_call_instruction_impl(&mut self, direct_name: Option<&str>, _func_ptr: Option<&Operand>, indirect: bool, stack_arg_space: usize) {
        if let Some(name) = direct_name {
            self.state.emit_fmt(format_args!("    bl {}", name));
        } else if let (true, FptrSource::Reg(reg)) = (indirect, self.fptr_source) {
            self.state.emit_fmt(format_args!("    blr {}", callee_saved_name(reg)));
        } else if let (true, FptrSource::Slot(slot)) = (indirect, self.fptr_source) {
            let adjust = if self.state.has_dyn_alloca { 0 } else { stack_arg_space as i64 };
            self.emit_load_from_sp("x17", slot + adjust, "ldr");
            self.state.emit("    blr x17");
        } else if indirect {
            let spill_offset = stack_arg_space as i64;
            if Self::is_valid_imm_offset(spill_offset, "ldr", "x17") {
                self.state.emit_fmt(format_args!("    ldr x17, [sp, #{}]", spill_offset));
            } else {
                self.load_large_imm("x17", spill_offset);
                self.state.emit("    add x17, sp, x17");
                self.state.emit("    ldr x17, [x17]");
            }
            self.state.emit("    blr x17");
        }
    }

    pub(super) fn emit_call_cleanup_impl(&mut self, stack_arg_space: usize, _f128_temp_space: usize, indirect: bool) {
        let fptr_spill = if indirect { self.emit_call_fptr_spill_size() } else { 0 };
        self.fptr_source = FptrSource::Spilled;
        let total = stack_arg_space + fptr_spill;
        if total > 0 {
            self.emit_add_sp(total as i64);
        }
    }

    pub(super) fn emit_call_store_i128_result_impl(&mut self, dest: &Value) {
        self.store_x0_x1_to(dest);
    }

    pub(super) fn emit_call_store_f128_result_impl(&mut self, dest: &Value) {
        if let Some(slot) = self.state.get_slot(dest.0) {
            self.emit_store_to_sp("q0", slot.0, "str");
            self.state.track_f128_self(dest.0);
        }
        self.state.emit("    sub sp, sp, #16");
        self.state.emit("    str q1, [sp]");
        self.state.emit("    bl __trunctfdf2");
        self.state.emit("    fmov x0, d0");
        self.state.emit("    ldr q1, [sp]");
        self.state.emit("    add sp, sp, #16");
        self.state.reg_cache.invalidate_all();
        self.state.reg_cache.set_acc(dest.0, false);
    }
}

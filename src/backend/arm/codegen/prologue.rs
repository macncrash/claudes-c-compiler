//! ArmCodegen: prologue/epilogue and stack frame operations.

use crate::ir::reexports::{IrFunction, Instruction, Operand, Terminator, Value};
use crate::common::fx_hash::FxHashSet;
use crate::common::types::IrType;
use crate::backend::generation::{calculate_stack_space_common, find_param_alloca};
use crate::backend::call_abi::{ParamClass, classify_params};
use super::peephole;
use super::emit::{
    ArmCodegen, callee_saved_name, callee_saved_name_32, ARM_CALLEE_SAVED, ARM_CALLER_SAVED, ARM_ARG_REGS,
    ARM_FP_CALLEE_SAVED, ARM_FP_CALLER_SAVED, ARM_FP_REG_ID_BASE,
};

impl ArmCodegen {
    // ---- calculate_stack_space ----

    pub(super) fn calculate_stack_space_impl(&mut self, func: &IrFunction) -> i64 {
        use crate::ir::reexports::Instruction;
        use crate::backend::regalloc::PhysReg;

        let mut asm_clobbered_regs: Vec<PhysReg> = Vec::new();
        Self::prescan_inline_asm_callee_saved(func, &mut asm_clobbered_regs);
        let mut temp_regs = Self::assign_vec_temp_regs(func);
        let const_count = Self::assign_vec_const_regs(func, &super::emit::ARM_VEC_VAR_REGS).len();
        let (var_regs, vars, lanes, fresh, spills) =
            Self::assign_vec_var_regs(func, &temp_regs, &self.call_abi_config_impl(), const_count);
        self.vec_fresh_stores = fresh;
        self.vec_spills = spills;
        Self::fold_vec_temp_copies(func, &mut temp_regs, &var_regs, &lanes);
        Self::fold_vec_temps_into_v0(func, &mut temp_regs);
        self.vec_regs = var_regs.into_iter().chain(temp_regs).collect();
        self.vec_vars = vars.into_iter().map(|(reg, size)| (reg, size, 0)).collect();
        self.vec_param_loads = func.param_alloca_values.iter()
            .filter_map(|p| {
                let reg = *self.vec_regs.get(&p.0)?;
                let size = self.vec_vars.iter().find(|v| v.0 == reg)?.1;
                Some((*p, reg, size))
            })
            .collect();
        self.vec_lanes = lanes;
        self.vec_not_folds = Self::fold_vec_nots(func, &self.vec_regs, &self.vec_lanes);
        (self.vec_selects, self.vec_select_parts) = Self::fold_vec_selects(func, &self.vec_regs, &self.vec_lanes, &self.vec_not_folds);
        self.vec_shift_adds = Self::fold_vec_shift_adds(func, &self.vec_regs, &self.vec_lanes);
        self.vec_xor3 = Self::fold_vec_xor3(func, &self.vec_regs, &self.vec_lanes);
        let free: Vec<u8> = super::emit::ARM_VEC_VAR_REGS.iter().copied()
            .filter(|r| !self.vec_regs.values().any(|v| v == r))
            .collect();
        self.vec_consts = Self::assign_vec_const_regs(func, &free);
        (self.vec_consts_on_entry, self.vec_const_rebuilds, self.vec_const_block_rebuilds) =
            Self::vec_const_rebuilds(func, &self.vec_consts);
        let base_regs: &[PhysReg] = if func.is_variadic { &[] } else { &ARM_CALLEE_SAVED };
        let available_regs = crate::backend::generation::filter_available_regs(base_regs, &asm_clobbered_regs);

        let mut caller_saved_regs: Vec<PhysReg> = if func.is_variadic {
            Vec::new()
        } else {
            ARM_CALLER_SAVED.to_vec()
        };
        let mut has_f128_ops = false;
        for block in &func.blocks {
            for inst in &block.instructions {
                match inst {
                    Instruction::BinOp { ty, .. } | Instruction::UnaryOp { ty, .. }
                    | Instruction::Cmp { ty, .. } | Instruction::Load { ty, .. }
                    | Instruction::Store { ty, .. } if *ty == IrType::F128 => {
                        has_f128_ops = true;
                    }
                    Instruction::Cast { to_ty, .. } if *to_ty == IrType::F128 => {
                        has_f128_ops = true;
                    }
                    Instruction::Cast { from_ty, .. } if *from_ty == IrType::F128 => {
                        has_f128_ops = true;
                    }
                    _ => {}
                }
            }
        }
        if has_f128_ops {
            caller_saved_regs.clear();
        }
        let leaf_extra = if has_f128_ops { Vec::new() } else { Self::leaf_extra_caller_saved(func, &self.call_abi_config_impl()) };
        caller_saved_regs.extend_from_slice(&leaf_extra);

        let (mut reg_assigned, mut cached_liveness) = crate::backend::generation::run_regalloc_and_merge_clobbers(
            func, available_regs, caller_saved_regs, &asm_clobbered_regs,
            &mut self.reg_assignments, &mut self.used_callee_saved,
            false,
            true, // argument setup leaves x10-x15 intact (calls.rs)
            true, // memcpy reads register-assigned pointers (memory.rs)
            true, // simple ops read their operands before writing the dest
        );

        // FP registers for F32/F64 values. Skipped for variadic functions
        // (like the GPR pools) and under -mgeneral-regs-only.
        self.fp_reg_assignments.clear();
        self.used_fp_callee_saved.clear();
        if !func.is_variadic && !self.general_regs_only {
            let liveness = cached_liveness.take()
                .unwrap_or_else(|| crate::backend::liveness::compute_live_intervals(func));
            let config = crate::backend::regalloc::FpRegAllocConfig {
                callee_saved_regs: ARM_FP_CALLEE_SAVED.to_vec(),
                // Registers holding vector values are not for scalars.
                caller_saved_regs: ARM_FP_CALLER_SAVED.iter().copied()
                    .filter(|r| !self.vec_regs.values().any(|&v| v == r.0)
                        && !self.vec_consts.iter().any(|c| c.3 == r.0))
                    .collect(),
            };
            let fp = crate::backend::regalloc::allocate_fp_registers(func, &liveness, &config);
            cached_liveness = Some(liveness);
            // Stack layout must treat these like register-allocated values:
            // no stack slot, and never coalesced with another value's slot.
            for (&id, &(reg, _)) in &fp.assignments {
                reg_assigned.insert(id, PhysReg(ARM_FP_REG_ID_BASE + reg.0));
            }
            self.fp_reg_assignments = fp.assignments;
            self.used_fp_callee_saved = fp.used_callee_saved;
        }

        self.prestore_regs = ARM_CALLEE_SAVED.to_vec();
        if Self::params_can_prestore_caller_saved(func, &self.call_abi_config_impl()) {
            // The leaf extras are never incoming argument registers either.
            self.prestore_regs.extend_from_slice(&ARM_CALLER_SAVED);
            self.prestore_regs.extend_from_slice(&leaf_extra);
        }
        let prestore_regs = self.prestore_regs.clone();
        // The callee-saved registers go right above the frame record, below
        // the locals, so their stp/ldp offsets stay small (stp reaches 504)
        // however large the frame is.
        self.callee_save_offset = 16;
        self.fp_callee_save_offset = (16 + self.used_callee_saved.len() as i64 * 8 + 7) & !7;
        let saves_end = self.fp_callee_save_offset + (self.used_fp_callee_saved.len() as i64 + 1) / 2 * 16;
        let locals_start = (saves_end + 15) & !15;
        let mut space = calculate_stack_space_common(&mut self.state, func, locals_start, |space, alloc_size, align| {
            let effective_align = if align > 0 { align.max(8) } else { 8 };
            let slot = (space + effective_align - 1) & !(effective_align - 1);
            let new_space = slot + ((alloc_size + 7) & !7).max(8);
            (slot, new_space)
        }, &reg_assigned, &prestore_regs, cached_liveness, false);

        // Save slots for vector variables across calls.
        if !self.vec_vars.is_empty() {
            space = (space + 15) & !15;
            for var in &mut self.vec_vars {
                var.2 = space;
                space += 16;
            }
        }

        if func.is_variadic {
            space = (space + 7) & !7;
            self.va_gp_save_offset = space;
            space += 64;

            if !self.general_regs_only {
                space = (space + 15) & !15;
                self.va_fp_save_offset = space;
                space += 128;
            }

            let config = self.call_abi_config_impl();
            let classification = crate::backend::call_abi::classify_params_full(func, &config);
            let param_classes = classification.classes;
            let mut named_gp = 0usize;
            let mut named_fp = 0usize;
            for (i, class) in param_classes.iter().enumerate() {
                // On ARM64, the sret pointer goes in x8 (a dedicated register),
                // NOT in x0-x7. Don't count it as consuming a GP argument register,
                // otherwise va_start computes the wrong __gr_offs and skips the
                // first variadic argument.
                if self.state.uses_sret && i == 0 {
                    continue;
                }
                named_gp += class.gp_reg_count();
                match class {
                    crate::backend::call_abi::ParamClass::FloatReg { .. }
                    | crate::backend::call_abi::ParamClass::F128FpReg { .. } => named_fp += 1,
                    crate::backend::call_abi::ParamClass::StructHfaReg { count, .. } => named_fp += count,
                    _ => {}
                }
            }
            self.va_named_gp_count = named_gp.min(8);
            self.va_named_fp_count = named_fp.min(8);
            // Unnamed args start at the next 8-byte slot after the named ones
            // (which Apple arm64 packs, so their end need not be a slot boundary).
            self.va_named_stack_bytes = classification.total_stack_bytes.next_multiple_of(8);
        }

        // HFA return: the caller passes no sret pointer, so reserve the buffer
        // here (up to four doubles); the param stores point x8 at it.
        self.hfa_ret = None;
        if func.uses_sret {
            if let Some(crate::common::types::RiscvFloatClass::Hfa { count, elem_size }) =
                func.params.first().and_then(|p| p.riscv_float_class)
            {
                space = (space + 15) & !15;
                self.hfa_ret = Some((count, elem_size, space));
                space += 32;
            }
        }
        self.hfa_ret_copies.clear();
        self.hfa_ret_loaded = false;
        if let Some((count, elem_size, _)) = self.hfa_ret {
            for block in &func.blocks {
                let Terminator::Return(Some(Operand::Value(ret))) = &block.terminator else { continue };
                if let Some(Instruction::Memcpy { dest, src, size }) = block.instructions.last() {
                    if dest == ret && *size == count * elem_size {
                        self.hfa_ret_copies.insert((dest.0, src.0));
                    }
                }
            }
        }
        self.hfa_ret_ptr_loads = None;
        if let (Some(_), Some((ptr, _))) = (self.hfa_ret, find_param_alloca(func, 0)) {
            self.hfa_ret_ptr_loads = Self::hfa_ret_ptr_only_loaded(func, ptr, &self.hfa_ret_copies);
        }
        // Those loads are never emitted: free their registers (a callee-saved
        // one need not be saved if nothing else has it).
        for v in self.hfa_ret_ptr_loads.iter().flatten() {
            if let Some(r) = self.reg_assignments.remove(v) {
                if !self.reg_assignments.values().any(|x| *x == r) {
                    self.used_callee_saved.retain(|x| *x != r);
                }
            }
        }

        space
    }

    // ---- aligned_frame_size ----

    pub(super) fn aligned_frame_size_impl(&self, raw_space: i64) -> i64 {
        (raw_space + 15) & !15
    }

    // ---- emit_prologue ----

    pub(super) fn emit_prologue_impl(&mut self, func: &IrFunction, frame_size: i64) {
        self.current_return_type = func.return_type;
        self.current_frame_size = frame_size;
        self.frame_base_offset = None;
        self.emit_prologue_arm(frame_size);
        // Tells the peephole optimizer how many of x0/x1 carry the return
        // value at `ret`; it always removes this line.
        let ret_regs = match func.return_type {
            // (An HFA comes back in v0-v3, not through the sret pointer.)
            _ if self.hfa_ret.is_some() => Some(peephole::RET_NONE_MARKER),
            IrType::Void | IrType::F32 | IrType::F64 | IrType::F128 => Some(peephole::RET_NONE_MARKER),
            IrType::I8 | IrType::U8 | IrType::I16 | IrType::U16 | IrType::I32 | IrType::U32
            | IrType::I64 | IrType::U64 | IrType::Ptr if !func.uses_sret => Some(peephole::RET_X0_MARKER),
            _ => None,
        };
        if let Some(marker) = ret_regs {
            self.state.emit(marker);
        }

        let used_regs = self.used_callee_saved.clone();
        let base = self.callee_save_offset;
        let n = used_regs.len();
        let mut i = 0;
        while i + 1 < n {
            let r1 = callee_saved_name(used_regs[i]);
            let r2 = callee_saved_name(used_regs[i + 1]);
            let offset = base + (i as i64) * 8;
            self.emit_stp_to_sp(r1, r2, offset);
            i += 2;
        }
        if i < n {
            let r = callee_saved_name(used_regs[i]);
            let offset = base + (i as i64) * 8;
            self.emit_store_to_sp(r, offset, "str");
        }
        self.emit_save_restore_fp_callee_saved(true);
    }

    // ---- emit_epilogue ----

    pub(super) fn emit_epilogue_impl(&mut self, frame_size: i64) {
        self.emit_restore_callee_saved();
        self.emit_epilogue_arm(frame_size);
    }

    // ---- emit_store_params ----

    /// Argument registers x3-x8 that a function can use as extra caller-saved
    /// registers. Codegen only uses them as scratch for calls (argument
    /// setup), division (x3), atomics, i128 arithmetic, variadics, and inline
    /// asm, so a function with none of those may allocate the ones above its
    /// own incoming GP arguments (and x8 unless it is the sret pointer).
    fn leaf_extra_caller_saved(func: &IrFunction, config: &crate::backend::call_abi::CallAbiConfig) -> Vec<crate::backend::regalloc::PhysReg> {
        use crate::ir::reexports::{Instruction, IrBinOp};
        use crate::backend::regalloc::PhysReg;
        if func.is_variadic {
            return Vec::new();
        }
        let wide = |ty: &IrType| matches!(ty, IrType::I128 | IrType::U128 | IrType::F128);
        let mut has_div = false;
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            match inst {
                Instruction::BinOp { op, ty, .. } => {
                    if wide(ty) {
                        return Vec::new();
                    }
                    has_div |= matches!(op, IrBinOp::SDiv | IrBinOp::UDiv | IrBinOp::SRem | IrBinOp::URem);
                }
                Instruction::Cast { from_ty, to_ty, .. } if wide(from_ty) || wide(to_ty) => return Vec::new(),
                Instruction::Load { ty, .. } | Instruction::Store { ty, .. }
                | Instruction::UnaryOp { ty, .. } | Instruction::Cmp { ty, .. } if wide(ty) => return Vec::new(),
                Instruction::Call { .. } | Instruction::CallIndirect { .. } | Instruction::InlineAsm { .. }
                | Instruction::Intrinsic { .. } | Instruction::AtomicLoad { .. } | Instruction::AtomicStore { .. }
                | Instruction::AtomicRmw { .. } | Instruction::AtomicCmpxchg { .. } | Instruction::Fence { .. }
                | Instruction::VaArg { .. } | Instruction::VaStart { .. } | Instruction::VaEnd { .. }
                | Instruction::VaCopy { .. } | Instruction::DynAlloca { .. } => return Vec::new(),
                _ => {}
            }
        }
        // First GP register not carrying an incoming argument.
        let mut first_free = 0usize;
        for class in classify_params(func, config) {
            let last = match class {
                ParamClass::IntReg { reg_idx } | ParamClass::LargeStructByRefReg { reg_idx, .. } => reg_idx,
                ParamClass::I128RegPair { base_reg_idx } => base_reg_idx + 1,
                ParamClass::StructByValReg { base_reg_idx, size } => base_reg_idx + usize::from(size > 8),
                c if c.uses_gp_reg() => return Vec::new(),
                _ => continue,
            };
            first_free = first_free.max(last + 1);
        }
        let first = first_free.max(if has_div { 4 } else { 3 });
        let last = if func.uses_sret { 7 } else { 8 };
        (first..=last).map(|r| PhysReg(r as u8)).collect()
    }

    /// Whether ParamRef dests may live in caller-saved registers (x10-x15)
    /// from function entry. Those registers are not argument registers, but
    /// the prologue only keeps them intact, and the earlier start of the
    /// live range only collides with nothing, when:
    /// - every ParamRef is in the entry block, preceded only by allocas and
    ///   other ParamRefs, and
    /// - no param is copied with x10 (large struct by reference on the stack)
    ///   or converted with a libcall (f128 in an FP register).
    fn params_can_prestore_caller_saved(func: &IrFunction, config: &crate::backend::call_abi::CallAbiConfig) -> bool {
        use crate::ir::reexports::Instruction;
        if func.is_variadic {
            return false;
        }
        let classes = classify_params(func, config);
        if classes.iter().any(|c| matches!(c, ParamClass::F128FpReg { .. } | ParamClass::LargeStructByRefStack { .. })) {
            return false;
        }
        let Some(entry) = func.blocks.first() else { return false };
        let in_prefix = entry.instructions.iter()
            .take_while(|i| matches!(i, Instruction::Alloca { .. } | Instruction::ParamRef { .. }))
            .filter(|i| matches!(i, Instruction::ParamRef { .. }))
            .count();
        let total = func.blocks.iter()
            .flat_map(|b| &b.instructions)
            .filter(|i| matches!(i, Instruction::ParamRef { .. }))
            .count();
        in_prefix == total
    }

    /// The loads of the sret pointer from its slot `ptr`, if those loads are
    /// its only uses and the loaded pointer is only the destination of
    /// `copies` (which load the result registers from their source) or
    /// returned (which an HFA return ignores).
    fn hfa_ret_ptr_only_loaded(func: &IrFunction, ptr: Value, copies: &FxHashSet<(u32, u32)>)
                               -> Option<FxHashSet<u32>> {
        let mut loads = FxHashSet::default();
        for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
            match inst {
                Instruction::Load { dest, ptr: p, .. } if *p == ptr => {
                    loads.insert(dest.0);
                }
                _ if inst.used_values().contains(&ptr.0) => return None,
                _ => {}
            }
        }
        for block in &func.blocks {
            for inst in &block.instructions {
                if let Instruction::Memcpy { dest, src, .. } = inst {
                    if copies.contains(&(dest.0, src.0)) {
                        continue;
                    }
                }
                if matches!(inst, Instruction::Load { ptr: p, .. } if *p == ptr) {
                    continue;
                }
                if inst.used_values().iter().any(|v| *v == ptr.0 || loads.contains(v)) {
                    return None;
                }
            }
            match &block.terminator {
                Terminator::Return(Some(Operand::Value(v))) if loads.contains(&v.0) => {}
                t if t.used_values().iter().any(|v| *v == ptr.0 || loads.contains(v)) => return None,
                _ => {}
            }
        }
        Some(loads)
    }

    pub(super) fn emit_store_params_impl(&mut self, func: &IrFunction) {
        if func.is_variadic {
            self.emit_save_variadic_regs();
        }
        // HFA return: point the sret pointer (read from x8 below) at the
        // frame buffer the return loads from.
        if let Some((_, _, buf_offset)) = self.hfa_ret.filter(|_| self.hfa_ret_ptr_loads.is_none()) {
            self.emit_add_sp_offset("x8", buf_offset);
        }

        let config = self.call_abi_config_impl();
        let param_classes = classify_params(func, &config);
        self.state.param_classes = param_classes.clone();
        self.state.num_params = func.params.len();
        self.state.func_is_variadic = func.is_variadic;

        self.state.param_alloca_slots = (0..func.params.len()).map(|i| {
            find_param_alloca(func, i).and_then(|(dest, ty)| {
                self.state.get_slot(dest.0).map(|slot| (slot, ty))
            })
        }).collect();

        // Pre-store optimization: when a GP param's alloca is dead (promoted by
        // mem2reg) but the ParamRef dest is register-allocated to a callee-saved
        // register, store the ABI arg register directly to that callee-saved
        // register in the prologue.  This is critical because:
        // 1. Dead alloca means no stack slot exists for this param
        // 2. The ABI register (x0-x7) will be clobbered by subsequent codegen
        //    (ARM uses x0 as the universal scratch/result register)
        // 3. We must save the value NOW, before any other code runs
        // 4. emit_param_ref will see param_pre_stored and skip code generation
        let sret_shift = if self.state.uses_sret { 1usize } else { 0 };
        let mut paramref_dests: Vec<Option<Value>> = vec![None; func.params.len()];
        for block in &func.blocks {
            for inst in &block.instructions {
                if let Instruction::ParamRef { dest, param_idx, .. } = inst {
                    if *param_idx < paramref_dests.len() {
                        paramref_dests[*param_idx] = Some(*dest);
                    }
                }
            }
        }
        // Build a map from physical register -> list of param indices that use it,
        // so we can detect when two params share the same callee-saved register.
        let mut reg_to_params: crate::common::fx_hash::FxHashMap<u8, Vec<usize>> = crate::common::fx_hash::FxHashMap::default();
        for (i, _) in func.params.iter().enumerate() {
            if let Some(paramref_dest) = paramref_dests[i] {
                if let Some(&phys_reg) = self.reg_assignments.get(&paramref_dest.0) {
                    reg_to_params.entry(phys_reg.0).or_default().push(i);
                }
            }
        }

        for (i, _) in func.params.iter().enumerate() {
            let class = param_classes[i];
            if !class.uses_gp_reg() { continue; }
            // Skip params that have an alloca slot (they'll be handled by emit_store_gp_params)
            let has_slot = self.state.param_alloca_slots.get(i)
                .and_then(|opt| opt.as_ref())
                .is_some();
            if has_slot { continue; }

            if let Some(paramref_dest) = paramref_dests[i] {
                if let Some(&phys_reg) = self.reg_assignments.get(&paramref_dest.0) {
                    // Only pre-store to callee-saved registers, and to
                    // caller-saved ones when params_can_prestore_caller_saved
                    // allows it.
                    if self.prestore_regs.contains(&phys_reg) {
                        // Safety check: if another param's dest is also assigned
                        // to this register, skip pre-store to avoid conflicts.
                        // The register allocator may assign the same register to
                        // two params whose live ranges don't overlap, but pre-store
                        // extends the effective lifetime to function entry.
                        if let Some(users) = reg_to_params.get(&phys_reg.0) {
                            if users.len() > 1 {
                                continue;
                            }
                        }
                        let dest_reg = callee_saved_name(phys_reg);
                        if let ParamClass::IntReg { reg_idx } = class {
                                let actual_idx = if sret_shift > 0 && reg_idx == 0 && i == 0 {
                                    // sret: the pointer comes in x8
                                    self.state.emit_fmt(format_args!(
                                        "    mov {}, x8", dest_reg));
                                    self.state.param_pre_stored.insert(i);
                                    continue;
                                } else if reg_idx >= sret_shift {
                                    reg_idx - sret_shift
                                } else {
                                    reg_idx
                                };
                                let src_reg = ARM_ARG_REGS[actual_idx];
                                self.state.emit_fmt(format_args!(
                                    "    mov {}, {}", dest_reg, src_reg));
                                self.state.param_pre_stored.insert(i);
                        }
                    }
                }
            }
        }

        self.emit_store_gp_params(func, &param_classes);
        self.emit_store_fp_params(func, &param_classes);
        self.emit_store_stack_params(func, &param_classes);
        self.emit_vec_param_loads();
        self.emit_vec_consts(self.vec_consts_on_entry);
    }

    // ---- emit_param_ref ----

    pub(super) fn emit_param_ref_impl(&mut self, dest: &Value, param_idx: usize, ty: IrType) {
        if param_idx >= self.state.param_classes.len() {
            return;
        }

        // If this param was pre-stored directly to its register-allocated
        // destination during emit_store_params, the value is already in place.
        // No code needs to be emitted — the register already holds the value.
        if self.state.param_pre_stored.contains(&param_idx) {
            return;
        }

        if param_idx < self.state.param_alloca_slots.len() {
            if let Some((slot, alloca_ty)) = self.state.param_alloca_slots[param_idx] {
                let ldr_instr = self.load_instr_for_type_impl(alloca_ty);
                let (actual_instr, reg) = Self::arm_parse_load(ldr_instr);
                self.emit_load_from_sp(reg, slot.0, actual_instr);
                self.store_x0_to(dest);
                return;
            }
        }

        let class = self.state.param_classes[param_idx];
        let frame_size = self.current_frame_size;

        // AArch64 ABI: sret shifts GP register indices
        let sret_shift = if self.state.uses_sret { 1usize } else { 0 };

        match class {
            ParamClass::IntReg { reg_idx } => {
                let actual_reg = if sret_shift > 0 && reg_idx == 0 && param_idx == 0 {
                    Self::reg_for_type("x8", ty)
                } else {
                    let actual_idx = if reg_idx >= sret_shift { reg_idx - sret_shift } else { reg_idx };
                    Self::reg_for_type(ARM_ARG_REGS[actual_idx], ty)
                };
                // Copy straight into an assigned register; going through x0
                // would clobber param 0 if it has not been read yet.
                if let Some(&phys) = self.reg_assignments.get(&dest.0) {
                    let dst = if actual_reg.starts_with('w') { callee_saved_name_32(phys) } else { callee_saved_name(phys) };
                    self.state.emit_fmt(format_args!("    mov {}, {}", dst, actual_reg));
                    self.state.reg_cache.invalidate_all();
                    return;
                }
                let dst = Self::reg_for_type("x0", ty);
                if actual_reg != dst {
                    self.state.emit_fmt(format_args!("    mov {}, {}", dst, actual_reg));
                }
                self.store_x0_to(dest);
            }
            ParamClass::FloatReg { reg_idx } => {
                if ty == IrType::F32 {
                    self.state.emit_fmt(format_args!("    fmov w0, s{}", reg_idx));
                } else {
                    self.state.emit_fmt(format_args!("    fmov x0, d{}", reg_idx));
                }
                self.store_x0_to(dest);
            }
            ParamClass::StackScalar { offset } => {
                let src = frame_size + offset;
                let ldr_instr = self.load_instr_for_type_impl(ty);
                let (actual_instr, reg) = Self::arm_parse_load(ldr_instr);
                self.emit_load_from_sp(reg, src, actual_instr);
                self.store_x0_to(dest);
            }
            _ => {}
        }
    }

    // ---- emit_epilogue_and_ret ----

    pub(super) fn emit_epilogue_and_ret_impl(&mut self, frame_size: i64) {
        self.emit_restore_callee_saved();
        self.emit_epilogue_arm(frame_size);
        self.state.emit("    ret");
    }

    // ---- store_instr_for_type / load_instr_for_type ----

    pub(super) fn store_instr_for_type_impl(&self, ty: IrType) -> &'static str {
        Self::str_for_type(ty)
    }

    pub(super) fn load_instr_for_type_impl(&self, ty: IrType) -> &'static str {
        match ty {
            IrType::I8 => "ldrsb",
            IrType::U8 => "ldrb",
            IrType::I16 => "ldrsh",
            IrType::U16 => "ldrh",
            IrType::I32 => "ldrsw",
            IrType::U32 | IrType::F32 => "ldr32",
            _ => "ldr64",
        }
    }
}

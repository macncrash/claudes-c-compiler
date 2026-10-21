//! AArch64 NEON/SIMD intrinsic emission and F128 (quad-precision) soft-float helpers.
//!
//! NEON intrinsics: SSE-equivalent operations via 128-bit NEON instructions.
//! F128: IEEE 754 binary128 via compiler-rt/libgcc soft-float libcalls.

use crate::ir::reexports::{CallInfo, Instruction, IntrinsicOp, IrConst, IrFunction, Operand, Terminator, Value, VecElem, VecOpKind};
use crate::backend::state::SlotAddr;
use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{IrType, RiscvFloatClass};
use super::emit::{ARM_VEC_TEMP_REGS, ARM_VEC_VAR_REGS};
use super::emit::ArmCodegen;
use crate::backend::call_abi::{CallAbiConfig, CallArgClass, classify_call_args};

impl ArmCodegen {
    pub(super) fn emit_neon_binary_128(&mut self, dest_ptr: &Value, args: &[Operand], neon_inst: &str) {
        // Load first 128-bit operand pointer into x0, then load q0
        self.operand_to_x0(&args[0]);
        self.state.emit("    ldr q0, [x0]");
        // Load second 128-bit operand pointer into x1, then load q1
        match &args[1] {
            Operand::Value(v) => {
                if let Some(slot) = self.state.get_slot(v.0) {
                    if self.state.is_alloca(v.0) {
                        self.emit_alloca_addr("x1", v.0, slot.0);
                    } else {
                        self.emit_load_from_sp("x1", slot.0, "ldr");
                    }
                }
            }
            Operand::Const(_) => {
                self.operand_to_x0(&args[1]);
                self.state.emit("    mov x1, x0");
            }
        }
        self.state.emit("    ldr q1, [x1]");
        // Apply the binary NEON operation
        self.state.emit_fmt(format_args!("    {} v0.16b, v0.16b, v1.16b", neon_inst));
        // Store result to dest_ptr
        self.load_ptr_to_reg(dest_ptr, "x0");
        self.state.emit("    str q0, [x0]");
    }

    /// Store a scalar result from x0 (or w0) into the dest stack slot.
    fn store_scalar_dest(&mut self, dest: &Option<Value>, reg: &str) {
        if let Some(d) = dest {
            if let Some(slot) = self.state.get_slot(d.0) {
                self.emit_store_to_sp(reg, slot.0, "str");
            }
        }
    }

    /// Emit a unary F64 operation: fmov to d0, apply `op_inst`, fmov back, store result.
    fn emit_f64_unary_neon(&mut self, dest: &Option<Value>, args: &[Operand], op_inst: &str) {
        self.operand_to_x0(&args[0]);
        self.state.emit("    fmov d0, x0");
        self.state.emit_fmt(format_args!("    {} d0, d0", op_inst));
        self.state.emit("    fmov x0, d0");
        self.store_scalar_dest(dest, "x0");
    }

    /// Emit a unary F32 operation: fmov to s0, apply `op_inst`, fmov back, store result.
    fn emit_f32_unary_neon(&mut self, dest: &Option<Value>, args: &[Operand], op_inst: &str) {
        self.operand_to_x0(&args[0]);
        self.state.emit("    fmov s0, w0");
        self.state.emit_fmt(format_args!("    {} s0, s0", op_inst));
        self.state.emit("    fmov w0, s0");
        self.store_scalar_dest(dest, "w0");
    }

    /// Emit a non-temporal store: load value from args[0], store to dest_ptr.
    fn emit_nontemporal_store(&mut self, dest_ptr: &Option<Value>, args: &[Operand], save_reg: &str, val_reg: &str) {
        if let Some(ptr) = dest_ptr {
            self.operand_to_x0(&args[0]);
            self.state.emit_fmt(format_args!("    mov {}, {}", save_reg, val_reg));
            self.load_ptr_to_reg(ptr, "x0");
            self.state.emit_fmt(format_args!("    str {}, [x0]", save_reg));
        }
    }

    /// Pick the vector variables that can live in a NEON register
    /// (ARM_VEC_VAR_REGS) for the whole function: an 8- or 16-byte local or
    /// parameter (not volatile or over-aligned) that some VecOp uses, or
    /// that is built lane by lane and then read whole (an initializer that
    /// is copied, returned or passed), and whose address never escapes.
    /// Every use of the alloca, or of a constant GEP from it, must be a
    /// whole read or write by a VecOp or Memcpy, a load or store of one
    /// aligned lane of up to 8 bytes, or a call argument passed in a SIMD
    /// register. Calls and other instructions that may clobber v16-v31
    /// spill and reload the register around themselves (spill_vec_vars). A
    /// parameter is loaded into its register once the prologue has stored
    /// it (emit_vec_param_loads).
    ///
    /// Temporaries (`temps`) are not candidates. Returns (alloca ->
    /// register, each variable's (register, size), lane pointer ->
    /// (register, offset), fresh_lane_stores, vec_vars_live_across).
    #[allow(clippy::type_complexity)]
    pub(super) fn assign_vec_var_regs(func: &IrFunction, temps: &FxHashMap<u32, u8>, config: &CallAbiConfig,
                                    const_count: usize)
        -> (FxHashMap<u32, u8>, Vec<(u8, u8)>, FxHashMap<u32, (u8, i64)>, FxHashSet<usize>, FxHashMap<usize, u32>)
    {
        let none = || (FxHashMap::default(), Vec::new(), FxHashMap::default(), FxHashSet::default(), FxHashMap::default());
        let insts = || func.blocks.iter().flat_map(|b| &b.instructions);
        if insts().any(|i| matches!(i, Instruction::InlineAsm { .. })) {
            return none();
        }
        let mut size_of: FxHashMap<u32, i64> = insts()
            .filter_map(|inst| match inst {
                Instruction::Alloca { dest, size: size @ (8 | 16), align, volatile: false, .. }
                    if *align <= 16 && !temps.contains_key(&dest.0) =>
                    Some((dest.0, *size as i64)),
                _ => None,
            })
            .collect();
        if size_of.is_empty() {
            return none();
        }
        // Pointers into a candidate: value -> (alloca, byte offset). GEPs may
        // chain and appear in any block order, so iterate to a fixed point.
        let mut lanes: FxHashMap<u32, (u32, i64)> = size_of.keys().map(|&id| (id, (id, 0))).collect();
        loop {
            let mut changed = false;
            for inst in insts() {
                if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = inst {
                    if let (Some(&(var, off)), Some(k), false) = (lanes.get(&base.0), c.to_i64(), lanes.contains_key(&dest.0)) {
                        lanes.insert(dest.0, (var, off + k));
                        changed = true;
                    }
                }
            }
            if !changed {
                break;
            }
        }
        let mut bad: FxHashSet<u32> = FxHashSet::default();
        let mut vec_used: FxHashSet<u32> = FxHashSet::default();
        let mut lane_built: FxHashSet<u32> = FxHashSet::default();
        let mut read_whole: FxHashSet<u32> = FxHashSet::default();
        let lane_ok = |lanes: &FxHashMap<u32, (u32, i64)>, size_of: &FxHashMap<u32, i64>, ptr: u32, ty: IrType| {
            let (var, off) = lanes[&ptr];
            let n = ty.size() as i64;
            matches!(n, 1 | 2 | 4 | 8) && !matches!(ty, IrType::F128 | IrType::I128 | IrType::U128)
                && off >= 0 && off % n == 0 && off + n <= size_of[&var]
        };
        let whole = |lanes: &FxHashMap<u32, (u32, i64)>, size_of: &FxHashMap<u32, i64>, ptr: u32, n: i64| {
            let (var, off) = lanes[&ptr];
            off == 0 && size_of[&var] == n
        };
        for inst in insts() {
            let mut reject = |id: u32| {
                if let Some(&(var, _)) = lanes.get(&id) {
                    bad.insert(var);
                }
            };
            match inst {
                Instruction::Load { ptr, ty, .. } if lanes.contains_key(&ptr.0) => {
                    if !lane_ok(&lanes, &size_of, ptr.0, *ty) {
                        reject(ptr.0);
                    }
                }
                Instruction::Store { val, ptr, ty, .. } => {
                    if let Operand::Value(v) = val {
                        reject(v.0);
                    }
                    if lanes.contains_key(&ptr.0) {
                        if lane_ok(&lanes, &size_of, ptr.0, *ty) {
                            lane_built.insert(lanes[&ptr.0].0);
                        } else {
                            reject(ptr.0);
                        }
                    }
                }
                Instruction::GetElementPtr { base, offset, .. } => {
                    if !matches!(offset, Operand::Const(_)) {
                        reject(base.0);
                    }
                    if let Operand::Value(v) = offset {
                        reject(v.0);
                    }
                }
                Instruction::Intrinsic { op: IntrinsicOp::VecOp { bytes, splat, .. }, dest_ptr, args, .. } => {
                    for (i, arg) in args.iter().enumerate() {
                        if let Operand::Value(v) = arg {
                            if lanes.contains_key(&v.0) {
                                if splat & (1 << i) == 0 && whole(&lanes, &size_of, v.0, *bytes as i64) {
                                    vec_used.insert(lanes[&v.0].0);
                                } else {
                                    reject(v.0);
                                }
                            }
                        }
                    }
                    if let Some(d) = dest_ptr {
                        if lanes.contains_key(&d.0) {
                            if whole(&lanes, &size_of, d.0, *bytes as i64) {
                                vec_used.insert(lanes[&d.0].0);
                            } else {
                                reject(d.0);
                            }
                        }
                    }
                }
                Instruction::Memcpy { dest, src, size } => {
                    for p in [dest.0, src.0] {
                        if lanes.contains_key(&p) && !whole(&lanes, &size_of, p, *size as i64) {
                            reject(p);
                        }
                    }
                    if lanes.contains_key(&src.0) {
                        read_whole.insert(lanes[&src.0].0);
                    }
                }
                // A variable passed whole in a SIMD register (see
                // emit_call_struct_byval_reg_args); anything else escapes.
                // Not with F128 arguments: converting those calls out after
                // the variables are spilled, which may clobber them.
                Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. } => {
                    if let Instruction::CallIndirect { func_ptr: Operand::Value(v), .. } = inst {
                        reject(v.0);
                    }
                    let classes = classify_call_args(&info.args, &info.arg_types, &info.struct_arg_sizes,
                        &info.struct_arg_aligns, &info.struct_arg_classes, &info.struct_arg_riscv_float_classes,
                        info.is_variadic, config);
                    let has_f128 = info.arg_types.contains(&IrType::F128);
                    for (i, arg) in info.args.iter().enumerate() {
                        let Operand::Value(v) = arg else { continue };
                        if !lanes.contains_key(&v.0) {
                            continue;
                        }
                        let in_reg = matches!(classes.get(i), Some(CallArgClass::StructHfaReg { count: 1, .. }))
                            && !(info.is_sret && i == 0) && !has_f128
                            && size_of.contains_key(&v.0)
                            && info.struct_arg_sizes.get(i) == Some(&Some(size_of[&v.0] as usize));
                        if in_reg {
                            read_whole.insert(v.0);
                        } else {
                            reject(v.0);
                        }
                    }
                }
                Instruction::Alloca { .. } => {}
                other => other.for_each_used_value(|id| reject(id)),
            }
        }
        for block in &func.blocks {
            block.terminator.for_each_used_value(|id| {
                if let Some(&(var, _)) = lanes.get(&id) {
                    bad.insert(var);
                }
            });
        }
        size_of.retain(|id, _| {
            (vec_used.contains(id) || lane_built.contains(id) && read_whole.contains(id)) && !bad.contains(id)
        });
        // Variables a VecOp uses come first; the initializer-only ones get
        // what is left after `const_count` hoisted constant broadcasts.
        let mut vars: Vec<(bool, u32, i64)> = size_of.iter()
            .map(|(&id, &size)| (!vec_used.contains(&id), id, size))
            .collect();
        vars.sort_unstable();
        let primary = vars.iter().filter(|v| !v.0).count();
        let limit = primary.max(ARM_VEC_VAR_REGS.len().saturating_sub(const_count));
        let mut regs = FxHashMap::default();
        let mut var_list = Vec::new();
        for ((_, id, size), &reg) in vars.into_iter().zip(ARM_VEC_VAR_REGS.iter()).take(limit) {
            regs.insert(id, reg);
            var_list.push((reg, size as u8));
        }
        let fresh = Self::fresh_lane_stores(func, &lanes, &size_of, &regs);
        let spills = Self::vec_vars_live_across(func, &lanes, &regs, &fresh);
        let lane_regs = lanes.into_iter()
            .filter_map(|(ptr, (var, off))| regs.get(&var).map(|&r| (ptr, (r, off))))
            .collect();
        (regs, var_list, lane_regs, fresh, spills)
    }

    /// Which register variables are live after each instruction that may
    /// clobber them, as a mask of `1 << reg`, keyed by Instruction address:
    /// only those are spilled and reloaded around it (spill_vec_vars). A
    /// whole write, or a fresh lane store, starts a new value; other lane
    /// stores keep the remaining lanes, so they count as uses.
    fn vec_vars_live_across(func: &IrFunction, lanes: &FxHashMap<u32, (u32, i64)>,
                            regs: &FxHashMap<u32, u8>, fresh: &FxHashSet<usize>) -> FxHashMap<usize, u32> {
        let bit = |id: u32| lanes.get(&id).and_then(|l| regs.get(&l.0)).map_or(0u32, |&r| 1 << r);
        // (uses, defs) of one instruction.
        let effect = |inst: &Instruction| -> (u32, u32) {
            match inst {
                Instruction::Load { ptr, .. } => (bit(ptr.0), 0),
                Instruction::Store { ptr, .. } if fresh.contains(&(inst as *const Instruction as usize)) => (0, bit(ptr.0)),
                Instruction::Store { ptr, .. } => (bit(ptr.0), 0),
                Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr, args, .. } => {
                    let uses = args.iter().fold(0, |m, a| m | if let Operand::Value(v) = a { bit(v.0) } else { 0 });
                    (uses, dest_ptr.map_or(0, |d| bit(d.0)))
                }
                Instruction::Memcpy { dest, src, .. } => (bit(src.0), bit(dest.0)),
                Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. } => {
                    (info.args.iter().fold(0, |m, a| m | if let Operand::Value(v) = a { bit(v.0) } else { 0 }), 0)
                }
                _ => (0, 0),
            }
        };
        let index: FxHashMap<u32, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label.0, i)).collect();
        let succs: Vec<Vec<usize>> = func.blocks.iter()
            .map(|b| crate::backend::liveness::terminator_targets(&b.terminator).iter()
                .filter_map(|t| index.get(t).copied()).collect())
            .collect();
        let step = |live: u32, inst: &Instruction| {
            let (uses, defs) = effect(inst);
            (live & !defs) | uses
        };
        let mut live_in = vec![0u32; func.blocks.len()];
        loop {
            let mut changed = false;
            for (b, block) in func.blocks.iter().enumerate().rev() {
                let out = succs[b].iter().fold(0, |m, &s| m | live_in[s]);
                let inn = block.instructions.iter().rev().fold(out, step);
                if inn != live_in[b] {
                    live_in[b] = inn;
                    changed = true;
                }
            }
            if !changed {
                break;
            }
        }
        let mut spills = FxHashMap::default();
        for (b, block) in func.blocks.iter().enumerate() {
            let mut live = succs[b].iter().fold(0, |m, &s| m | live_in[s]);
            for inst in block.instructions.iter().rev() {
                if Self::clobbers_vec_regs(inst) {
                    spills.insert(inst as *const Instruction as usize, live);
                }
                live = step(live, inst);
            }
        }
        spills
    }

    /// The lane stores that start initializing a register variable: every
    /// other byte of it is stored, in the same block, before anything reads
    /// it, so the store may write the whole register, which breaks the
    /// dependency on its old contents. Keyed by Instruction address (see
    /// vec_before_instruction).
    fn fresh_lane_stores(func: &IrFunction, lanes: &FxHashMap<u32, (u32, i64)>,
                         size_of: &FxHashMap<u32, i64>, regs: &FxHashMap<u32, u8>) -> FxHashSet<usize> {
        let var_of = |id: u32| lanes.get(&id).map(|l| l.0).filter(|v| regs.contains_key(v));
        let mut fresh = FxHashSet::default();
        for block in &func.blocks {
            let insts = &block.instructions;
            for (i, inst) in insts.iter().enumerate() {
                let Instruction::Store { ptr, .. } = inst else { continue };
                let Some(var) = var_of(ptr.0) else { continue };
                let full = (1u32 << size_of[&var]) - 1;
                let mut covered = 0u32;
                for later in &insts[i..] {
                    let mut reads = false;
                    match later {
                        Instruction::Store { ptr, ty, val, .. } if var_of(ptr.0) == Some(var) => {
                            if let Operand::Value(v) = val {
                                reads = var_of(v.0) == Some(var);
                            }
                            covered |= ((1u32 << ty.size()) - 1) << lanes[&ptr.0].1;
                        }
                        Instruction::GetElementPtr { .. } => {}
                        other => other.for_each_used_value(|id| reads |= var_of(id) == Some(var)),
                    }
                    if reads || covered == full {
                        break;
                    }
                }
                if covered == full {
                    fresh.insert(inst as *const Instruction as usize);
                }
            }
        }
        fresh
    }

    /// Before an instruction: emit the lane loads and stores, GEPs and
    /// copies that involve a vector variable (returns true when the
    /// instruction is fully handled), and spill vector variables ahead of
    /// anything that may clobber their registers.
    pub(super) fn vec_before_instruction(&mut self, inst: &Instruction) -> bool {
        if self.vec_vars.is_empty() && self.vec_consts.is_empty() {
            return false;
        }
        match inst {
            Instruction::GetElementPtr { dest, .. } if self.vec_lanes.contains_key(&dest.0) => true,
            Instruction::Load { dest, ptr, ty, .. } if self.vec_lanes.contains_key(&ptr.0) => {
                let (reg, off) = self.vec_lanes[&ptr.0];
                let n = ty.size() as i64;
                let (lane, idx) = (Self::lane_suffix(n), off / n);
                match ty {
                    IrType::I8 | IrType::I16 => {
                        self.state.emit_fmt(format_args!("    umov w0, v{}.{}[{}]", reg, lane, idx));
                        let ext = if n == 1 { "sxtb" } else { "sxth" };
                        self.state.emit_fmt(format_args!("    {} x0, w0", ext));
                    }
                    IrType::I32 => {
                        self.state.emit_fmt(format_args!("    umov w0, v{}.s[{}]", reg, idx));
                        self.state.emit("    sxtw x0, w0");
                    }
                    _ if n == 8 => self.state.emit_fmt(format_args!("    umov x0, v{}.d[{}]", reg, idx)),
                    _ => self.state.emit_fmt(format_args!("    umov w0, v{}.{}[{}]", reg, lane, idx)),
                }
                self.store_x0_to(dest);
                true
            }
            Instruction::Store { val, ptr, ty, .. } if self.vec_lanes.contains_key(&ptr.0) => {
                let (reg, off) = self.vec_lanes[&ptr.0];
                let n = ty.size() as i64;
                self.operand_to_x0(val);
                let gpr = if n == 8 { "x0" } else { "w0" };
                if self.vec_fresh_stores.contains(&(inst as *const Instruction as usize)) {
                    // The rest is stored before it is read: broadcast.
                    let arr = match n { 1 => "16b", 2 => "8h", 4 => "4s", _ => "2d" };
                    self.state.emit_fmt(format_args!("    dup v{reg}.{arr}, {gpr}"));
                } else {
                    self.state.emit_fmt(format_args!("    ins v{}.{}[{}], {}", reg, Self::lane_suffix(n), off / n, gpr));
                }
                true
            }
            _ => {
                if Self::clobbers_vec_regs(inst) {
                    self.spill_vec_vars(inst, "str");
                }
                false
            }
        }
    }

    /// After an instruction: reload the vector variables spilled before it.
    pub(super) fn vec_after_instruction(&mut self, inst: &Instruction) {
        if Self::clobbers_vec_regs(inst) {
            self.spill_vec_vars(inst, "ldr");
            self.emit_vec_consts();
        }
    }

    /// Store (`str`) or reload (`ldr`) the register of every vector
    /// variable live after `inst` to or from its save slot.
    fn spill_vec_vars(&mut self, inst: &Instruction, instr: &str) {
        let live = self.vec_spills.get(&(inst as *const Instruction as usize)).copied().unwrap_or(u32::MAX);
        for (reg, size, offset) in self.vec_vars.clone() {
            if live & (1 << reg) == 0 {
                continue;
            }
            let width = if size == 16 { "q" } else { "d" };
            if instr == "str" {
                self.emit_store_to_sp(&format!("{width}{reg}"), offset, instr);
            } else {
                self.emit_load_from_sp(&format!("{width}{reg}"), offset, instr);
            }
        }
    }

    fn lane_suffix(n: i64) -> &'static str {
        match n { 1 => "b", 2 => "h", 4 => "s", _ => "d" }
    }

    /// Pick the vector temporaries that can live in a NEON register
    /// (ARM_VEC_TEMP_REGS) instead of their stack slot: an 8- or 16-byte
    /// alloca written by exactly one VecOp and afterwards only read, whole,
    /// by VecOps or a Memcpy in the same block, with nothing in between that
    /// could call out or use v28-v31 (clobbers_vec_regs). Its slot is then never written or read. Registers are assigned
    /// greedily by live range within each block.
    /// A temporary whose only use is a whole copy into a register variable
    /// (`v = a & b`: VecOp into T, then Memcpy V <- T) can live in the
    /// variable's register when nothing between the two reads or writes the
    /// variable: the VecOp then writes the variable directly and the copy
    /// becomes a move of the register to itself, which emits nothing. (The
    /// VecOp may read the variable too: each op reads its operands before
    /// writing its result.)
    pub(super) fn fold_vec_temp_copies(func: &IrFunction, temps: &mut FxHashMap<u32, u8>,
                                       vars: &FxHashMap<u32, u8>, lanes: &FxHashMap<u32, (u8, i64)>) {
        let reg_of = |id: u32| vars.get(&id).copied().or_else(|| lanes.get(&id).map(|l| l.0));
        let mut use_count: FxHashMap<u32, usize> = FxHashMap::default();
        for block in &func.blocks {
            for inst in &block.instructions {
                inst.for_each_used_value(|id| {
                    if temps.contains_key(&id) {
                        *use_count.entry(id).or_default() += 1;
                    }
                });
            }
            block.terminator.for_each_used_value(|id| {
                if temps.contains_key(&id) {
                    *use_count.entry(id).or_default() += 2;
                }
            });
        }
        for block in &func.blocks {
            let insts = &block.instructions;
            for (p, inst) in insts.iter().enumerate() {
                let Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr: Some(t), .. } = inst else { continue };
                // Producer and copy are its only two uses.
                if !temps.contains_key(&t.0) || use_count.get(&t.0) != Some(&2) {
                    continue;
                }
                let Some(q) = (p + 1..insts.len()).find(|&q| insts[q].used_values().contains(&t.0)) else { continue };
                let Instruction::Memcpy { dest: v, src, .. } = &insts[q] else { continue };
                let Some(reg) = vars.get(&v.0).copied() else { continue };
                if src != t {
                    continue;
                }
                let touches = |i: &Instruction| {
                    let mut hit = false;
                    i.for_each_used_value(|id| hit |= reg_of(id) == Some(reg));
                    hit || i.dest().is_some_and(|d| reg_of(d.0) == Some(reg))
                };
                if insts[p + 1..q].iter().any(touches) {
                    continue;
                }
                temps.insert(t.0, reg);
            }
        }
    }

    /// Vector Not results that one And reads and nothing else: the And can
    /// compute `a & ~s` as `bic` from the Not's source `s` directly, if `s`
    /// is in a register that no instruction between the two writes (only
    /// vector ops writing other registers may come between). Returns
    /// Not result -> its source.
    pub(super) fn fold_vec_nots(func: &IrFunction, regs: &FxHashMap<u32, u8>,
                                lanes: &FxHashMap<u32, (u8, i64)>) -> FxHashMap<u32, u32> {
        let mut folds = FxHashMap::default();
        let mut use_count: FxHashMap<u32, usize> = FxHashMap::default();
        for block in &func.blocks {
            for inst in &block.instructions {
                inst.for_each_used_value(|id| *use_count.entry(id).or_default() += 1);
            }
            block.terminator.for_each_used_value(|id| *use_count.entry(id).or_default() += 2);
        }
        let reg_of = |id: u32| regs.get(&id).copied().or_else(|| lanes.get(&id).filter(|l| l.1 == 0).map(|l| l.0));
        for block in &func.blocks {
            let insts = &block.instructions;
            for (p, inst) in insts.iter().enumerate() {
                let Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: VecOpKind::Not, .. }, dest_ptr: Some(t), args, .. } = inst
                    else { continue };
                let [Operand::Value(src)] = args[..] else { continue };
                let Some(src_reg) = regs.get(&src.0).copied() else { continue };
                if !regs.contains_key(&t.0) || use_count.get(&t.0) != Some(&2) {
                    continue;
                }
                let Some(q) = (p + 1..insts.len()).find(|&q| insts[q].used_values().contains(&t.0)) else { continue };
                let reads_t = matches!(&insts[q], Instruction::Intrinsic {
                    op: IntrinsicOp::VecOp { op: VecOpKind::And, splat: 0, .. }, args, ..
                } if args.iter().filter(|a| matches!(a, Operand::Value(v) if *v == *t)).count() == 1);
                let untouched = insts[p + 1..q].iter().all(|i| matches!(i,
                    Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr: Some(d), .. }
                        if reg_of(d.0).is_some_and(|r| r != src_reg)));
                if reads_t && untouched {
                    folds.insert(t.0, src.0);
                }
            }
        }
        folds
    }

    /// Integer right shifts by a constant whose result one Add reads and
    /// nothing else, where the Add's result goes to the register holding its
    /// other operand: the Add becomes a shift-and-accumulate (usra/ssra) of
    /// the shift's source. The source must be unchanged in between: in a
    /// register only vector ops writing other registers may come between, in
    /// memory only vector ops with register results. Returns shift result ->
    /// (source, count).
    pub(super) fn fold_vec_shift_adds(func: &IrFunction, regs: &FxHashMap<u32, u8>,
                                      lanes: &FxHashMap<u32, (u8, i64)>) -> FxHashMap<u32, (u32, u32)> {
        let mut folds = FxHashMap::default();
        let mut use_count: FxHashMap<u32, usize> = FxHashMap::default();
        for block in &func.blocks {
            for inst in &block.instructions {
                inst.for_each_used_value(|id| *use_count.entry(id).or_default() += 1);
            }
            block.terminator.for_each_used_value(|id| *use_count.entry(id).or_default() += 2);
        }
        let reg_of = |id: u32| regs.get(&id).copied().or_else(|| lanes.get(&id).filter(|l| l.1 == 0).map(|l| l.0));
        for block in &func.blocks {
            let insts = &block.instructions;
            for (p, inst) in insts.iter().enumerate() {
                let Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: op @ VecOpKind::Shr, elem, splat, .. }, dest_ptr: Some(t), args, .. } = inst
                    else { continue };
                let Some(n) = Self::vec_shift_imm(*op, *elem, *splat, args).filter(|&n| n >= 1) else { continue };
                let Some(Operand::Value(src)) = args.first() else { continue };
                if elem.is_float() || splat & 1 != 0 || !regs.contains_key(&t.0) || use_count.get(&t.0) != Some(&2) {
                    continue;
                }
                let Some(q) = (p + 1..insts.len()).find(|&q| insts[q].used_values().contains(&t.0)) else { continue };
                let Instruction::Intrinsic { op: IntrinsicOp::VecOp { op: VecOpKind::Add, elem: add_elem, splat: 0, .. },
                                             dest_ptr: Some(d), args: add_args, .. } = &insts[q] else { continue };
                let others: Vec<&Operand> = add_args.iter().filter(|a| !matches!(a, Operand::Value(v) if v == t)).collect();
                let [Operand::Value(other)] = others[..] else { continue };
                let src_reg = reg_of(src.0);
                let dest_reg = reg_of(d.0);
                let untouched = insts[p + 1..q].iter().all(|i| matches!(i,
                    Instruction::Intrinsic { op: IntrinsicOp::VecOp { .. }, dest_ptr: Some(w), .. }
                        if reg_of(w.0).is_some_and(|r| Some(r) != src_reg)));
                if add_elem == elem && dest_reg.is_some() && reg_of(other.0) == dest_reg && untouched {
                    folds.insert(t.0, (src.0, n));
                }
            }
        }
        folds
    }

    pub(super) fn assign_vec_temp_regs(func: &IrFunction) -> FxHashMap<u32, u8> {
        let mut regs = FxHashMap::default();
        let alloca_sizes: FxHashMap<u32, usize> = func.blocks.iter()
            .flat_map(|b| &b.instructions)
            .filter_map(|inst| match inst {
                Instruction::Alloca { dest, size: size @ (8 | 16), .. } => Some((dest.0, *size)),
                _ => None,
            })
            .collect();
        if alloca_sizes.is_empty() {
            return regs;
        }
        // Each candidate's uses: (block, index, kind). Kind: 0 = written by a
        // VecOp, 1 = read whole by a VecOp or Memcpy, 2 = anything else.
        let mut uses: FxHashMap<u32, Vec<(usize, usize, u8)>> = FxHashMap::default();
        for (bi, block) in func.blocks.iter().enumerate() {
            for (ii, inst) in block.instructions.iter().enumerate() {
                let mut kinds: Vec<(u32, u8)> = Vec::new();
                match inst {
                    Instruction::Intrinsic { op: IntrinsicOp::VecOp { bytes, splat, .. }, dest_ptr, args, .. } => {
                        if let Some(d) = dest_ptr {
                            let whole = alloca_sizes.get(&d.0) == Some(&(*bytes as usize));
                            kinds.push((d.0, if whole { 0 } else { 2 }));
                        }
                        for (i, arg) in args.iter().enumerate() {
                            if let Operand::Value(v) = arg {
                                let whole = splat & (1 << i) == 0
                                    && alloca_sizes.get(&v.0) == Some(&(*bytes as usize));
                                kinds.push((v.0, if whole { 1 } else { 2 }));
                            }
                        }
                    }
                    Instruction::Memcpy { dest, src, size } => {
                        kinds.push((dest.0, 2));
                        kinds.push((src.0, if alloca_sizes.get(&src.0) == Some(size) { 1 } else { 2 }));
                    }
                    // A call returning a short vector in v0 writes its result
                    // buffer (emit_call_store_hfa_return), like a VecOp.
                    Instruction::Call { info, .. } | Instruction::CallIndirect { info, .. }
                        if Self::vec_ret_buffer(info).is_some() =>
                    {
                        let buf = Self::vec_ret_buffer(info);
                        if let Instruction::CallIndirect { func_ptr: Operand::Value(v), .. } = inst {
                            kinds.push((v.0, 2));
                        }
                        for arg in &info.args {
                            if let Operand::Value(v) = arg {
                                let whole = buf.is_some_and(|(b, n)| b == v.0 && alloca_sizes.get(&b) == Some(&n));
                                kinds.push((v.0, if whole { 0 } else { 2 }));
                            }
                        }
                    }
                    Instruction::InlineAsm { .. } => return FxHashMap::default(),
                    other => other.for_each_used_value(|id| kinds.push((id, 2))),
                }
                for (id, kind) in kinds {
                    if alloca_sizes.contains_key(&id) {
                        uses.entry(id).or_default().push((bi, ii, kind));
                    }
                }
            }
            block.terminator.for_each_used_value(|id| {
                if alloca_sizes.contains_key(&id) {
                    uses.entry(id).or_default().push((bi, usize::MAX, 2));
                }
            });
        }
        // Live range (block, producer index, last read index) of each temp.
        let mut ranges: Vec<(usize, usize, usize, u32)> = Vec::new();
        for (&id, list) in &uses {
            let Some(&(bi, prod, _)) = list.iter().find(|u| u.2 == 0) else { continue };
            let ok = list.iter().all(|&(b, i, k)| b == bi && (k == 1 && i > prod || k == 0 && i == prod))
                && list.iter().filter(|u| u.2 == 0).count() == 1
                && list.len() > 1;
            if !ok {
                continue;
            }
            let last = list.iter().map(|u| u.1).max().unwrap_or(prod);
            let insts = &func.blocks[bi].instructions;
            if !insts[prod + 1..last].iter().any(Self::clobbers_vec_regs) {
                ranges.push((bi, prod, last, id));
            }
        }
        ranges.sort_unstable();
        // Greedy assignment per block: a register is free again at the
        // instruction that last reads it (NEON ops read before they write).
        let mut busy: Vec<(usize, usize, u8)> = Vec::new(); // (block, last read, reg)
        for (bi, prod, last, id) in ranges {
            busy.retain(|&(b, end, _)| b == bi && end > prod);
            if let Some(&reg) = ARM_VEC_TEMP_REGS.iter().find(|r| busy.iter().all(|b| b.2 != **r)) {
                busy.push((bi, last, reg));
                regs.insert(id, reg);
            }
        }
        regs
    }

    /// The result buffer of a call returning a one-member HFA (a short
    /// vector) in v0: (buffer value, size).
    fn vec_ret_buffer(info: &CallInfo) -> Option<(u32, usize)> {
        match (info.is_sret, info.struct_arg_riscv_float_classes.first(), info.args.first()) {
            (true, Some(Some(RiscvFloatClass::Hfa { count: 1, elem_size })), Some(Operand::Value(v))) =>
                Some((v.0, *elem_size)),
            _ => None,
        }
    }

    /// Whether an instruction may clobber v16-v31, which hold vector
    /// temporaries and variables: calls, inline asm, other intrinsics,
    /// va_* and atomics, and F128/I128 arithmetic (soft-float and 128-bit
    /// division call out).
    pub(super) fn clobbers_vec_regs(inst: &Instruction) -> bool {
        let wide = |ty: &IrType| matches!(ty, IrType::F128 | IrType::I128 | IrType::U128);
        match inst {
            Instruction::Intrinsic { op, .. } => !matches!(op, IntrinsicOp::VecOp { .. }),
            Instruction::Load { ty, .. } | Instruction::Store { ty, .. }
            | Instruction::BinOp { ty, .. } | Instruction::UnaryOp { ty, .. }
            | Instruction::Cmp { ty, .. } | Instruction::Select { ty, .. } => wide(ty),
            Instruction::Cast { from_ty, to_ty, .. } => wide(from_ty) || wide(to_ty),
            Instruction::Call { .. } | Instruction::CallIndirect { .. } | Instruction::InlineAsm { .. }
            | Instruction::VaArg { .. } | Instruction::VaStart { .. } | Instruction::VaEnd { .. }
            | Instruction::VaCopy { .. } | Instruction::VaArgStruct { .. }
            | Instruction::AtomicRmw { .. } | Instruction::AtomicCmpxchg { .. }
            | Instruction::AtomicLoad { .. } | Instruction::AtomicStore { .. } => true,
            _ => false,
        }
    }

    fn vec_arrangement(elem: VecElem, bytes: u8) -> &'static str {
        match (elem.size(), bytes == 16) {
            (1, true) => "16b", (1, false) => "8b",
            (2, true) => "8h", (2, false) => "4h",
            (4, true) => "4s", (4, false) => "2s",
            (_, true) => "2d", (_, false) => "1d",
        }
    }

    /// The lane bits a constant broadcast puts in each lane: the low bits of
    /// the constant as operand_to_x0 loads it.
    fn splat_bits(c: &IrConst, elem: VecElem) -> Option<u64> {
        let bits = match c {
            IrConst::I8(v) => *v as u64,
            IrConst::I16(v) => *v as u64,
            IrConst::I32(v) => *v as u64,
            IrConst::I64(v) => *v as u64,
            IrConst::F32(v) => v.to_bits() as u64,
            IrConst::F64(v) => v.to_bits(),
            IrConst::Zero => 0,
            _ => return None,
        };
        Some(if elem.size() == 8 { bits } else { bits & ((1u64 << (8 * elem.size())) - 1) })
    }

    /// The count of a shift by a broadcast constant that fits the immediate
    /// forms (shl #n, ushr/sshr #n), if `op` is such a shift.
    fn vec_shift_imm(op: VecOpKind, elem: VecElem, splat: u8, args: &[Operand]) -> Option<u32> {
        if !matches!(op, VecOpKind::Shl | VecOpKind::Shr) || splat & 2 == 0 {
            return None;
        }
        let Some(Operand::Const(c)) = args.get(1) else { return None };
        let n = c.to_i64()?;
        (0..8 * elem.size() as i64).contains(&n).then_some(n as u32)
    }

    /// The register holding a hoisted constant broadcast of `arg`, if any.
    fn vec_const_reg(&self, arg: &Operand, elem: VecElem, bytes: u8) -> Option<u8> {
        let Operand::Const(c) = arg else { return None };
        let bits = Self::splat_bits(c, elem)?;
        self.vec_consts.iter()
            .find(|(k, e, b, _)| *e == elem && *b == bytes && Self::splat_bits(k, *e) == Some(bits))
            .map(|&(_, _, _, reg)| reg)
    }

    /// Pick the constant broadcasts worth keeping in a register: those that
    /// vector ops use in a loop (a block some later block branches back
    /// to, or that branches back itself), most used first, one per free
    /// register.
    pub(super) fn assign_vec_const_regs(func: &IrFunction, free_regs: &[u8]) -> Vec<(IrConst, VecElem, u8, u8)> {
        let index: FxHashMap<u32, usize> = func.blocks.iter().enumerate().map(|(i, b)| (b.label.0, i)).collect();
        let mut in_loop = vec![false; func.blocks.len()];
        for (j, block) in func.blocks.iter().enumerate() {
            let mut targets = Vec::new();
            match &block.terminator {
                Terminator::Branch(l) => targets.push(*l),
                Terminator::CondBranch { true_label, false_label, .. } => targets.extend([*true_label, *false_label]),
                Terminator::Switch { cases, default, .. } => {
                    targets.extend(cases.iter().map(|c| c.1));
                    targets.push(*default);
                }
                Terminator::IndirectBranch { possible_targets, .. } => targets.extend(possible_targets.iter().copied()),
                _ => {}
            }
            for t in targets {
                if let Some(&k) = index.get(&t.0) {
                    if k <= j {
                        in_loop[k..=j].iter_mut().for_each(|b| *b = true);
                    }
                }
            }
        }
        let mut counts: Vec<(IrConst, VecElem, u8, usize)> = Vec::new();
        for (_, block) in func.blocks.iter().enumerate().filter(|(i, _)| in_loop[*i]) {
            for inst in &block.instructions {
                let Instruction::Intrinsic { op: IntrinsicOp::VecOp { op, elem, bytes, splat }, args, .. } = inst else { continue };
                for (i, arg) in args.iter().enumerate() {
                    let Operand::Const(c) = arg else { continue };
                    // (A shift count that fits an immediate needs no register.)
                    if splat & (1 << i) == 0 || (i == 1 && Self::vec_shift_imm(*op, *elem, *splat, args).is_some()) {
                        continue;
                    }
                    let Some(bits) = Self::splat_bits(c, *elem) else { continue };
                    match counts.iter_mut().find(|(k, e, b, _)| e == elem && b == bytes && Self::splat_bits(k, *e) == Some(bits)) {
                        Some(entry) => entry.3 += 1,
                        None => counts.push((*c, *elem, *bytes, 1)),
                    }
                }
            }
        }
        counts.sort_by(|a, b| b.3.cmp(&a.3));
        counts.into_iter().zip(free_regs.iter())
            .map(|((c, e, b, _), &reg)| (c, e, b, reg))
            .collect()
    }

    /// Load vector parameters kept in registers from the slots the
    /// parameter stores just wrote.
    pub(super) fn emit_vec_param_loads(&mut self) {
        for (param, reg, size) in self.vec_param_loads.clone() {
            let width = if size == 16 { "q" } else { "d" };
            self.vec_mem_access("ldr", &format!("{width}{reg}"), &param);
        }
    }

    /// Materialize the hoisted constant broadcasts in their registers.
    pub(super) fn emit_vec_consts(&mut self) {
        for (c, elem, bytes, reg) in self.vec_consts.clone() {
            self.operand_to_x0(&Operand::Const(c));
            let gpr = if elem.size() == 8 { "x0" } else { "w0" };
            self.state.emit_fmt(format_args!("    dup v{}.{}, {}", reg, Self::vec_arrangement(elem, bytes), gpr));
        }
        if !self.vec_consts.is_empty() {
            self.state.reg_cache.invalidate_acc();
        }
    }

    /// Emit an IntrinsicOp::VecOp: operands go to v0 (and v1), loaded through
    /// their pointers or broadcast from a scalar, and the result is computed in
    /// v0 and stored through `dest_ptr`. v0-v7 are codegen scratch.
    fn emit_vec_op(&mut self, op: VecOpKind, elem: VecElem, bytes: u8, splat: u8, dest_ptr: &Value, args: &[Operand]) {
        // A Not folded into the And that reads it: nothing here, and the And
        // becomes bic with the Not's source (fold_vec_nots).
        // Likewise a shift folded into the Add after it (usra/ssra,
        // fold_vec_shift_adds).
        if matches!(op, VecOpKind::Not) && self.vec_not_folds.contains_key(&dest_ptr.0)
            || matches!(op, VecOpKind::Shr) && self.vec_shift_adds.contains_key(&dest_ptr.0) {
            return;
        }
        let folded_from = |folds: &FxHashMap<u32, u32>| args.iter().position(|a| matches!(a, Operand::Value(v) if folds.contains_key(&v.0)));
        let folded = match op {
            VecOpKind::And => folded_from(&self.vec_not_folds),
            _ => None,
        };
        let accumulate = match op {
            VecOpKind::Add => args.iter().position(|a| matches!(a, Operand::Value(v) if self.vec_shift_adds.contains_key(&v.0))),
            _ => None,
        };
        let new_args;
        let mut shift_count = 0;
        let (splat, args) = match (folded, accumulate) {
            (Some(i), _) => {
                let Operand::Value(t) = args[i] else { unreachable!() };
                new_args = [args[1 - i], Operand::Value(Value(self.vec_not_folds[&t.0]))];
                ((splat >> (1 - i)) & 1, &new_args[..])
            }
            (None, Some(i)) => {
                let Operand::Value(t) = args[i] else { unreachable!() };
                let (src, n) = self.vec_shift_adds[&t.0];
                shift_count = n;
                new_args = [args[1 - i], Operand::Value(Value(src))];
                ((splat >> (1 - i)) & 1, &new_args[..])
            }
            (None, None) => (splat, args),
        };
        let q = bytes == 16;
        let mem = if q { "q" } else { "d" };
        let arr = Self::vec_arrangement(elem, bytes);
        let bits = if q { "16b" } else { "8b" };
        let imm_shift = Self::vec_shift_imm(op, elem, splat, args);
        // What v0/v1 still hold from the previous vector op, if nothing has
        // been emitted since.
        let mut held = if self.vec_scratch.0 == self.state.out.buf.len() { self.vec_scratch.1 } else { [None; 2] };
        // Operand registers: a temporary's own register, else v0/v1.
        let mut src = [String::new(), String::new()];
        for (i, arg) in args.iter().enumerate() {
            src[i] = format!("v{i}");
            if i == 1 && imm_shift.is_some() {
                continue;
            }
            if let Some(reg) = self.vec_const_reg(arg, elem, bytes).filter(|_| splat & (1 << i) != 0) {
                src[i] = format!("v{reg}");
            } else if splat & (1 << i) != 0 {
                self.operand_to_x0(arg);
                let gpr = if elem.size() == 8 { "x0" } else { "w0" };
                self.state.emit_fmt(format_args!("    dup v{}.{}, {}", i, arr, gpr));
                held[i] = None;
            } else if let Operand::Value(v) = arg {
                if let Some(&reg) = self.vec_regs.get(&v.0) {
                    src[i] = format!("v{reg}");
                } else if held[i] == Some((v.0, bytes)) {
                    // Already loaded.
                } else if i == 1 && held[0] == Some((v.0, bytes)) {
                    src[1] = "v0".to_string();
                } else {
                    self.vec_mem_access("ldr", &format!("{mem}{i}"), v);
                    held[i] = Some((v.0, bytes));
                }
            } else {
                self.operand_to_x0(arg);
                self.state.emit_fmt(format_args!("    ldr {}{}, [x0]", mem, i));
                held[i] = None;
            }
        }
        let dest_reg = self.vec_regs.get(&dest_ptr.0).copied();
        let d = format!("v{}", dest_reg.unwrap_or(0));
        let [a, b] = &src;
        let (f, u) = (elem.is_float(), elem.is_unsigned());
        let bin = |name: &str| format!("    {name} {d}.{arr}, {a}.{arr}, {b}.{arr}");
        let swapped = |name: &str| format!("    {name} {d}.{arr}, {b}.{arr}, {a}.{arr}");
        let bitwise = |name: &str| format!("    {name} {d}.{bits}, {a}.{bits}, {b}.{bits}");
        let gt = if f { "fcmgt" } else if u { "cmhi" } else { "cmgt" };
        let ge = if f { "fcmge" } else if u { "cmhs" } else { "cmge" };
        let shl = if u { "ushl" } else { "sshl" };
        let lines = match op {
            // d += b >> n, with d already holding the other operand (a).
            VecOpKind::Add if accumulate.is_some() => {
                let mut lines = Vec::new();
                if *a != d {
                    lines.push(format!("    orr {d}.{bits}, {a}.{bits}, {a}.{bits}"));
                }
                lines.push(format!("    {} {d}.{arr}, {b}.{arr}, #{shift_count}", if u { "usra" } else { "ssra" }));
                lines
            }
            VecOpKind::Add => vec![bin(if f { "fadd" } else { "add" })],
            VecOpKind::Sub => vec![bin(if f { "fsub" } else { "sub" })],
            VecOpKind::Mul => vec![bin(if f { "fmul" } else { "mul" })],
            VecOpKind::Div => vec![bin("fdiv")],
            VecOpKind::And => vec![bitwise(if folded.is_some() { "bic" } else { "and" })],
            VecOpKind::Or => vec![bitwise("orr")],
            VecOpKind::Xor => vec![bitwise("eor")],
            // ushl/sshl shift left by a signed per-lane amount; right shifts
            // shift left by the negated amount (logical for ushl, arithmetic
            // for sshl), negated into v1 so a temporary count survives.
            // A constant count uses the immediate forms (ushr/sshr take
            // 1..=lane bits, so a shift by 0 is a move).
            VecOpKind::Shl | VecOpKind::Shr if imm_shift == Some(0) =>
                vec![format!("    orr {d}.{bits}, {a}.{bits}, {a}.{bits}")],
            VecOpKind::Shl | VecOpKind::Shr if imm_shift.is_some() => {
                let name = match op { VecOpKind::Shl => "shl", _ if u => "ushr", _ => "sshr" };
                vec![format!("    {name} {d}.{arr}, {a}.{arr}, #{}", imm_shift.unwrap())]
            }
            VecOpKind::Shl => vec![bin(shl)],
            VecOpKind::Shr => vec![
                format!("    neg v1.{arr}, {b}.{arr}"),
                format!("    {shl} {d}.{arr}, {a}.{arr}, v1.{arr}"),
            ],
            VecOpKind::Eq => vec![bin(if f { "fcmeq" } else { "cmeq" })],
            VecOpKind::Ne => vec![bin(if f { "fcmeq" } else { "cmeq" }),
                                  format!("    mvn {d}.{bits}, {d}.{bits}")],
            VecOpKind::Gt => vec![bin(gt)],
            VecOpKind::Ge => vec![bin(ge)],
            VecOpKind::Lt => vec![swapped(gt)],
            VecOpKind::Le => vec![swapped(ge)],
            VecOpKind::Neg => vec![format!("    {} {d}.{arr}, {a}.{arr}", if f { "fneg" } else { "neg" })],
            VecOpKind::Not => vec![format!("    mvn {d}.{bits}, {a}.{bits}")],
        };
        for line in &lines {
            self.state.emit(line);
        }
        if dest_reg.is_none() {
            self.vec_mem_access("str", &format!("{mem}0"), dest_ptr);
        }
        self.state.reg_cache.invalidate_acc();
        // The result went to v0 and memory, or a register right shift
        // negated its count into v1.
        if dest_reg.is_none() {
            held = [None; 2];
        } else if matches!(op, VecOpKind::Shr) && imm_shift.is_none() {
            held[1] = None;
        }
        self.vec_scratch = (self.state.out.buf.len(), held);
    }

    /// The register holding the whole vector at pointer value `id`: a
    /// temporary, a variable, or a constant GEP of offset 0 from one.
    pub(super) fn vec_reg_of(&self, id: u32) -> Option<u8> {
        self.vec_regs.get(&id).copied()
            .or_else(|| self.vec_lanes.get(&id).filter(|l| l.1 == 0).map(|l| l.0))
    }

    /// Memcpy to or from a vector value held in a register: a register
    /// move, or one whole-vector load or store. Returns false when neither
    /// side is in a register.
    pub(super) fn try_emit_vec_copy(&mut self, dest: &Value, src: &Value, size: usize) -> bool {
        let width = if size == 16 { "q" } else { "d" };
        let bits = if size == 16 { "16b" } else { "8b" };
        match (self.vec_reg_of(dest.0), self.vec_reg_of(src.0)) {
            (Some(d), Some(s)) => {
                if d != s {
                    self.state.emit_fmt(format_args!("    orr v{d}.{bits}, v{s}.{bits}, v{s}.{bits}"));
                }
            }
            (Some(d), None) => self.vec_mem_access("ldr", &format!("{width}{d}"), src),
            (None, Some(s)) => self.vec_mem_access("str", &format!("{width}{s}"), dest),
            (None, None) => return false,
        }
        self.state.reg_cache.invalidate_acc();
        true
    }

    /// Load or store a whole vector register (`q`/`d`) through pointer value
    /// `ptr`, addressing a plain stack alloca directly off the frame.
    fn vec_mem_access(&mut self, instr: &str, reg: &str, ptr: &Value) {
        if let Some(SlotAddr::Direct(slot)) = self.state.resolve_slot_addr(ptr.0) {
            if instr == "str" {
                self.emit_store_to_sp(reg, slot.0, instr);
            } else {
                self.emit_load_from_sp(reg, slot.0, instr);
            }
        } else if let Some(base) = self.ptr_in_reg(ptr) {
            self.state.emit_fmt(format_args!("    {} {}, [{}]", instr, reg, base));
        } else {
            self.operand_to_x0(&Operand::Value(*ptr));
            self.state.emit_fmt(format_args!("    {} {}, [x0]", instr, reg));
        }
    }

    pub(super) fn emit_intrinsic_arm(&mut self, dest: &Option<Value>, op: &IntrinsicOp, dest_ptr: &Option<Value>, args: &[Operand]) {
        match op {
            IntrinsicOp::VecOp { op: vop, elem, bytes, splat } => {
                if let Some(dptr) = dest_ptr {
                    self.emit_vec_op(*vop, *elem, *bytes, *splat, dptr, args);
                }
            }
            IntrinsicOp::Lfence | IntrinsicOp::Mfence => {
                self.state.emit("    dmb ish");
            }
            IntrinsicOp::Sfence => {
                self.state.emit("    dmb ishst");
            }
            IntrinsicOp::Pause => {
                self.state.emit("    yield");
            }
            IntrinsicOp::Clflush => {
                // ARM has no direct clflush; use dc civac (clean+invalidate to PoC)
                self.operand_to_x0(&args[0]);
                self.state.emit("    dc civac, x0");
            }
            IntrinsicOp::Movnti => {
                self.emit_nontemporal_store(dest_ptr, args, "w9", "w0");
            }
            IntrinsicOp::Movnti64 => {
                self.emit_nontemporal_store(dest_ptr, args, "x9", "x0");
            }
            IntrinsicOp::Movntdq | IntrinsicOp::Movntpd => {
                // Non-temporal 128-bit store: dest_ptr = target, args[0] = source ptr
                if let Some(ptr) = dest_ptr {
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    ldr q0, [x0]");
                    self.load_ptr_to_reg(ptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::Loaddqu => {
                // Load 128-bit unaligned: args[0] = source ptr, dest_ptr = result storage
                if let Some(dptr) = dest_ptr {
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    ldr q0, [x0]");
                    self.load_ptr_to_reg(dptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::Storedqu => {
                // Store 128-bit unaligned: dest_ptr = target ptr, args[0] = source data ptr
                if let Some(ptr) = dest_ptr {
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    ldr q0, [x0]");
                    self.load_ptr_to_reg(ptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::Pcmpeqb128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "cmeq");
                }
            }
            IntrinsicOp::Pcmpeqd128 => {
                if let Some(dptr) = dest_ptr {
                    // For 32-bit lane equality, load q regs, use cmeq with .4s arrangement
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    ldr q0, [x0]");
                    if let Operand::Value(v) = &args[1] {
                        self.load_ptr_to_reg(v, "x1");
                    } else {
                        self.operand_to_x0(&args[1]);
                        self.state.emit("    mov x1, x0");
                    }
                    self.state.emit("    ldr q1, [x1]");
                    self.state.emit("    cmeq v0.4s, v0.4s, v1.4s");
                    self.load_ptr_to_reg(dptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::Psubusb128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "uqsub");
                }
            }
            IntrinsicOp::Psubsb128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "sqsub");
                }
            }
            IntrinsicOp::Por128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "orr");
                }
            }
            IntrinsicOp::Pand128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "and");
                }
            }
            IntrinsicOp::Pxor128 => {
                if let Some(dptr) = dest_ptr {
                    self.emit_neon_binary_128(dptr, args, "eor");
                }
            }
            IntrinsicOp::Pmovmskb128 => {
                // Extract the high bit of each byte in a 128-bit vector into a 16-bit mask.
                // NEON has no pmovmskb equivalent, so we use a multi-step sequence:
                //   1. Load 128-bit data into v0
                //   2. Shift right each byte by 7 to isolate the sign bit
                //   3. Multiply by power-of-2 bit positions, then add across lanes
                self.operand_to_x0(&args[0]);
                self.state.emit("    ldr q0, [x0]");
                self.state.emit("    ushr v0.16b, v0.16b, #7");
                // Load bit position constants: [1,2,4,8,16,32,64,128] repeated
                self.state.emit("    movz x0, #0x0201");
                self.state.emit("    movk x0, #0x0804, lsl #16");
                self.state.emit("    movk x0, #0x2010, lsl #32");
                self.state.emit("    movk x0, #0x8040, lsl #48");
                self.state.emit("    fmov d1, x0");
                self.state.emit("    mov v1.d[1], x0");
                self.state.emit("    mul v0.16b, v0.16b, v1.16b");
                // Split and sum each half
                self.state.emit("    ext v1.16b, v0.16b, v0.16b, #8");
                self.state.emit("    addv b0, v0.8b");
                self.state.emit("    umov w0, v0.b[0]");
                self.state.emit("    addv b1, v1.8b");
                self.state.emit("    umov w1, v1.b[0]");
                self.state.emit("    orr w0, w0, w1, lsl #8");
                self.store_scalar_dest(dest, "x0");
            }
            IntrinsicOp::SetEpi8 => {
                if let Some(dptr) = dest_ptr {
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    dup v0.16b, w0");
                    self.load_ptr_to_reg(dptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::SetEpi32 => {
                if let Some(dptr) = dest_ptr {
                    self.operand_to_x0(&args[0]);
                    self.state.emit("    dup v0.4s, w0");
                    self.load_ptr_to_reg(dptr, "x0");
                    self.state.emit("    str q0, [x0]");
                }
            }
            IntrinsicOp::Crc32_8 | IntrinsicOp::Crc32_16
            | IntrinsicOp::Crc32_32 | IntrinsicOp::Crc32_64 => {
                let is_64 = matches!(op, IntrinsicOp::Crc32_64);
                let (save_reg, crc_inst) = match op {
                    IntrinsicOp::Crc32_8  => ("w9", "crc32cb w9, w9, w0"),
                    IntrinsicOp::Crc32_16 => ("w9", "crc32ch w9, w9, w0"),
                    IntrinsicOp::Crc32_32 => ("w9", "crc32cw w9, w9, w0"),
                    IntrinsicOp::Crc32_64 => ("x9", "crc32cx w9, w9, x0"),
                    _ => unreachable!(),
                };
                self.operand_to_x0(&args[0]);
                self.state.emit_fmt(format_args!("    mov {}, {}", save_reg, if is_64 { "x0" } else { "w0" }));
                self.operand_to_x0(&args[1]);
                self.state.emit_fmt(format_args!("    {}", crc_inst));
                self.state.emit("    mov x0, x9");
                self.store_scalar_dest(dest, "x0");
            }
            IntrinsicOp::FrameAddress => {
                self.state.emit("    mov x0, x29");
                self.store_scalar_dest(dest, "x0");
            }
            IntrinsicOp::ReturnAddress => {
                // x30 (lr) is clobbered by bl instructions, so read from stack
                self.state.emit("    ldr x0, [x29, #8]");
                self.store_scalar_dest(dest, "x0");
            }
            IntrinsicOp::ThreadPointer => {
                // __builtin_thread_pointer(): read TLS base from tpidr_el0
                self.state.emit("    mrs x0, tpidr_el0");
                self.store_scalar_dest(dest, "x0");
            }
            IntrinsicOp::SqrtF64 => self.emit_f64_unary_neon(dest, args, "fsqrt"),
            IntrinsicOp::SqrtF32 => self.emit_f32_unary_neon(dest, args, "fsqrt"),
            IntrinsicOp::FabsF64 => self.emit_f64_unary_neon(dest, args, "fabs"),
            IntrinsicOp::FabsF32 => self.emit_f32_unary_neon(dest, args, "fabs"),
            // x86-specific SSE/AES-NI/CLMUL intrinsics - these are x86-only and should
            // not appear in ARM codegen in practice. Cross-compiled code that conditionally
            // uses these behind #ifdef __x86_64__ will have the calls dead-code eliminated.
            // TODO: consider emitting a runtime trap instead of silent zeros
            IntrinsicOp::Aesenc128 | IntrinsicOp::Aesenclast128
            | IntrinsicOp::Aesdec128 | IntrinsicOp::Aesdeclast128
            | IntrinsicOp::Aesimc128 | IntrinsicOp::Aeskeygenassist128
            | IntrinsicOp::Pclmulqdq128
            | IntrinsicOp::Pslldqi128 | IntrinsicOp::Psrldqi128
            | IntrinsicOp::Psllqi128 | IntrinsicOp::Psrlqi128
            | IntrinsicOp::Pshufd128 | IntrinsicOp::Loadldi128
            | IntrinsicOp::Paddw128 | IntrinsicOp::Psubw128
            | IntrinsicOp::Pmulhw128 | IntrinsicOp::Pmaddwd128
            | IntrinsicOp::Pcmpgtw128 | IntrinsicOp::Pcmpgtb128
            | IntrinsicOp::Psllwi128 | IntrinsicOp::Psrlwi128
            | IntrinsicOp::Psrawi128 | IntrinsicOp::Psradi128
            | IntrinsicOp::Pslldi128 | IntrinsicOp::Psrldi128
            | IntrinsicOp::Paddd128 | IntrinsicOp::Psubd128
            | IntrinsicOp::Packssdw128 | IntrinsicOp::Packsswb128 | IntrinsicOp::Packuswb128
            | IntrinsicOp::Punpcklbw128 | IntrinsicOp::Punpckhbw128
            | IntrinsicOp::Punpcklwd128 | IntrinsicOp::Punpckhwd128
            | IntrinsicOp::SetEpi16 | IntrinsicOp::Pinsrw128
            | IntrinsicOp::Pextrw128 | IntrinsicOp::Storeldi128
            | IntrinsicOp::Cvtsi128Si32 | IntrinsicOp::Cvtsi32Si128
            | IntrinsicOp::Cvtsi128Si64
            | IntrinsicOp::Pshuflw128 | IntrinsicOp::Pshufhw128
            | IntrinsicOp::Pinsrd128 | IntrinsicOp::Pextrd128
            | IntrinsicOp::Pinsrb128 | IntrinsicOp::Pextrb128
            | IntrinsicOp::Pinsrq128 | IntrinsicOp::Pextrq128 => {
                // x86-only: zero dest if present
                if let Some(dptr) = dest_ptr {
                    if let Some(slot) = self.state.get_slot(dptr.0) {
                        self.state.emit_fmt(format_args!("    add x9, sp, #{}", slot.0));
                        self.state.emit("    stp xzr, xzr, [x9]");
                    }
                }
            }
        }
    }

    // ---- F128 (long double / IEEE quad precision) soft-float helpers ----
    //
    // On AArch64, long double is IEEE 754 binary128 (16 bytes).
    // Hardware has no quad-precision FP ops, so we use compiler-rt/libgcc soft-float:
    //   Comparison: __eqtf2, __lttf2, __letf2, __gttf2, __getf2
    //   Arithmetic: __addtf3, __subtf3, __multf3, __divtf3
    //   Conversion: __extenddftf2 (f64->f128), __trunctfdf2 (f128->f64)
    // ABI: f128 passed/returned in Q registers (q0, q1). Int result in w0/x0.

}

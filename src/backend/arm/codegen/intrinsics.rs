//! AArch64 NEON/SIMD intrinsic emission and F128 (quad-precision) soft-float helpers.
//!
//! NEON intrinsics: SSE-equivalent operations via 128-bit NEON instructions.
//! F128: IEEE 754 binary128 via compiler-rt/libgcc soft-float libcalls.

use crate::ir::reexports::{Instruction, IntrinsicOp, IrFunction, Operand, Value, VecElem, VecOpKind};
use crate::backend::state::SlotAddr;
use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::IrType;
use super::emit::{ARM_VEC_TEMP_REGS, ARM_VEC_VAR_REGS};
use super::emit::ArmCodegen;

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
    /// (ARM_VEC_VAR_REGS) for the whole function: an 8- or 16-byte local
    /// (not a parameter, not volatile or over-aligned) that some VecOp uses
    /// and whose address never escapes. Every use of the alloca, or of a
    /// constant GEP from it, must be a whole read or write by a VecOp or
    /// Memcpy, or a load or store of one aligned lane of up to 8 bytes.
    /// Calls and other instructions that may clobber v16-v31 spill and
    /// reload the register around themselves (spill_vec_vars).
    ///
    /// Temporaries (`temps`) are not candidates. Returns (alloca ->
    /// register, each variable's (register, size), lane pointer ->
    /// (register, offset)).
    #[allow(clippy::type_complexity)]
    pub(super) fn assign_vec_var_regs(func: &IrFunction, temps: &FxHashMap<u32, u8>)
        -> (FxHashMap<u32, u8>, Vec<(u8, u8)>, FxHashMap<u32, (u8, i64)>)
    {
        let none = || (FxHashMap::default(), Vec::new(), FxHashMap::default());
        let insts = || func.blocks.iter().flat_map(|b| &b.instructions);
        if insts().any(|i| matches!(i, Instruction::InlineAsm { .. })) {
            return none();
        }
        let mut size_of: FxHashMap<u32, i64> = insts()
            .filter_map(|inst| match inst {
                Instruction::Alloca { dest, size: size @ (8 | 16), align, volatile: false, .. }
                    if *align <= 16 && !func.param_alloca_values.contains(dest)
                        && !temps.contains_key(&dest.0) =>
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
                    if lanes.contains_key(&ptr.0) && !lane_ok(&lanes, &size_of, ptr.0, *ty) {
                        reject(ptr.0);
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
        size_of.retain(|id, _| vec_used.contains(id) && !bad.contains(id));
        let mut vars: Vec<(u32, i64)> = size_of.into_iter().collect();
        vars.sort_unstable();
        let mut regs = FxHashMap::default();
        let mut var_list = Vec::new();
        for ((id, size), &reg) in vars.into_iter().zip(ARM_VEC_VAR_REGS.iter()) {
            regs.insert(id, reg);
            var_list.push((reg, size as u8));
        }
        let lane_regs = lanes.into_iter()
            .filter_map(|(ptr, (var, off))| regs.get(&var).map(|&r| (ptr, (r, off))))
            .collect();
        (regs, var_list, lane_regs)
    }

    /// Before an instruction: emit the lane loads and stores, GEPs and
    /// copies that involve a vector variable (returns true when the
    /// instruction is fully handled), and spill vector variables ahead of
    /// anything that may clobber their registers.
    pub(super) fn vec_before_instruction(&mut self, inst: &Instruction) -> bool {
        if self.vec_vars.is_empty() {
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
                self.state.emit_fmt(format_args!("    ins v{}.{}[{}], {}", reg, Self::lane_suffix(n), off / n, gpr));
                true
            }
            _ => {
                if Self::clobbers_vec_regs(inst) {
                    self.spill_vec_vars("str");
                }
                false
            }
        }
    }

    /// After an instruction: reload the vector variables spilled before it.
    pub(super) fn vec_after_instruction(&mut self, inst: &Instruction) {
        if !self.vec_vars.is_empty() && Self::clobbers_vec_regs(inst) {
            self.spill_vec_vars("ldr");
        }
    }

    /// Store (`str`) or reload (`ldr`) every vector variable's register
    /// to or from its save slot.
    fn spill_vec_vars(&mut self, instr: &str) {
        for (reg, size, offset) in self.vec_vars.clone() {
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

    /// Emit an IntrinsicOp::VecOp: operands go to v0 (and v1), loaded through
    /// their pointers or broadcast from a scalar, and the result is computed in
    /// v0 and stored through `dest_ptr`. v0-v7 are codegen scratch.
    fn emit_vec_op(&mut self, op: VecOpKind, elem: VecElem, bytes: u8, splat: u8, dest_ptr: &Value, args: &[Operand]) {
        let q = bytes == 16;
        let mem = if q { "q" } else { "d" };
        let arr = match (elem.size(), q) {
            (1, true) => "16b", (1, false) => "8b",
            (2, true) => "8h", (2, false) => "4h",
            (4, true) => "4s", (4, false) => "2s",
            (_, true) => "2d", (_, false) => "1d",
        };
        let bits = if q { "16b" } else { "8b" };
        // Operand registers: a temporary's own register, else v0/v1.
        let mut src = [String::new(), String::new()];
        for (i, arg) in args.iter().enumerate() {
            src[i] = format!("v{i}");
            if splat & (1 << i) != 0 {
                self.operand_to_x0(arg);
                let gpr = if elem.size() == 8 { "x0" } else { "w0" };
                self.state.emit_fmt(format_args!("    dup v{}.{}, {}", i, arr, gpr));
            } else if let Operand::Value(v) = arg {
                if let Some(&reg) = self.vec_regs.get(&v.0) {
                    src[i] = format!("v{reg}");
                } else {
                    self.vec_mem_access("ldr", &format!("{mem}{i}"), v);
                }
            } else {
                self.operand_to_x0(arg);
                self.state.emit_fmt(format_args!("    ldr {}{}, [x0]", mem, i));
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
            VecOpKind::Add => vec![bin(if f { "fadd" } else { "add" })],
            VecOpKind::Sub => vec![bin(if f { "fsub" } else { "sub" })],
            VecOpKind::Mul => vec![bin(if f { "fmul" } else { "mul" })],
            VecOpKind::Div => vec![bin("fdiv")],
            VecOpKind::And => vec![bitwise("and")],
            VecOpKind::Or => vec![bitwise("orr")],
            VecOpKind::Xor => vec![bitwise("eor")],
            // ushl/sshl shift left by a signed per-lane amount; right shifts
            // shift left by the negated amount (logical for ushl, arithmetic
            // for sshl), negated into v1 so a temporary count survives.
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
    }

    /// Memcpy to or from a vector value held in a register: a register
    /// move, or one whole-vector load or store. Returns false when neither
    /// side is in a register.
    pub(super) fn try_emit_vec_copy(&mut self, dest: &Value, src: &Value, size: usize) -> bool {
        let width = if size == 16 { "q" } else { "d" };
        let bits = if size == 16 { "16b" } else { "8b" };
        match (self.vec_regs.get(&dest.0).copied(), self.vec_regs.get(&src.0).copied()) {
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

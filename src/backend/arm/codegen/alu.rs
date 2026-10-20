//! ArmCodegen: ALU operations (integer arithmetic, bitwise, unary).

use crate::ir::reexports::{IrBinOp, Operand, Value};
use crate::common::types::IrType;
use super::emit::{ArmCodegen, callee_saved_name, arm_alu_mnemonic};

impl ArmCodegen {
    pub(super) fn emit_float_neg_impl(&mut self, ty: IrType) {
        if ty == IrType::F32 {
            self.state.emit("    fmov s0, w0");
            self.state.emit("    fneg s0, s0");
            self.state.emit("    fmov w0, s0");
            self.state.emit("    mov w0, w0"); // zero-extend
        } else {
            self.state.emit("    fmov d0, x0");
            self.state.emit("    fneg d0, d0");
            self.state.emit("    fmov x0, d0");
        }
    }

    pub(super) fn emit_int_neg_impl(&mut self, _ty: IrType) {
        self.state.emit("    neg x0, x0");
    }

    pub(super) fn emit_int_not_impl(&mut self, _ty: IrType) {
        self.state.emit("    mvn x0, x0");
    }

    pub(super) fn emit_int_clz_impl(&mut self, ty: IrType) {
        if ty == IrType::I32 || ty == IrType::U32 {
            self.state.emit("    clz w0, w0");
        } else {
            self.state.emit("    clz x0, x0");
        }
    }

    pub(super) fn emit_int_ctz_impl(&mut self, ty: IrType) {
        if ty == IrType::I32 || ty == IrType::U32 {
            self.state.emit("    rbit w0, w0");
            self.state.emit("    clz w0, w0");
        } else {
            self.state.emit("    rbit x0, x0");
            self.state.emit("    clz x0, x0");
        }
    }

    pub(super) fn emit_int_bswap_impl(&mut self, ty: IrType) {
        if ty == IrType::I16 || ty == IrType::U16 {
            self.state.emit("    rev w0, w0");
            self.state.emit("    lsr w0, w0, #16");
        } else if ty == IrType::I32 || ty == IrType::U32 {
            self.state.emit("    rev w0, w0");
        } else {
            self.state.emit("    rev x0, x0");
        }
    }

    pub(super) fn emit_int_popcount_impl(&mut self, ty: IrType) {
        if ty == IrType::I32 || ty == IrType::U32 {
            self.state.emit("    fmov s0, w0");
        } else {
            self.state.emit("    fmov d0, x0");
        }
        self.state.emit("    cnt v0.8b, v0.8b");
        self.state.emit("    uaddlv h0, v0.8b");
        self.state.emit("    fmov w0, s0");
    }

    pub(super) fn emit_int_binop_impl(&mut self, dest: &Value, op: IrBinOp, lhs: &Operand, rhs: &Operand, ty: IrType) {
        let use_32bit = ty == IrType::I32 || ty == IrType::U32;
        let is_unsigned = ty.is_unsigned();

        // Strength reduction: UDiv/URem by power-of-2 constant
        if let Some(shift) = Self::const_as_power_of_2(rhs) {
            if op == IrBinOp::UDiv {
                self.operand_to_x0(lhs);
                if use_32bit {
                    self.state.emit_fmt(format_args!("    lsr w0, w0, #{}", shift));
                } else {
                    self.state.emit_fmt(format_args!("    lsr x0, x0, #{}", shift));
                }
                self.store_x0_to(dest);
                return;
            }
            if op == IrBinOp::URem {
                self.operand_to_x0(lhs);
                let mask = (1u64 << shift) - 1;
                if use_32bit {
                    self.state.emit_fmt(format_args!("    and w0, w0, #{}", mask));
                } else {
                    self.state.emit_fmt(format_args!("    and x0, x0, #{}", mask));
                }
                self.store_x0_to(dest);
                return;
            }
        }

        // Three-address path for ALU ops and shifts: read operands from their
        // registers (loading the rest into x1/x0), use an immediate rhs when it
        // encodes, and write the dest's register directly.
        let is_shift = matches!(op, IrBinOp::Shl | IrBinOp::AShr | IrBinOp::LShr);
        if is_shift || matches!(op, IrBinOp::Add | IrBinOp::Sub | IrBinOp::And
            | IrBinOp::Or | IrBinOp::Xor | IrBinOp::Mul)
        {
            let width = if use_32bit { 32 } else { 64 };
            let rhs_imm = if is_shift {
                Self::const_as_shift_imm(rhs, width)
            } else if matches!(op, IrBinOp::Add | IrBinOp::Sub) {
                Self::const_as_imm12(rhs)
            } else {
                None
            };
            let lhs_reg = self.operand_reg(lhs).map(callee_saved_name);
            let rhs_reg = self.operand_reg(rhs).map(callee_saved_name);
            let rhs_needs_load = rhs_imm.is_none() && rhs_reg.is_none();
            let a = match lhs_reg {
                Some(r) => r,
                None => {
                    self.operand_to_x0(lhs);
                    if rhs_needs_load {
                        self.state.emit("    mov x1, x0");
                        "x1"
                    } else {
                        "x0"
                    }
                }
            };
            let b = match (rhs_imm, rhs_reg) {
                (Some(imm), _) => format!("#{}", imm),
                (None, Some(r)) => r.to_string(),
                (None, None) => {
                    self.operand_to_x0(rhs);
                    "x0".to_string()
                }
            };
            let d = self.dest_reg(dest).map_or("x0", callee_saved_name);
            let mnemonic = match op {
                IrBinOp::Shl => "lsl",
                IrBinOp::AShr => "asr",
                IrBinOp::LShr => "lsr",
                _ => arm_alu_mnemonic(op),
            };
            if use_32bit {
                let w = |r: &str| if r.starts_with('x') { r.replacen('x', "w", 1) } else { r.to_string() };
                self.state.emit_fmt(format_args!("    {} {}, {}, {}", mnemonic, w(d), w(a), w(&b)));
                // Keep signed 32-bit results in canonical sign-extended form.
                if !is_unsigned {
                    self.state.emit_fmt(format_args!("    sxtw {}, {}", d, w(d)));
                }
            } else {
                self.state.emit_fmt(format_args!("    {} {}, {}, {}", mnemonic, d, a, b));
            }
            if d == "x0" {
                self.store_x0_to(dest);
            } else {
                self.state.reg_cache.invalidate_acc();
            }
            return;
        }

        // Division and remainder: accumulator path
        self.operand_to_x0(lhs);
        self.state.emit("    mov x1, x0");
        self.operand_to_x0(rhs);
        self.state.emit("    mov x2, x0");

        if use_32bit {
            match op {
                IrBinOp::SDiv => {
                    self.state.emit("    sdiv w0, w1, w2");
                    self.state.emit("    sxtw x0, w0");
                }
                IrBinOp::UDiv => self.state.emit("    udiv w0, w1, w2"),
                IrBinOp::SRem => {
                    self.state.emit("    sdiv w3, w1, w2");
                    self.state.emit("    msub w0, w3, w2, w1");
                    self.state.emit("    sxtw x0, w0");
                }
                IrBinOp::URem => {
                    self.state.emit("    udiv w3, w1, w2");
                    self.state.emit("    msub w0, w3, w2, w1");
                }
                _ => unreachable!("handled by the three-address path"),
            }
        } else {
            match op {
                IrBinOp::SDiv => self.state.emit("    sdiv x0, x1, x2"),
                IrBinOp::UDiv => self.state.emit("    udiv x0, x1, x2"),
                IrBinOp::SRem => {
                    self.state.emit("    sdiv x3, x1, x2");
                    self.state.emit("    msub x0, x3, x2, x1");
                }
                IrBinOp::URem => {
                    self.state.emit("    udiv x3, x1, x2");
                    self.state.emit("    msub x0, x3, x2, x1");
                }
                _ => unreachable!("handled by the three-address path"),
            }
        }

        self.store_x0_to(dest);
    }

    pub(super) fn emit_copy_i128_impl(&mut self, dest: &Value, src: &Operand) {
        self.operand_to_x0_x1(src);
        self.store_x0_x1_to(dest);
    }
}

//! ArmCodegen: floating-point binary operations.

use crate::ir::reexports::{Operand, Value};
use crate::common::types::IrType;
use crate::backend::cast::FloatOp;
use crate::backend::traits::ArchCodegen;
use super::emit::ArmCodegen;

impl ArmCodegen {
    pub(super) fn emit_float_binop_impl(&mut self, dest: &Value, op: FloatOp, lhs: &Operand, rhs: &Operand, ty: IrType) {
        if ty == IrType::F128 {
            crate::backend::f128_softfloat::f128_emit_binop(self, dest, op, lhs, rhs);
            return;
        }
        let mnemonic = self.emit_float_binop_mnemonic(op);
        // Any operand in an allocated FP register: operate on FP registers
        // directly instead of round-tripping through x0.
        if self.operand_fp_reg(lhs).is_some() || self.operand_fp_reg(rhs).is_some()
            || self.dest_fp_reg(dest).is_some()
        {
            let (s0, s1) = if ty == IrType::F32 { ("s0", "s1") } else { ("d0", "d1") };
            let a = self.float_operand_to_fp_reg(lhs, ty, s0);
            let b = self.float_operand_to_fp_reg(rhs, ty, s1);
            let d = self.dest_fp_reg(dest).unwrap_or_else(|| s0.to_string());
            self.state.emit_fmt(format_args!("    {} {}, {}, {}", mnemonic, d, a, b));
            self.finish_float_result(dest, &d, ty);
            return;
        }
        self.operand_to_x0(lhs);
        self.state.emit("    mov x1, x0");
        self.operand_to_x0(rhs);
        self.emit_float_binop_body(mnemonic, ty);
        self.store_x0_to(dest);
    }

    pub(super) fn emit_float_binop_body(&mut self, mnemonic: &str, ty: IrType) {
        self.state.emit("    mov x2, x0");
        if ty == IrType::F32 {
            self.state.emit("    fmov s0, w1");
            self.state.emit("    fmov s1, w2");
            self.state.emit_fmt(format_args!("    {} s0, s0, s1", mnemonic));
            self.state.emit("    fmov w0, s0");
            self.state.emit("    mov w0, w0"); // zero-extend
        } else {
            self.state.emit("    fmov d0, x1");
            self.state.emit("    fmov d1, x2");
            self.state.emit_fmt(format_args!("    {} d0, d0, d1", mnemonic));
            self.state.emit("    fmov x0, d0");
        }
    }

    pub(super) fn emit_f128_neg_impl(&mut self, dest: &Value, src: &Operand) {
        self.emit_f128_neg_full(dest, src);
    }
}

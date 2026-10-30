//! ArmCodegen: cast operations.

use crate::ir::reexports::{Operand, Value};
use crate::common::types::IrType;
use crate::backend::cast::{CastKind, classify_cast};
use super::emit::{ArmCodegen, callee_saved_name, callee_saved_name_32};

impl ArmCodegen {
    pub(super) fn emit_cast_instrs_impl(&mut self, from_ty: IrType, to_ty: IrType) {
        match classify_cast(from_ty, to_ty) {
            CastKind::Noop | CastKind::UnsignedToSignedSameSize { .. } => {}

            CastKind::FloatToSigned { from_f64 } => {
                if from_f64 {
                    self.state.emit("    fmov d0, x0");
                    self.state.emit("    fcvtzs x0, d0");
                } else {
                    self.state.emit("    fmov s0, w0");
                    self.state.emit("    fcvtzs x0, s0");
                }
                match to_ty {
                    IrType::I8 => self.state.emit("    sxtb x0, w0"),
                    IrType::I16 => self.state.emit("    sxth x0, w0"),
                    IrType::I32 => self.state.emit("    sxtw x0, w0"),
                    _ => {}
                }
            }

            CastKind::FloatToUnsigned { from_f64, .. } => {
                if from_f64 {
                    self.state.emit("    fmov d0, x0");
                    self.state.emit("    fcvtzu x0, d0");
                } else {
                    self.state.emit("    fmov s0, w0");
                    self.state.emit("    fcvtzu x0, s0");
                }
                match to_ty {
                    IrType::U8 => self.state.emit("    and x0, x0, #0xff"),
                    IrType::U16 => self.state.emit("    and x0, x0, #0xffff"),
                    IrType::U32 => self.state.emit("    mov w0, w0"),
                    _ => {}
                }
            }

            CastKind::SignedToFloat { to_f64, from_ty } => {
                match from_ty.size() {
                    1 => self.state.emit("    sxtb x0, w0"),
                    2 => self.state.emit("    sxth x0, w0"),
                    4 => self.state.emit("    sxtw x0, w0"),
                    _ => {}
                }
                if to_f64 {
                    self.state.emit("    scvtf d0, x0");
                    self.state.emit("    fmov x0, d0");
                } else {
                    self.state.emit("    scvtf s0, x0");
                    self.state.emit("    fmov w0, s0");
                }
            }

            CastKind::UnsignedToFloat { to_f64, .. } => {
                if to_f64 {
                    self.state.emit("    ucvtf d0, x0");
                    self.state.emit("    fmov x0, d0");
                } else {
                    self.state.emit("    ucvtf s0, x0");
                    self.state.emit("    fmov w0, s0");
                }
            }

            CastKind::FloatToFloat { widen } => {
                if widen {
                    self.state.emit("    fmov s0, w0");
                    self.state.emit("    fcvt d0, s0");
                    self.state.emit("    fmov x0, d0");
                } else {
                    self.state.emit("    fmov d0, x0");
                    self.state.emit("    fcvt s0, d0");
                    self.state.emit("    fmov w0, s0");
                }
            }

            CastKind::SignedToUnsignedSameSize { to_ty } => {
                match to_ty {
                    IrType::U8 => self.state.emit("    and x0, x0, #0xff"),
                    IrType::U16 => self.state.emit("    and x0, x0, #0xffff"),
                    IrType::U32 => self.state.emit("    mov w0, w0"),
                    _ => {}
                }
            }

            CastKind::IntWiden { from_ty, .. } => {
                if from_ty.is_unsigned() {
                    match from_ty {
                        IrType::U8 => self.state.emit("    and x0, x0, #0xff"),
                        IrType::U16 => self.state.emit("    and x0, x0, #0xffff"),
                        IrType::U32 => self.state.emit("    mov w0, w0"),
                        _ => {}
                    }
                } else {
                    match from_ty {
                        IrType::I8 => self.state.emit("    sxtb x0, w0"),
                        IrType::I16 => self.state.emit("    sxth x0, w0"),
                        IrType::I32 => self.state.emit("    sxtw x0, w0"),
                        _ => {}
                    }
                }
            }

            CastKind::IntNarrow { to_ty } => {
                match to_ty {
                    IrType::I8 => self.state.emit("    sxtb x0, w0"),
                    IrType::U8 => self.state.emit("    and x0, x0, #0xff"),
                    IrType::I16 => self.state.emit("    sxth x0, w0"),
                    IrType::U16 => self.state.emit("    and x0, x0, #0xffff"),
                    IrType::I32 => self.state.emit("    sxtw x0, w0"),
                    IrType::U32 => self.state.emit("    mov w0, w0"),
                    _ => {}
                }
            }

            CastKind::SignedToF128 { .. }
            | CastKind::UnsignedToF128 { .. }
            | CastKind::F128ToSigned { .. }
            | CastKind::F128ToUnsigned { .. }
            | CastKind::FloatToF128 { .. }
            | CastKind::F128ToFloat { .. } => {
                unreachable!("F128 cast variants not produced by classify_cast()");
            }
        }
    }

    pub(super) fn emit_cast_impl(&mut self, dest: &Value, src: &Operand, from_ty: IrType, to_ty: IrType) {
        if self.mul_high_parts.contains(&dest.0) {
            return;
        }
        if let Some(&(x, m, signed)) = self.mul_highs.get(&dest.0) {
            let src = match self.operand_reg(&x) {
                Some(r) => callee_saved_name(r),
                None => {
                    self.operand_to_x0(&x);
                    "x0"
                }
            };
            self.emit_load_imm64("x1", m);
            let d = self.dest_reg(dest).map_or("x0", callee_saved_name);
            self.state.emit_fmt(format_args!("    {} {}, {}, x1", if signed { "smulh" } else { "umulh" }, d, src));
            self.state.reg_cache.invalidate_acc();
            if d == "x0" {
                self.store_x0_to(dest);
            }
            return;
        }
        if crate::backend::f128_softfloat::f128_emit_cast(self, dest, src, from_ty, to_ty) {
            return;
        }
        if self.emit_int_cast_in_regs(dest, src, from_ty, to_ty) {
            return;
        }
        crate::backend::traits::emit_cast_default(self, dest, src, from_ty, to_ty);
    }

    /// An integer cast of a value in a register, written straight to the
    /// dest's register (`and x13, x12, #0xffff` rather than through x0 and a
    /// move). The extensions are those emit_cast_instrs_impl makes.
    fn emit_int_cast_in_regs(&mut self, dest: &Value, src: &Operand, from_ty: IrType, to_ty: IrType) -> bool {
        let int = |t: IrType| t.is_integer() && t.size() <= 8;
        if !int(from_ty) || !int(to_ty) {
            return false;
        }
        let Some(s) = self.operand_reg(src) else { return false };
        let ext = match classify_cast(from_ty, to_ty) {
            CastKind::Noop | CastKind::UnsignedToSignedSameSize { .. } => None,
            CastKind::IntNarrow { to_ty: t } | CastKind::SignedToUnsignedSameSize { to_ty: t } => Some(t),
            CastKind::IntWiden { from_ty: t, .. } => Some(t),
            _ => return false,
        };
        let d = self.dest_reg(dest).map_or("x0", callee_saved_name);
        let (sx, sw) = (callee_saved_name(s), callee_saved_name_32(s));
        let dw = d.replacen('x', "w", 1);
        match ext {
            Some(IrType::I8) => self.state.emit_fmt(format_args!("    sxtb {}, {}", d, sw)),
            Some(IrType::U8) => self.state.emit_fmt(format_args!("    and {}, {}, #0xff", d, sx)),
            Some(IrType::I16) => self.state.emit_fmt(format_args!("    sxth {}, {}", d, sw)),
            Some(IrType::U16) => self.state.emit_fmt(format_args!("    and {}, {}, #0xffff", d, sx)),
            Some(IrType::I32) => self.state.emit_fmt(format_args!("    sxtw {}, {}", d, sw)),
            Some(IrType::U32) => self.state.emit_fmt(format_args!("    mov {}, {}", dw, sw)),
            _ => {
                if d != sx {
                    self.state.emit_fmt(format_args!("    mov {}, {}", d, sx));
                }
            }
        }
        if d == "x0" {
            self.state.reg_cache.invalidate_acc();
            self.store_x0_to(dest);
        } else {
            self.state.reg_cache.invalidate_acc();
        }
        true
    }
}

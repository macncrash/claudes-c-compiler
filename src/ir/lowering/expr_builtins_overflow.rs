//! Overflow-checking arithmetic builtins for the lowerer.
//!
//! Handles __builtin_{s,u}{add,sub,mul}{,l,ll}_overflow and the generic
//! __builtin_{add,sub,mul}_overflow variants. These builtins perform an
//! arithmetic operation, store the result through a pointer, and return
//! 1 (bool true) if the operation overflowed.
//!
//! Also handles __builtin_{add,sub,mul}_overflow_p(a, b, (T)0) predicate-only
//! variants (GCC 7+) which return 1 if the operation would overflow type T,
//! without storing the result.

use crate::common::types::AliasClass;
use crate::frontend::parser::ast::{Expr};
use crate::ir::reexports::{
    Instruction,
    IrBinOp,
    IrCmpOp,
    IrConst,
    Operand,
    Value,
};
use crate::common::types::{AddressSpace, IrType, CType, target_int_ir_type, target_is_32bit};
use super::lower::Lowerer;

/// An overflow builtin's operand or result: its IR type and whether it is signed.
type OvType = (IrType, bool);

impl Lowerer {
    /// Lower __builtin_{add,sub,mul}_overflow(a, b, result_ptr) and type-specific variants.
    ///
    /// These builtins perform an arithmetic operation, store the result through
    /// the pointer, and return 1 (bool true) if the operation overflowed: if the
    /// infinitely precise result does not fit the result type.
    pub(super) fn lower_overflow_builtin(&mut self, name: &str, args: &[Expr], op: IrBinOp) -> Option<Operand> {
        if args.len() < 3 {
            return Some(Operand::Const(IrConst::I64(0)));
        }

        // The result type: from the name of a type-specific variant
        // (__builtin_sadd_overflow: int, __builtin_uaddll_overflow: unsigned long long)
        // or from the pointer argument of a generic one (__builtin_add_overflow).
        let res = self.overflow_result_type(name, &args[2]);
        let is_generic = matches!(name, "__builtin_add_overflow" | "__builtin_sub_overflow" | "__builtin_mul_overflow");

        let lhs_raw = self.lower_expr(&args[0]);
        let rhs_raw = self.lower_expr(&args[1]);
        let lhs = self.overflow_operand(&args[0], lhs_raw, is_generic, res);
        let rhs = self.overflow_operand(&args[1], rhs_raw, is_generic, res);

        let result_ptr_val = self.lower_expr(&args[2]);
        let result_ptr = self.operand_to_value(result_ptr_val);

        // The flag is computed before the store: the output pointer may alias an input.
        let (result, overflow) = self.emit_exact_overflow(op, lhs, rhs, res);
        self.emit(Instruction::Store { val: Operand::Value(result), ptr: result_ptr, ty: res.0,
         seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY });
        Some(Operand::Value(overflow))
    }

    /// Lower __builtin_{add,sub,mul}_overflow_p(a, b, (T)0) predicate-only variants.
    ///
    /// These return 1 if the operation would overflow type T, 0 otherwise.
    /// The third argument is a value expression whose type determines T (the value itself is ignored).
    pub(super) fn lower_overflow_p_builtin(&mut self, args: &[Expr], op: IrBinOp) -> Option<Operand> {
        if args.len() < 3 {
            return Some(Operand::Const(IrConst::I64(0)));
        }

        // The third argument determines the result type (only its type matters, not its value).
        let result_ctype = self.expr_ctype(&args[2]);
        let res = (IrType::from_ctype(&result_ctype), result_ctype.is_signed());

        let lhs_raw = self.lower_expr(&args[0]);
        let rhs_raw = self.lower_expr(&args[1]);
        // Lower but discard the third argument (it may have side effects, though
        // in practice it's always a cast of 0)
        let _ = self.lower_expr(&args[2]);

        let lhs = self.overflow_operand(&args[0], lhs_raw, true, res);
        let rhs = self.overflow_operand(&args[1], rhs_raw, true, res);
        let (_, overflow) = self.emit_exact_overflow(op, lhs, rhs, res);
        Some(Operand::Value(overflow))
    }

    /// An operand with its type: its own for the generic builtins, converted to
    /// the result type for the type-specific ones (whose parameters have it).
    fn overflow_operand(&mut self, expr: &Expr, val: Operand, is_generic: bool, res: OvType) -> (Operand, IrType, bool) {
        let ctype = self.expr_ctype(expr);
        let ty = IrType::from_ctype(&ctype);
        if is_generic {
            (val, ty, ctype.is_signed())
        } else if ty != res.0 {
            (Operand::Value(self.emit_cast_val(val, ty, res.0)), res.0, res.1)
        } else {
            (val, res.0, res.1)
        }
    }

    /// The result of `a op b` wrapped to the result type, and the overflow flag:
    /// whether the infinitely precise result does not fit the result type.
    fn emit_exact_overflow(&mut self, op: IrBinOp, a: (Operand, IrType, bool), b: (Operand, IrType, bool), res: OvType) -> (Value, Value) {
        let bits = |t: IrType| (t.size() * 8) as u32;
        let (rt, rs) = res;

        // Operands of the result type, up to 64 bits (and 128-bit add and sub):
        // the operation in that type and its own overflow test.
        if a.1 == rt && b.1 == rt && a.2 == rs && b.2 == rs && (bits(rt) <= 64 || op != IrBinOp::Mul) {
            let result = self.emit_binop_val(op, a.0, b.0, rt);
            let overflow = if rs {
                self.compute_signed_overflow(op, a.0, b.0, result, rt)
            } else {
                self.compute_unsigned_overflow(op, a.0, b.0, result, rt)
            };
            return (result, overflow);
        }

        // The bits a signed type needs to hold every value of an operand, and the
        // exact result. The compute type must also be wider than an unsigned result
        // type, so that a negative result is told from a large one.
        let need = |t: IrType, signed: bool| bits(t) + u32::from(!signed);
        let (na, nb) = (need(a.1, a.2), need(b.1, b.2));
        let exact = if op == IrBinOp::Mul { na + nb } else { na.max(nb) + 1 };
        let required = exact.max(need(rt, rs));
        if required <= 128 {
            // Exact in a signed compute type: overflow iff the truncated result
            // does not extend back to the exact one.
            let ct = if required <= 32 { IrType::I32 } else if required <= 64 { IrType::I64 } else { IrType::I128 };
            let x = self.overflow_cast(a.0, a.1, ct);
            let y = self.overflow_cast(b.0, b.1, ct);
            let wide = self.emit_binop_val(op, x, y, ct);
            if ct == rt {
                let zero = self.emit_binop_val(IrBinOp::And, Operand::Const(IrConst::I32(0)), Operand::Const(IrConst::I32(0)), IrType::I32);
                return (wide, zero);
            }
            let truncated = self.emit_cast_val(Operand::Value(wide), ct, rt);
            let back = self.emit_cast_val(Operand::Value(truncated), rt, ct);
            let overflow = self.emit_cmp_val(IrCmpOp::Ne, Operand::Value(wide), Operand::Value(back), ct);
            return (truncated, overflow);
        }
        self.emit_sign_magnitude_overflow(op, a, b, res)
    }

    fn overflow_cast(&mut self, v: Operand, from: IrType, to: IrType) -> Operand {
        if from == to { v } else { Operand::Value(self.emit_cast_val(v, from, to)) }
    }

    /// The exact result needs more than 128 bits (an unsigned __int128 operand with
    /// a signed one or a signed result, a 128-bit multiply, 64-bit unsigned
    /// products): each operand as a sign and a 128-bit magnitude, combined with
    /// exact carry and product checks, then compared with the result type's range.
    fn emit_sign_magnitude_overflow(&mut self, op: IrBinOp, a: (Operand, IrType, bool), b: (Operand, IrType, bool), res: OvType) -> (Value, Value) {
        let u = IrType::U128;
        let i32t = IrType::I32;
        let (rt, rs) = res;
        let x = self.overflow_cast(a.0, a.1, if a.2 { IrType::I128 } else { u });
        let y = self.overflow_cast(b.0, b.1, if b.2 { IrType::I128 } else { u });

        // The stored value is the operation wrapped to the result's width.
        let full = self.emit_binop_val(op, x, y, u);
        let result = if rt.size() == 16 { full } else { self.emit_cast_val(Operand::Value(full), u, rt) };

        let (na, ma) = self.sign_magnitude(x, a.2);
        let (nb, mb) = self.sign_magnitude(y, b.2);
        let nb = if op == IrBinOp::Sub {
            Operand::Value(self.emit_binop_val(IrBinOp::Xor, nb, Operand::Const(IrConst::I32(1)), i32t))
        } else {
            nb
        };
        let zero = Operand::Const(IrConst::I128(0));
        let (neg, mag, mag_over) = if op == IrBinOp::Mul {
            let neg = self.emit_binop_val(IrBinOp::Xor, na, nb, i32t);
            let mag = self.emit_binop_val(IrBinOp::Mul, ma, mb, u);
            // The magnitude product wrapped iff mag / ma != mb (ma != 0).
            let a_nonzero = self.emit_cmp_val(IrCmpOp::Ne, ma, zero, u);
            let a_zero = self.emit_cmp_val(IrCmpOp::Eq, ma, zero, u);
            let a_zero = self.emit_cast_val(Operand::Value(a_zero), i32t, u);
            let divisor = self.emit_binop_val(IrBinOp::Or, ma, Operand::Value(a_zero), u);
            let q = self.emit_binop_val(IrBinOp::UDiv, Operand::Value(mag), Operand::Value(divisor), u);
            let differs = self.emit_cmp_val(IrCmpOp::Ne, Operand::Value(q), mb, u);
            let over = self.emit_binop_val(IrBinOp::And, Operand::Value(a_nonzero), Operand::Value(differs), i32t);
            (neg, mag, over)
        } else {
            // Same signs: the magnitudes add (a carry is too large for any result).
            // Different signs: the smaller is taken from the larger, whose sign it keeps.
            let same = self.emit_binop_val(IrBinOp::Xor, na, nb, i32t);
            let same = self.emit_binop_val(IrBinOp::Xor, Operand::Value(same), Operand::Const(IrConst::I32(1)), i32t);
            let sum = self.emit_binop_val(IrBinOp::Add, ma, mb, u);
            let carry = self.emit_cmp_val(IrCmpOp::Ult, Operand::Value(sum), ma, u);
            let a_ge = self.emit_cmp_val(IrCmpOp::Uge, ma, mb, u);
            let d1 = self.emit_binop_val(IrBinOp::Sub, ma, mb, u);
            let d2 = self.emit_binop_val(IrBinOp::Sub, mb, ma, u);
            let diff = self.mask_select(a_ge, d1, d2);
            let mag = self.mask_select(same, sum, diff);
            let keep_a = self.emit_binop_val(IrBinOp::Or, Operand::Value(same), Operand::Value(a_ge), i32t);
            let neg = self.fresh_value();
            self.emit(Instruction::Select { dest: neg, cond: Operand::Value(keep_a), true_val: na, false_val: nb, ty: i32t });
            let over = self.emit_binop_val(IrBinOp::And, Operand::Value(same), Operand::Value(carry), i32t);
            (neg, mag, over)
        };

        // Does the exact value -mag (neg) or mag fit the result type?
        let rbits = rt.size() as u32 * 8;
        let max: u128 = if rs { (1u128 << (rbits - 1)) - 1 } else { u128::MAX >> (128 - rbits) };
        let max = Operand::Const(IrConst::I128(max as i128));
        let out_of_range = if rs {
            // Up to max for a positive value, max + 1 for a negative one.
            let neg_wide = self.emit_cast_val(Operand::Value(neg), i32t, u);
            let limit = self.emit_binop_val(IrBinOp::Add, max, Operand::Value(neg_wide), u);
            self.emit_cmp_val(IrCmpOp::Ugt, Operand::Value(mag), Operand::Value(limit), u)
        } else {
            let nonzero = self.emit_cmp_val(IrCmpOp::Ne, Operand::Value(mag), zero, u);
            let negative = self.emit_binop_val(IrBinOp::And, Operand::Value(neg), Operand::Value(nonzero), i32t);
            let too_big = self.emit_cmp_val(IrCmpOp::Ugt, Operand::Value(mag), max, u);
            self.emit_binop_val(IrBinOp::Or, Operand::Value(negative), Operand::Value(too_big), i32t)
        };
        let overflow = self.emit_binop_val(IrBinOp::Or, Operand::Value(out_of_range), Operand::Value(mag_over), i32t);
        (result, overflow)
    }

    /// The sign (I32 0/1) and the 128-bit magnitude of a 128-bit operand.
    fn sign_magnitude(&mut self, x: Operand, signed: bool) -> (Operand, Operand) {
        if !signed {
            return (Operand::Const(IrConst::I32(0)), x);
        }
        let i = IrType::I128;
        let m = self.emit_binop_val(IrBinOp::AShr, x, Operand::Const(IrConst::I64(127)), i);
        let flipped = self.emit_binop_val(IrBinOp::Xor, x, Operand::Value(m), IrType::U128);
        let mag = self.emit_binop_val(IrBinOp::Sub, Operand::Value(flipped), Operand::Value(m), IrType::U128);
        let neg = self.emit_cmp_val(IrCmpOp::Slt, x, Operand::Const(IrConst::I128(0)), i);
        (Operand::Value(neg), Operand::Value(mag))
    }

    /// cond (I32 0/1) ? t : f on 128-bit values, by masks.
    fn mask_select(&mut self, cond: Value, t: Value, f: Value) -> Value {
        let u = IrType::U128;
        let c = self.emit_cast_val(Operand::Value(cond), IrType::I32, u);
        let mask = self.emit_binop_val(IrBinOp::Sub, Operand::Const(IrConst::I128(0)), Operand::Value(c), u);
        let not_mask = self.emit_binop_val(IrBinOp::Xor, Operand::Value(mask), Operand::Const(IrConst::I128(-1)), u);
        let tv = self.emit_binop_val(IrBinOp::And, Operand::Value(t), Operand::Value(mask), u);
        let fv = self.emit_binop_val(IrBinOp::And, Operand::Value(f), Operand::Value(not_mask), u);
        self.emit_binop_val(IrBinOp::Or, Operand::Value(tv), Operand::Value(fv), u)
    }
    /// Determine the result IrType and signedness for an overflow builtin.
    fn overflow_result_type(&self, name: &str, result_ptr_expr: &Expr) -> (IrType, bool) {
        // Check type-specific variants first
        // __builtin_sadd_overflow -> signed int
        // __builtin_saddl_overflow -> signed long
        // __builtin_saddll_overflow -> signed long long
        // __builtin_uadd_overflow -> unsigned int
        // etc.
        // The width is the suffix after the operation: none (int), l (long) or ll
        // (long long). Judged by the name's ending, umul and smul (whose "mul" ends
        // in l) were taken as long and never overflowed for int values.
        let typed = name.strip_prefix("__builtin_").and_then(|s| s.strip_suffix("_overflow")).and_then(|s| {
            let signed = match s.as_bytes().first() { Some(b's') => true, Some(b'u') => false, _ => return None };
            let suffix = ["add", "sub", "mul"].iter().find_map(|op| s[1..].strip_prefix(op))?;
            Some((signed, suffix))
        });
        if let Some((is_signed, suffix)) = typed {
            let ty = match (suffix, is_signed) {
                ("ll", true) => IrType::I64,
                ("ll", false) => IrType::U64,
                ("l", true) => target_int_ir_type(), // long: I64 on LP64, I32 on ILP32
                ("l", false) => if target_is_32bit() { IrType::U32 } else { IrType::U64 },
                (_, true) => IrType::I32,
                (_, false) => IrType::U32,
            };
            return (ty, is_signed);
        }

        // Generic variant: determine type from the pointer argument's pointee type
        let result_ctype = self.expr_ctype(result_ptr_expr);
        if let CType::Pointer(pointee, _) = &result_ctype {
            let is_signed = pointee.is_signed();
            let ir_ty = IrType::from_ctype(pointee);
            return (ir_ty, is_signed);
        }

        // If we can't determine the type (e.g., &local), try to get the
        // pointee from the expression structure
        if let Expr::AddressOf(inner, _) = result_ptr_expr {
            let inner_ctype = self.expr_ctype(inner);
            let is_signed = inner_ctype.is_signed();
            let ir_ty = IrType::from_ctype(&inner_ctype);
            return (ir_ty, is_signed);
        }

        // Default to signed long if we can't determine
        (target_int_ir_type(), true)
    }

    /// Compute signed overflow flag for add/sub/mul.
    fn compute_signed_overflow(
        &mut self,
        op: IrBinOp,
        lhs: Operand,
        rhs: Operand,
        result: Value,
        ty: IrType,
    ) -> Value {
        let bits = (ty.size() * 8) as i64;
        match op {
            IrBinOp::Add => {
                // Signed add overflow: ((result ^ lhs) & (result ^ rhs)) < 0
                // i.e., overflow if both operands have same sign and result has different sign
                let xor_lhs = self.emit_binop_val(IrBinOp::Xor, Operand::Value(result), lhs, ty);
                let xor_rhs = self.emit_binop_val(IrBinOp::Xor, Operand::Value(result), rhs, ty);
                let and_val = self.emit_binop_val(IrBinOp::And, Operand::Value(xor_lhs), Operand::Value(xor_rhs), ty);
                // Check sign bit: shift right by (bits-1) and check if non-zero
                let shifted = self.emit_binop_val(IrBinOp::AShr, Operand::Value(and_val), Operand::Const(IrConst::I64(bits - 1)), ty);
                // The shifted value is all-ones (-1) if overflow, all-zeros (0) if not
                // Convert to 0/1 by AND with 1
                self.emit_binop_val(IrBinOp::And, Operand::Value(shifted), Operand::Const(IrConst::I64(1)), ty)
            }
            IrBinOp::Sub => {
                // Signed sub overflow: ((lhs ^ rhs) & (result ^ lhs)) < 0
                // overflow if operands have different signs and result sign differs from lhs
                let xor_ops = self.emit_binop_val(IrBinOp::Xor, lhs, rhs, ty);
                let xor_res = self.emit_binop_val(IrBinOp::Xor, Operand::Value(result), lhs, ty);
                let and_val = self.emit_binop_val(IrBinOp::And, Operand::Value(xor_ops), Operand::Value(xor_res), ty);
                let shifted = self.emit_binop_val(IrBinOp::AShr, Operand::Value(and_val), Operand::Const(IrConst::I64(bits - 1)), ty);
                self.emit_binop_val(IrBinOp::And, Operand::Value(shifted), Operand::Const(IrConst::I64(1)), ty)
            }
            IrBinOp::Mul => {
                // For signed multiply overflow, we widen to double width, multiply, then
                // check if the result fits in the original width by checking that
                // sign-extending the truncated result gives back the full result.
                let wide_ty = Self::double_width_type(ty, true);
                let lhs_wide = self.emit_cast_val(lhs, ty, wide_ty);
                let rhs_wide = self.emit_cast_val(rhs, ty, wide_ty);
                let wide_result = self.emit_binop_val(IrBinOp::Mul, Operand::Value(lhs_wide), Operand::Value(rhs_wide), wide_ty);
                // Truncate back to original width
                let truncated = self.emit_cast_val(Operand::Value(wide_result), wide_ty, ty);
                // Sign-extend truncated back to wide type
                let sign_extended = self.emit_cast_val(Operand::Value(truncated), ty, wide_ty);
                // Overflow if wide_result != sign_extended
                self.emit_cmp_val(IrCmpOp::Ne, Operand::Value(wide_result), Operand::Value(sign_extended), wide_ty)
            }
            _ => unreachable!("overflow only for add/sub/mul"),
        }
    }

    /// Compute unsigned overflow flag for add/sub/mul.
    fn compute_unsigned_overflow(
        &mut self,
        op: IrBinOp,
        lhs: Operand,
        rhs: Operand,
        result: Value,
        ty: IrType,
    ) -> Value {
        // Make sure we're comparing in unsigned type
        let unsigned_ty = ty.to_unsigned();
        match op {
            IrBinOp::Add => {
                // Unsigned add overflow: result < lhs (wrapping means the sum is smaller)
                self.emit_cmp_val(IrCmpOp::Ult, Operand::Value(result), lhs, unsigned_ty)
            }
            IrBinOp::Sub => {
                // Unsigned sub overflow: lhs < rhs (borrow occurred)
                self.emit_cmp_val(IrCmpOp::Ult, lhs, rhs, unsigned_ty)
            }
            IrBinOp::Mul => {
                // Unsigned multiply overflow: widen to double width, multiply, check upper half is zero
                let wide_ty = Self::double_width_type(ty, false);
                let lhs_wide = self.emit_cast_val(lhs, unsigned_ty, wide_ty);
                let rhs_wide = self.emit_cast_val(rhs, unsigned_ty, wide_ty);
                let wide_result = self.emit_binop_val(IrBinOp::Mul, Operand::Value(lhs_wide), Operand::Value(rhs_wide), wide_ty);
                // Truncate back to original width
                let truncated = self.emit_cast_val(Operand::Value(wide_result), wide_ty, unsigned_ty);
                // Zero-extend truncated back to wide type
                let zero_extended = self.emit_cast_val(Operand::Value(truncated), unsigned_ty, wide_ty);
                // Overflow if wide_result != zero_extended
                self.emit_cmp_val(IrCmpOp::Ne, Operand::Value(wide_result), Operand::Value(zero_extended), wide_ty)
            }
            _ => unreachable!("overflow only for add/sub/mul"),
        }
    }

    /// Get the double-width integer type for overflow multiply detection.
    fn double_width_type(ty: IrType, signed: bool) -> IrType {
        match ty {
            IrType::I8 | IrType::U8 => if signed { IrType::I16 } else { IrType::U16 },
            IrType::I16 | IrType::U16 => if signed { IrType::I32 } else { IrType::U32 },
            IrType::I32 | IrType::U32 => if signed { IrType::I64 } else { IrType::U64 },
            IrType::I64 | IrType::U64 => if signed { IrType::I128 } else { IrType::U128 },
            // For 128-bit, we can't widen further; use a different strategy
            // (but this is extremely rare in practice)
            _ => if signed { IrType::I128 } else { IrType::U128 },
        }
    }
}

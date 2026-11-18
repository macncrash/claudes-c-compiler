//! Fuse `sin(x)` and `cos(x)` of the same value into one libm call.
//!
//! Apple's libm computes both at once in `__sincos_stret` and
//! `__sincosf_stret`, which return `{ sin, cos }` in d0/d1 (s0/s1), an HFA:
//! ccc lowers that as an sret call whose buffer the callee's registers are
//! stored to. So `s = sinf(x); ... c = cosf(x);` in one block becomes, at
//! the first of the two calls,
//!
//! ```text
//! buf = alloca 8;  call __sincosf_stret(buf, x);  s = load buf;  c = load buf + 4
//! ```
//!
//! Neither call writes memory on Darwin (libm never sets errno there), so
//! moving the second up to the first changes nothing a program can see.
//! Runs after the memory passes, which mostly cannot see through the sret
//! buffer; mod_ref knows the call writes only it (SRET_LIBM), for the loop
//! passes that run after phi elimination.
use crate::common::types::{AddressSpace, AliasClass, IrType, RiscvFloatClass};
use crate::ir::reexports::{CallInfo, ConstHashKey, Instruction, IrConst, IrFunction, Operand, Value};

/// Fuse the pairs in `func`; returns how many were fused.
pub(crate) fn fuse_function(func: &mut IrFunction) -> usize {
    let mut fused = 0;
    for bi in 0..func.blocks.len() {
        while let Some((first, second, f32)) = find_pair(&func.blocks[bi].instructions) {
            rewrite(func, bi, first, second, f32);
            fused += 1;
        }
    }
    fused
}

/// A call's argument, compared by value number or by constant bits (so
/// 0.0 and -0.0 differ).
#[derive(Clone, PartialEq)]
enum Arg {
    Value(u32),
    Const(ConstHashKey),
}

impl Arg {
    fn of(op: &Operand) -> Arg {
        match op {
            Operand::Value(v) => Arg::Value(v.0),
            Operand::Const(c) => Arg::Const(c.to_hash_key()),
        }
    }
}

/// A plain call to sin/cos (or sinf/cosf): (is_sin, is_f32, argument, dest).
fn trig_call(inst: &Instruction) -> Option<(bool, bool, Arg, Value)> {
    let Instruction::Call { func, info } = inst else { return None };
    let (is_sin, f32) = match func.as_str() {
        "sin" => (true, false),
        "cos" => (false, false),
        "sinf" => (true, true),
        "cosf" => (false, true),
        _ => return None,
    };
    let ty = if f32 { IrType::F32 } else { IrType::F64 };
    let plain = !info.is_variadic && !info.is_sret && info.return_type == ty
        && info.args.len() == 1 && info.arg_types.first() == Some(&ty);
    let (Some(dest), Some(arg), true) = (info.dest, info.args.first(), plain) else { return None };
    Some((is_sin, f32, Arg::of(arg), dest))
}

/// The first sin/cos pair of one argument in a block, by instruction index
/// (the earlier call first), with its width. A value argument has one
/// definition (the IR is still SSA here), before both calls. No call of
/// the same argument comes before the first: it would have paired first.
fn find_pair(insts: &[Instruction]) -> Option<(usize, usize, bool)> {
    for (i, a) in insts.iter().enumerate() {
        let Some((sin_a, f32_a, arg_a, _)) = trig_call(a) else { continue };
        for (j, b) in insts.iter().enumerate().skip(i + 1) {
            let Some((sin_b, f32_b, arg_b, _)) = trig_call(b) else { continue };
            if sin_a != sin_b && f32_a == f32_b && arg_a == arg_b {
                return Some((i, j, f32_a));
            }
        }
    }
    None
}

fn fresh(func: &mut IrFunction) -> Value {
    let v = Value(func.next_value_id);
    func.next_value_id += 1;
    v
}

fn rewrite(func: &mut IrFunction, bi: usize, first: usize, second: usize, f32: bool) {
    let (sin_first, _, key, dest_first) = trig_call(&func.blocks[bi].instructions[first]).unwrap();
    let (_, _, _, dest_second) = trig_call(&func.blocks[bi].instructions[second]).unwrap();
    let (sin_dest, cos_dest) = if sin_first { (dest_first, dest_second) } else { (dest_second, dest_first) };
    let Instruction::Call { info, .. } = &func.blocks[bi].instructions[first] else { unreachable!() };
    let arg = info.args[0];
    // Later calls of either with this argument and width: copies of the
    // fused results (the calls read no memory, so the results are equal).
    for inst in func.blocks[bi].instructions.iter_mut().skip(second + 1) {
        if let Some((is_sin, f, k, dest)) = trig_call(inst) {
            if f == f32 && k == key {
                *inst = Instruction::Copy { dest, src: Operand::Value(if is_sin { sin_dest } else { cos_dest }) };
            }
        }
    }
    let (ty, n) = if f32 { (IrType::F32, 4) } else { (IrType::F64, 8) };
    let (buf, cos_ptr) = (fresh(func), fresh(func));

    let load = |dest, ptr| Instruction::Load {
        dest, ptr, ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY,
    };
    let call = Instruction::Call {
        func: if f32 { "__sincosf_stret" } else { "__sincos_stret" }.to_string(),
        info: CallInfo {
            dest: None,
            args: vec![Operand::Value(buf), arg],
            arg_types: vec![IrType::Ptr, ty],
            return_type: IrType::Ptr,
            is_variadic: false,
            num_fixed_args: 2,
            struct_arg_sizes: vec![None, None],
            struct_arg_aligns: vec![None, None],
            struct_arg_classes: vec![Vec::new(), Vec::new()],
            struct_arg_riscv_float_classes: vec![Some(RiscvFloatClass::Hfa { count: 2, elem_size: n }), None],
            is_sret: true,
            is_fastcall: false,
            ret_eightbyte_classes: Vec::new(),
        },
    };
    let insts = &mut func.blocks[bi].instructions;
    insts.remove(second);
    insts.splice(first..=first, [
        Instruction::Alloca { dest: buf, ty: IrType::Ptr, size: 2 * n, align: n, volatile: false },
        call,
        load(sin_dest, buf),
        Instruction::GetElementPtr { dest: cos_ptr, base: buf, offset: Operand::Const(IrConst::I64(n as i64)), ty: IrType::I8 },
        load(cos_dest, cos_ptr),
    ]);
}

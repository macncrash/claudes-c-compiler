//! Block execution profiles (-fprofile-generate=DIR, -fprofile-use=DIR).
//!
//! The profile point is the end of the IR pipeline, just before shrink_split:
//! decisions made there and in the backend (which values split at a slow
//! path, which blocks a register allocator weighs most) can use real counts
//! instead of static estimates. Compilation is deterministic, so a build with
//! the same source and options reaches that point with the same blocks.
//!
//! With -fprofile-generate, each function's blocks bump counters in one
//! array per translation unit, `__ccc_prof_<id>` (`<id>` hashing the source
//! file's path), and the compiler writes `DIR/<id>.map`: a header line, then
//! for each counter its function, that function's shape hash and the
//! block's label. A destructor appends the array, as little-endian 64-bit
//! counts, to `DIR/<id>.cnt` when the program exits; runs accumulate.
//!
//! With -fprofile-use, the counts of every run are summed and each function
//! whose shape hash still matches gets its blocks' counts
//! (IrFunction::profile). A function that changed, or a file never run, keeps
//! the static estimates.

use crate::common::fx_hash::FxHashMap;
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::reexports::{
    BasicBlock, BlockId, CallInfo, Instruction, IrBinOp, IrCmpOp, IrConst, IrFunction, IrModule, Operand, Terminator, Value,
};
use crate::ir::module::{GlobalInit, IrGlobal};

/// FNV-1a, 64 bits.
fn fnv(bytes: impl IntoIterator<Item = u8>, mut h: u64) -> u64 {
    for b in bytes {
        h ^= b as u64;
        h = h.wrapping_mul(0x100_0000_01b3);
    }
    h
}

const FNV_START: u64 = 0xcbf2_9ce4_8422_2325;

/// The id of a translation unit's profile files: a hash of its source
/// file's full path.
pub(crate) fn unit_id(input_file: &str) -> String {
    let path = std::fs::canonicalize(input_file).map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_else(|_| input_file.to_string());
    format!("{:016x}", fnv(path.bytes(), FNV_START))
}

/// A hash of a function's blocks: labels, instruction kinds, terminators.
fn shape_hash(func: &IrFunction) -> u64 {
    let mut h = fnv(func.name.bytes(), FNV_START);
    for block in &func.blocks {
        h = fnv(block.label.0.to_le_bytes(), h);
        for inst in &block.instructions {
            let tag = format!("{:?}", std::mem::discriminant(inst));
            h = fnv(tag.bytes(), h);
        }
        let succ: Vec<u32> = match &block.terminator {
            Terminator::Branch(t) => vec![t.0],
            Terminator::CondBranch { true_label, false_label, .. } => vec![true_label.0, false_label.0],
            Terminator::Switch { cases, default, .. } => cases.iter().map(|c| c.1 .0).chain([default.0]).collect(),
            Terminator::IndirectBranch { possible_targets, .. } => possible_targets.iter().map(|t| t.0).collect(),
            Terminator::Return(_) => vec![u32::MAX],
            Terminator::Unreachable => vec![u32::MAX - 1],
        };
        for s in succ {
            h = fnv(s.to_le_bytes(), h);
        }
    }
    h
}

fn call(name: &str, dest: Option<Value>, args: Vec<Operand>, arg_types: Vec<IrType>, return_type: IrType) -> Instruction {
    let n = args.len();
    Instruction::Call { func: name.to_string(), info: CallInfo {
        dest, args, arg_types, return_type, is_variadic: false, num_fixed_args: n,
        struct_arg_sizes: vec![None; n], struct_arg_aligns: vec![None; n], struct_arg_classes: vec![Vec::new(); n],
        struct_arg_riscv_float_classes: vec![None; n], is_sret: false, is_fastcall: false, ret_eightbyte_classes: Vec::new(),
    } }
}

/// Add the counters, their dump at exit, and write `dir/<id>.map`.
pub(crate) fn instrument(module: &mut IrModule, id: &str, dir: &str) -> Result<(), String> {
    let counters = format!("__ccc_prof_{id}");
    let mut map = String::new();
    let mut index = 0usize;
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration && !f.is_naked) {
        let hash = shape_hash(func);
        let mut next = func.next_value_id.max(func.max_value_id() + 1);
        for block in &mut func.blocks {
            map.push_str(&format!("{} {:016x} {}\n", func.name, hash, block.label.0));
            let v: Vec<Value> = (0..4).map(|i| Value(next + i)).collect();
            next += 4;
            let bump = [
                Instruction::GlobalAddr { dest: v[0], name: counters.clone() },
                Instruction::GetElementPtr { dest: v[1], base: v[0], offset: Operand::Const(IrConst::I64(8 * index as i64)), ty: IrType::Ptr },
                Instruction::Load { dest: v[2], ptr: v[1], ty: IrType::I64, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
                Instruction::BinOp { dest: v[3], op: IrBinOp::Add, lhs: Operand::Value(v[2]), rhs: Operand::Const(IrConst::I64(1)), ty: IrType::I64 },
                Instruction::Store { val: Operand::Value(v[3]), ptr: v[1], ty: IrType::I64, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY },
            ];
            let spans = !block.source_spans.is_empty() && block.source_spans.len() == block.instructions.len();
            block.instructions.splice(0..0, bump);
            if spans {
                block.source_spans.splice(0..0, std::iter::repeat_n(crate::common::source::Span::dummy(), 5));
            }
            index += 1;
        }
        func.next_value_id = next;
    }
    if index == 0 {
        return Ok(());
    }
    std::fs::create_dir_all(dir).map_err(|e| format!("cannot create profile directory {dir}: {e}"))?;
    let map_path = format!("{dir}/{id}.map");
    std::fs::write(&map_path, format!("ccc-profile 1 {index}\n{map}")).map_err(|e| format!("cannot write {map_path}: {e}"))?;
    let cnt_path = std::fs::canonicalize(dir).map(|p| p.to_string_lossy().into_owned()).unwrap_or_else(|_| dir.to_string());
    module.globals.push(IrGlobal {
        name: counters.clone(), ty: IrType::I64, size: 8 * index, align: 8, init: GlobalInit::Zero,
        is_static: true, is_extern: false, is_common: false, section: None, is_weak: false, visibility: None,
        has_explicit_align: true, is_const: false, is_used: true, is_thread_local: false,
    });
    let (path_label, mode_label) = (format!(".Lccc_prof_path_{id}"), format!(".Lccc_prof_mode_{id}"));
    module.string_literals.push((path_label.clone(), format!("{cnt_path}/{id}.cnt")));
    module.string_literals.push((mode_label.clone(), "ab".to_string()));
    // __ccc_prof_dump_<id>: if (f = fopen(path, "ab")) { fwrite(counters, 8, N, f); fclose(f); }
    let first = module.functions.iter().flat_map(|f| &f.blocks).map(|b| b.label.0 + 1).max().unwrap_or(0);
    let (open, write, done) = (BlockId(first), BlockId(first + 1), BlockId(first + 2));
    let name = format!("__ccc_prof_dump_{id}");
    let mut dump = IrFunction::new(name.clone(), IrType::Void, Vec::new(), false);
    dump.is_static = true;
    let v = |n: u32| Operand::Value(Value(n));
    let k = |n: i64| Operand::Const(IrConst::I64(n));
    dump.blocks = vec![
        BasicBlock { label: open, instructions: vec![
            Instruction::GlobalAddr { dest: Value(0), name: path_label },
            Instruction::GlobalAddr { dest: Value(1), name: mode_label },
            call("fopen", Some(Value(2)), vec![v(0), v(1)], vec![IrType::Ptr, IrType::Ptr], IrType::Ptr),
            Instruction::Cmp { dest: Value(3), op: IrCmpOp::Eq, lhs: v(2), rhs: k(0), ty: IrType::I64 },
        ], terminator: Terminator::CondBranch { cond: v(3), true_label: done, false_label: write }, source_spans: Vec::new() },
        BasicBlock { label: write, instructions: vec![
            Instruction::GlobalAddr { dest: Value(4), name: counters },
            call("fwrite", Some(Value(5)), vec![v(4), k(8), k(index as i64), v(2)],
                 vec![IrType::Ptr, IrType::I64, IrType::I64, IrType::Ptr], IrType::I64),
            call("fclose", Some(Value(6)), vec![v(2)], vec![IrType::Ptr], IrType::I32),
        ], terminator: Terminator::Branch(done), source_spans: Vec::new() },
        BasicBlock { label: done, instructions: Vec::new(), terminator: Terminator::Return(None), source_spans: Vec::new() },
    ];
    dump.next_value_id = 7;
    module.functions.push(dump);
    module.destructors.push(name);
    Ok(())
}

/// Read `dir/<id>.map` and `dir/<id>.cnt` and give each function whose
/// shape still matches its blocks' counts. Missing files leave the module
/// as it is.
pub(crate) fn load(module: &mut IrModule, id: &str, dir: &str) {
    let Ok(map) = std::fs::read_to_string(format!("{dir}/{id}.map")) else { return };
    let Ok(raw) = std::fs::read(format!("{dir}/{id}.cnt")) else { return };
    let mut lines = map.lines();
    let Some(n) = lines.next().and_then(|h| h.strip_prefix("ccc-profile 1 ")).and_then(|n| n.parse::<usize>().ok()) else { return };
    if n == 0 || raw.len() % (8 * n) != 0 {
        return;
    }
    let mut counts = vec![0u64; n];
    for run in raw.chunks(8 * n) {
        for (i, c) in run.chunks(8).enumerate() {
            counts[i] = counts[i].saturating_add(u64::from_le_bytes(c.try_into().unwrap()));
        }
    }
    // Function -> (shape hash, label -> count).
    let mut by_func: FxHashMap<&str, (u64, FxHashMap<u32, u64>)> = FxHashMap::default();
    for (i, line) in lines.enumerate().take(n) {
        let mut parts = line.split(' ');
        let (Some(f), Some(h), Some(l)) = (parts.next(), parts.next(), parts.next()) else { return };
        let (Ok(h), Ok(l)) = (u64::from_str_radix(h, 16), l.parse::<u32>()) else { return };
        by_func.entry(f).or_insert_with(|| (h, FxHashMap::default())).1.insert(l, counts[i]);
    }
    for func in module.functions.iter_mut().filter(|f| !f.is_declaration) {
        if let Some((h, labels)) = by_func.get(func.name.as_str()) {
            if *h == shape_hash(func) {
                func.profile = Some(labels.clone());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// f: entry -> (loop) -> exit.
    fn module() -> IrModule {
        let mut m = IrModule::new();
        let mut f = IrFunction::new("f".to_string(), IrType::Void, vec![], false);
        let ret = |l| BasicBlock { label: BlockId(l), instructions: vec![], terminator: Terminator::Return(None), source_spans: vec![] };
        let br = |l, t| BasicBlock { label: BlockId(l), instructions: vec![], terminator: Terminator::Branch(BlockId(t)), source_spans: vec![] };
        f.blocks = vec![br(0, 1), br(1, 2), ret(2)];
        m.functions.push(f);
        m
    }

    #[test]
    fn counts_come_back_to_blocks_of_unchanged_functions() {
        let dir = std::env::temp_dir().join(format!("ccc-prof-test-{}", std::process::id()));
        let dir = dir.to_string_lossy().into_owned();
        let mut m = module();
        instrument(&mut m, "t", &dir).unwrap();
        // Three counters bumped first in each block, a dump function as a destructor.
        assert!(m.functions[0].blocks.iter().all(|b| matches!(b.instructions[0], Instruction::GlobalAddr { .. })));
        assert_eq!(m.destructors, ["__ccc_prof_dump_t"]);
        assert_eq!(m.globals[0].size, 24);
        // Two runs' counts, summed.
        let run = |a: u64, b: u64, c: u64| [a, b, c].iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>();
        std::fs::write(format!("{dir}/t.cnt"), [run(1, 5, 1), run(2, 7, 2)].concat()).unwrap();
        let mut m = module();
        load(&mut m, "t", &dir);
        let p = m.functions[0].profile.as_ref().expect("profile loaded");
        assert_eq!((p[&0], p[&1], p[&2]), (3, 12, 3));
        // A function whose blocks changed keeps the static estimates.
        let mut m = module();
        m.functions[0].blocks[1].terminator = Terminator::Return(None);
        load(&mut m, "t", &dir);
        assert!(m.functions[0].profile.is_none());
        let _ = std::fs::remove_dir_all(&dir);
    }
}

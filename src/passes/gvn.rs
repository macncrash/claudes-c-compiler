//! Dominator-based Global Value Numbering (GVN) pass.
//!
//! This pass assigns "value numbers" to expressions and replaces redundant
//! computations with references to previously computed values (CSE).
//!
//! The pass walks the dominator tree in DFS order with scoped hash tables,
//! so expressions computed in dominating blocks are visible to all dominated
//! blocks. On backtracking, the hash tables are restored to their previous
//! state (same scoping pattern as rename_block in mem2reg).
//!
//! Value-numbered instruction types:
//! - BinOp (with commutative operand canonicalization)
//! - UnaryOp
//! - Cmp
//! - Cast (type-to-type conversions)
//! - GetElementPtr (base + offset address computation)
//! - Load (redundant load elimination within dominator scope, invalidated
//!   by stores, calls, and other memory-clobbering instructions; a store
//!   leaves what it cannot overlap: other fields of its struct, and with
//!   strict aliasing loads and stores of other alias classes)

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::types::{AddressSpace, AliasClass, IrType};
use crate::ir::reexports::{
    ConstHashKey,
    Instruction,
    IrBinOp,
    IrCmpOp,
    IrFunction,
    IrUnaryOp,
    Operand,
    Value,
};
use crate::ir::analysis;
use super::mod_ref::ModRef;

/// A value number expression key. Two instructions with the same ExprKey
/// compute the same value (assuming their operands are equivalent).
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum ExprKey {
    BinOp { op: IrBinOp, lhs: VNOperand, rhs: VNOperand, ty: IrType },
    UnaryOp { op: IrUnaryOp, src: VNOperand, ty: IrType },
    Cmp { op: IrCmpOp, lhs: VNOperand, rhs: VNOperand, ty: IrType },
    Cast { src: VNOperand, from_ty: IrType, to_ty: IrType },
    Gep { base: VNOperand, offset: VNOperand, ty: IrType },
    /// Load CSE key: two loads from the same pointer with the same type
    /// produce the same value if no intervening memory modification occurs.
    /// The pointer is its root (before any chain of constant-offset GEPs)
    /// and the byte offset from it, so differently typed GEPs of one
    /// address are one location.
    Load { ptr: VNOperand, off: i64, ty: IrType },
    /// A symbol's address, the same everywhere in the function.
    GlobalAddr { name: String },
}

/// Returns true if the ExprKey represents a Load (memory-dependent expression).
impl ExprKey {
    fn is_load(&self) -> bool {
        matches!(self, ExprKey::Load { .. })
    }
}

/// A value-numbered operand: either a constant or a value number.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum VNOperand {
    Const(ConstHashKey),
    ValueNum(u32),
}

/// The most loads, and stores, at one root a store to it carries over.
const MAX_KEPT: usize = 64;

/// Key for store-to-load forwarding: identifies a memory location by pointer
/// value number and access type.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct StoreFwdKey {
    ptr_vn: VNOperand,
    off: i64,
    ty: IrType,
}

/// A forwardable store: (stored operand, load generation, local generation,
/// whether the address is local-only). Valid while the load generation is
/// current, or for a local-only address while the local generation is.
type StoreFwdEntry = (Operand, u32, u32, bool, Tag);

/// An entry's alias class, and whether it is valid only for loads of that
/// class: one kept across a store of another class (strict aliasing) is.
/// (An access of class ANY, such as a byte copy, may read what that store
/// wrote.)
type Tag = (AliasClass, bool);

/// A cached load: (value, load generation, tag).
type LoadEntry = (Value, u32, Tag);

/// The most loads, and stores, of one alias class a store of another class
/// carries over.
const MAX_CLASS_KEPT: usize = 16;

/// The key under which by_root lists the entries of an alias class (value
/// numbers stay far below it).
fn class_list(class: AliasClass) -> u32 {
    u32::MAX - 256 + class.0 as u32
}

/// Whether an entry tagged tag serves a load of class class.
fn serves(tag: Tag, class: AliasClass) -> bool {
    !tag.1 || tag.0 == class
}

/// Mutable state for the GVN pass, threaded through the dominator-tree DFS.
///
/// Groups the value numbering tables, expression maps, and rollback logs that
/// were previously passed as 9 separate `&mut` parameters. The rollback logs
/// enable scoped hash table semantics: on entering a dominator-tree subtree,
/// save the log positions; on backtracking, restore entries to undo changes
/// made in that subtree.
struct GvnState {
    /// Maps Value ID -> value number. Indexed by `Value.0`.
    value_numbers: Vec<u32>,
    /// Next value number to assign.
    next_vn: u32,
    /// Pure expression -> canonical value (not memory-dependent).
    expr_to_value: FxHashMap<ExprKey, Value>,
    /// Load expression -> (canonical value, generation). Separated from
    /// `expr_to_value` so loads can be invalidated independently via
    /// generation bumping.
    load_expr_to_value: FxHashMap<ExprKey, LoadEntry>,
    /// Generation counter for O(1) load CSE invalidation. When a memory-
    /// clobbering instruction is encountered, bump this counter; cached
    /// load entries with older generations are considered stale.
    load_generation: u32,
    /// Like `load_generation`, but not bumped by calls: a call cannot write a
    /// local whose address never escapes (`local_only`), so a store to one
    /// stays forwardable across calls while this generation is current.
    local_generation: u32,
    /// Addresses of non-escaping locals: allocas, and GEPs into them, used
    /// only as load/store addresses or GEP bases (see `find_local_only_addresses`).
    local_only: FxHashSet<u32>,
    /// Store-to-load forwarding map: (pointer VN, type) -> (stored operand, generation).
    /// When a Store writes a value to a pointer, we record it here. A subsequent
    /// Load from the same pointer (same VN) with matching type and generation can
    /// be replaced with a Copy of the stored value, eliminating the load entirely.
    /// Uses the same generation counter as load CSE for O(1) invalidation.
    store_fwd_map: FxHashMap<StoreFwdKey, StoreFwdEntry>,
    /// Rollback log for `expr_to_value`: (key, previous_value).
    rollback_log: Vec<(ExprKey, Option<Value>)>,
    /// Rollback log for `load_expr_to_value`: (key, previous_entry).
    load_rollback_log: Vec<(ExprKey, Option<LoadEntry>)>,
    /// Rollback log for `store_fwd_map`: (key, previous_entry).
    store_fwd_rollback_log: Vec<(StoreFwdKey, Option<StoreFwdEntry>)>,
    /// Rollback log for `value_numbers`: (index, previous_vn).
    vn_log: Vec<(usize, u32)>,
    /// Total instructions eliminated across all blocks.
    total_eliminated: usize,
    /// Set of param alloca Value IDs whose address has escaped (used in
    /// non-Load/Store contexts). Store-to-load forwarding is disabled for
    /// these allocas because the backend's ParamRef optimization reads
    /// parameter values from the alloca slot, which may be modified by
    /// stores through aliased pointers.
    escaped_param_allocas: FxHashSet<u32>,
    /// Calls (block, instruction index) to functions that write no memory
    /// the caller can see, which leave cached loads valid.
    silent_calls: FxHashSet<(usize, usize)>,
    /// Pointer value -> (root, byte offset) through constant-offset GEPs.
    locs: FxHashMap<u32, (Value, i64)>,
    /// Root value number -> the load and store keys recorded at it, so a
    /// store to the root keeps those it cannot overlap (stale keys are
    /// skipped by their generation).
    by_root: FxHashMap<u32, RootKeys>,
    /// Rollback log for `by_root`: each root's lists as they were before
    /// the first change to them in a scope.
    by_root_log: Vec<(u32, Option<RootKeys>)>,
    /// The current scope's number, and the scope in which each root's lists
    /// were last logged.
    scope: u32,
    next_scope: u32,
    by_root_logged: FxHashMap<u32, u32>,
}

type RootKeys = (Vec<ExprKey>, Vec<StoreFwdKey>);

impl GvnState {
    /// Create a new GVN state sized for `max_value_id` values.
    fn new(max_value_id: usize, escaped_param_allocas: FxHashSet<u32>, local_only: FxHashSet<u32>) -> Self {
        Self {
            value_numbers: vec![u32::MAX; max_value_id + 1],
            next_vn: 0,
            expr_to_value: FxHashMap::default(),
            load_expr_to_value: FxHashMap::default(),
            load_generation: 0,
            local_generation: 0,
            local_only,
            store_fwd_map: FxHashMap::default(),
            rollback_log: Vec::new(),
            load_rollback_log: Vec::new(),
            store_fwd_rollback_log: Vec::new(),
            vn_log: Vec::new(),
            total_eliminated: 0,
            escaped_param_allocas,
            silent_calls: FxHashSet::default(),
            locs: FxHashMap::default(),
            by_root: FxHashMap::default(),
            by_root_log: Vec::new(),
            scope: 0,
            next_scope: 1,
            by_root_logged: FxHashMap::default(),
        }
    }

    /// The (root value number, byte offset) a pointer addresses.
    fn location(&mut self, ptr: Value) -> (VNOperand, i64) {
        let (root, off) = self.locs.get(&ptr.0).copied().unwrap_or((ptr, 0));
        (self.operand_to_vn(&Operand::Value(root)), off)
    }

    /// The lists at `root`, logged first if this scope has not changed them
    /// yet, so leaving the scope restores them for its siblings.
    fn root_keys(&mut self, root: u32) -> &mut RootKeys {
        if self.by_root_logged.insert(root, self.scope) != Some(self.scope) {
            self.by_root_log.push((root, self.by_root.get(&root).cloned()));
        }
        self.by_root.entry(root).or_default()
    }

    fn note_load_root(&mut self, key: &ExprKey, class: AliasClass) {
        if let ExprKey::Load { ptr: VNOperand::ValueNum(r), .. } = key {
            let loads = &mut self.root_keys(*r).0;
            loads.push(key.clone());
            trim_keys(loads);
        }
        if class != AliasClass::ANY {
            let loads = &mut self.root_keys(class_list(class)).0;
            loads.push(key.clone());
            trim_keys(loads);
        }
    }

    fn note_store_root(&mut self, key: &StoreFwdKey, class: AliasClass) {
        if let VNOperand::ValueNum(r) = key.ptr_vn {
            let stores = &mut self.root_keys(r).1;
            stores.push(key.clone());
            trim_keys(stores);
        }
        if class != AliasClass::ANY {
            let stores = &mut self.root_keys(class_list(class)).1;
            stores.push(key.clone());
            trim_keys(stores);
        }
    }

    /// The current entries of known alias classes other than class, which
    /// a store of class class cannot write without undefined behaviour;
    /// each is then valid only for loads of its own class.
    #[allow(clippy::type_complexity)]
    fn other_class_entries(&mut self, class: AliasClass) -> (Vec<(ExprKey, Value, Tag)>, Vec<(StoreFwdKey, StoreFwdEntry)>) {
        let gen = self.load_generation;
        let (mut loads, mut stores) = (Vec::new(), Vec::new());
        for c in 1..=AliasClass::PTR.0 {
            let c = AliasClass(c);
            if c == class {
                continue;
            }
            let Some((lk, sk)) = self.by_root.get(&class_list(c)) else { continue };
            for key in lk.iter().rev().take(MAX_CLASS_KEPT) {
                if let Some(&(v, g, t)) = self.load_expr_to_value.get(key) {
                    if g == gen && t.0 == c {
                        loads.push((key.clone(), v, (c, true)));
                    }
                }
            }
            for key in sk.iter().rev().take(MAX_CLASS_KEPT) {
                if let Some(&e) = self.store_fwd_map.get(key) {
                    if e.1 == gen && e.4.0 == c {
                        stores.push((key.clone(), (e.0, e.1, e.2, e.3, (c, true))));
                    }
                }
            }
        }
        (loads, stores)
    }

    /// The current load and store entries at the root of `ptr` whose bytes
    /// an access of `size` bytes at `ptr` cannot overlap.
    #[allow(clippy::type_complexity)]
    fn disjoint_entries(&mut self, ptr: Value, size: i64) -> (Vec<(ExprKey, Value, Tag)>, Vec<(StoreFwdKey, StoreFwdEntry)>) {
        let (VNOperand::ValueNum(root), off) = self.location(ptr) else { return (Vec::new(), Vec::new()) };
        if !self.by_root.contains_key(&root) {
            return (Vec::new(), Vec::new());
        }
        let (loads, stores) = std::mem::take(self.root_keys(root));
        let apart = |o: i64, ty: IrType| o + ty.size() as i64 <= off || off + size <= o;
        let gen = self.load_generation;
        let mut kept_loads = Vec::new();
        let mut seen: FxHashSet<ExprKey> = FxHashSet::default();
        for key in loads.into_iter().rev().take(MAX_KEPT) {
            let ExprKey::Load { off: o, ty, .. } = key else { continue };
            if let Some(&(v, g, t)) = self.load_expr_to_value.get(&key) {
                if g == gen && apart(o, ty) && seen.insert(key.clone()) {
                    kept_loads.push((key, v, t));
                }
            }
        }
        let mut kept_stores = Vec::new();
        let mut seen: FxHashSet<StoreFwdKey> = FxHashSet::default();
        for key in stores.into_iter().rev().take(MAX_KEPT) {
            if let Some(&entry) = self.store_fwd_map.get(&key) {
                if entry.1 == gen && apart(key.off, key.ty) && seen.insert(key.clone()) {
                    kept_stores.push((key, entry));
                }
            }
        }
        // Only the kept keys stay listed: the rest are stale after the store
        // (leaving the scope restores them with the generation they were
        // current in). At most the MAX_KEPT most recent of each kind are
        // kept, so a run of stores to one root (a large struct copied field
        // by field) stays linear.
        let lists = self.by_root.entry(root).or_default();
        lists.0.extend(kept_loads.iter().rev().map(|(k, ..)| k.clone()));
        lists.1.extend(kept_stores.iter().rev().map(|(k, _)| k.clone()));
        (kept_loads, kept_stores)
    }

    /// Carry entries kept across a store into the generation after it.
    #[allow(clippy::type_complexity)]
    fn renew(&mut self, (loads, stores): (Vec<(ExprKey, Value, Tag)>, Vec<(StoreFwdKey, StoreFwdEntry)>)) {
        for (key, v, t) in loads {
            let old = self.load_expr_to_value.insert(key.clone(), (v, self.load_generation, t));
            self.load_rollback_log.push((key, old));
        }
        for (key, (op, _, _, local, t)) in stores {
            let old = self.store_fwd_map.insert(key.clone(), (op, self.load_generation, self.local_generation, local, t));
            self.store_fwd_rollback_log.push((key, old));
        }
    }

    /// Assign a fresh value number, returning it.
    fn fresh_vn(&mut self) -> u32 {
        let vn = self.next_vn;
        self.next_vn += 1;
        vn
    }

    /// Assign a fresh value number to `dest` and record it in the rollback log.
    fn assign_fresh_vn(&mut self, dest: Value) {
        let vn = self.fresh_vn();
        let idx = dest.0 as usize;
        if idx < self.value_numbers.len() {
            let old_vn = self.value_numbers[idx];
            self.vn_log.push((idx, old_vn));
            self.value_numbers[idx] = vn;
        }
    }

    /// Convert an Operand to a VNOperand for hashing.
    /// If the value hasn't been assigned a value number yet (e.g. a function
    /// parameter or an alloca whose definition appears later in the block),
    /// assign it a fresh unique VN on the spot to avoid collisions between
    /// different un-numbered values and already-assigned VNs.
    fn operand_to_vn(&mut self, op: &Operand) -> VNOperand {
        match op {
            Operand::Const(c) => VNOperand::Const(c.to_hash_key()),
            Operand::Value(v) => {
                let idx = v.0 as usize;
                // Ensure the table is large enough
                if idx >= self.value_numbers.len() {
                    self.value_numbers.resize(idx + 1, u32::MAX);
                }
                if self.value_numbers[idx] != u32::MAX {
                    VNOperand::ValueNum(self.value_numbers[idx])
                } else {
                    // Assign a fresh VN to this previously un-numbered value
                    let vn = self.fresh_vn();
                    let old_vn = self.value_numbers[idx];
                    self.vn_log.push((idx, old_vn));
                    self.value_numbers[idx] = vn;
                    VNOperand::ValueNum(vn)
                }
            }
        }
    }

    /// Try to create an ExprKey for an instruction (for value numbering).
    /// Returns the expression key and the destination value, or None if
    /// the instruction is not eligible for value numbering.
    fn make_expr_key(&mut self, inst: &Instruction) -> Option<(ExprKey, Value)> {
        match inst {
            Instruction::BinOp { dest, op, lhs, rhs, ty } => {
                let lhs_vn = self.operand_to_vn(lhs);
                let rhs_vn = self.operand_to_vn(rhs);

                // For commutative operations, canonicalize operand order
                let (lhs_vn, rhs_vn) = if op.is_commutative() {
                    canonical_order(lhs_vn, rhs_vn)
                } else {
                    (lhs_vn, rhs_vn)
                };

                Some((ExprKey::BinOp { op: *op, lhs: lhs_vn, rhs: rhs_vn, ty: *ty }, *dest))
            }
            Instruction::UnaryOp { dest, op, src, ty } => {
                let src_vn = self.operand_to_vn(src);
                Some((ExprKey::UnaryOp { op: *op, src: src_vn, ty: *ty }, *dest))
            }
            Instruction::Cmp { dest, op, lhs, rhs, ty } => {
                let lhs_vn = self.operand_to_vn(lhs);
                let rhs_vn = self.operand_to_vn(rhs);
                Some((ExprKey::Cmp { op: *op, lhs: lhs_vn, rhs: rhs_vn, ty: *ty }, *dest))
            }
            Instruction::Cast { dest, src, from_ty, to_ty } => {
                // Casts between a 128-bit type and a float one are library
                // calls; integer extensions to and truncations from 128 bits
                // are numbered (the repeated multiply-highs of a quotient and
                // remainder by one constant, as div_by_const expands them).
                if (from_ty.is_128bit() || to_ty.is_128bit()) && !(from_ty.is_integer() && to_ty.is_integer()) {
                    return None;
                }
                let src_vn = self.operand_to_vn(src);
                Some((ExprKey::Cast { src: src_vn, from_ty: *from_ty, to_ty: *to_ty }, *dest))
            }
            Instruction::GetElementPtr { dest, base, offset, ty } => {
                let base_vn = self.operand_to_vn(&Operand::Value(*base));
                let offset_vn = self.operand_to_vn(offset);
                Some((ExprKey::Gep { base: base_vn, offset: offset_vn, ty: *ty }, *dest))
            }
            // Load CSE: two loads from the same pointer with the same type can be
            // CSE'd if no intervening memory modification occurred. The caller
            // (process_block) handles invalidating Load entries on memory clobbers.
            //
            // Excluded from CSE:
            // - Segment-overridden loads: access thread-local or CPU-local storage
            //   that may differ between accesses even without visible stores
            // - Volatile loads: each must happen as written
            // - Float, long double, i128 types: use different register paths in
            //   codegen that complicate Copy instruction handling
            // - AtomicLoad: has ordering semantics (falls through to _ => None)
            Instruction::Load { dest, ptr, ty, seg_override, volatile, .. } => {
                if *volatile || *seg_override != AddressSpace::Default {
                    return None;
                }
                if ty.is_float() || ty.is_long_double() || ty.is_128bit() {
                    return None;
                }
                let (ptr_vn, off) = self.location(*ptr);
                Some((ExprKey::Load { ptr: ptr_vn, off, ty: *ty }, *dest))
            }
            Instruction::GlobalAddr { dest, name } => Some((ExprKey::GlobalAddr { name: name.clone() }, *dest)),
            // Other instructions (Store, Call, AtomicLoad, etc.) are not eligible.
            // AtomicLoad is excluded because it has memory ordering semantics that
            // require the load to actually execute.
            _ => None,
        }
    }

    /// Save the current log positions for later rollback.
    fn save_scope(&mut self) -> ScopeCheckpoint {
        let saved_scope = self.scope;
        self.scope = self.next_scope;
        self.next_scope += 1;
        ScopeCheckpoint {
            by_root_log_start: self.by_root_log.len(),
            saved_scope,
            rollback_start: self.rollback_log.len(),
            load_rollback_start: self.load_rollback_log.len(),
            store_fwd_rollback_start: self.store_fwd_rollback_log.len(),
            vn_log_start: self.vn_log.len(),
            saved_load_generation: self.load_generation,
            saved_local_generation: self.local_generation,
        }
    }

    /// Restore state to a previously saved checkpoint, undoing all changes
    /// made since the checkpoint was taken.
    fn restore_scope(&mut self, checkpoint: &ScopeCheckpoint) {
        // Rollback: restore expr_to_value
        while self.rollback_log.len() > checkpoint.rollback_start {
            let (key, old_val) = self.rollback_log.pop()
                .expect("rollback_log length checked by while condition");
            if let Some(val) = old_val {
                self.expr_to_value.insert(key, val);
            } else {
                self.expr_to_value.remove(&key);
            }
        }

        // Rollback: restore load_expr_to_value
        while self.load_rollback_log.len() > checkpoint.load_rollback_start {
            let (key, old_val) = self.load_rollback_log.pop()
                .expect("load_rollback_log length checked by while condition");
            if let Some(val) = old_val {
                self.load_expr_to_value.insert(key, val);
            } else {
                self.load_expr_to_value.remove(&key);
            }
        }

        // Rollback: restore store_fwd_map
        while self.store_fwd_rollback_log.len() > checkpoint.store_fwd_rollback_start {
            let (key, old_val) = self.store_fwd_rollback_log.pop()
                .expect("store_fwd_rollback_log length checked by while condition");
            if let Some(val) = old_val {
                self.store_fwd_map.insert(key, val);
            } else {
                self.store_fwd_map.remove(&key);
            }
        }

        // Rollback: restore value_numbers
        while self.vn_log.len() > checkpoint.vn_log_start {
            let (idx, old_vn) = self.vn_log.pop()
                .expect("vn_log length checked by while condition");
            self.value_numbers[idx] = old_vn;
        }

        // Rollback: restore by_root
        while self.by_root_log.len() > checkpoint.by_root_log_start {
            let (root, old) = self.by_root_log.pop()
                .expect("by_root_log length checked by while condition");
            match old {
                Some(lists) => { self.by_root.insert(root, lists); }
                None => { self.by_root.remove(&root); }
            }
        }
        self.scope = checkpoint.saved_scope;

        // Rollback: restore load_generation
        self.load_generation = checkpoint.saved_load_generation;
        self.local_generation = checkpoint.saved_local_generation;
    }
}

/// Saved positions in the rollback logs, used by `GvnState::save_scope` /
/// `GvnState::restore_scope` to implement scoped hash table semantics.
struct ScopeCheckpoint {
    by_root_log_start: usize,
    saved_scope: u32,
    rollback_start: usize,
    load_rollback_start: usize,
    store_fwd_rollback_start: usize,
    vn_log_start: usize,
    saved_load_generation: u32,
    saved_local_generation: u32,
}

/// Find param allocas whose address has escaped (used in non-Load/Store contexts).
///
/// When a param alloca's address is taken (e.g., via `&x` which becomes a GEP
/// then gets simplified to a Copy), the alloca can be modified through aliased
/// pointers. Store-to-load forwarding must be disabled for these allocas because
/// the backend's ParamRef optimization reads parameter values from the alloca
/// slot at point of use, not at point of definition. If an aliased store
/// modifies the alloca between the original store and the forwarded use, the
/// read will return the wrong value.
fn find_escaped_param_allocas(func: &IrFunction) -> FxHashSet<u32> {
    let param_alloca_set: FxHashSet<u32> = func.param_alloca_values.iter().map(|v| v.0).collect();
    if param_alloca_set.is_empty() {
        return FxHashSet::default();
    }

    let mut escaped = FxHashSet::default();

    for block in &func.blocks {
        for inst in &block.instructions {
            match inst {
                // Load from param alloca is fine - it reads the value
                Instruction::Load { ptr, .. } => {
                    // ptr is the alloca value itself, this is a normal use
                    let _ = ptr;
                }
                // Store TO param alloca is fine - it writes to the alloca
                Instruction::Store { ptr, val, .. } => {
                    // ptr is the alloca, this is fine
                    let _ = ptr;
                    // But if the alloca value is used as a stored VALUE
                    // (i.e., its address is being stored somewhere), it escapes
                    if let Operand::Value(v) = val {
                        if param_alloca_set.contains(&v.0) {
                            escaped.insert(v.0);
                        }
                    }
                }
                // Copy of param alloca = address taken (e.g., simplified &x GEP)
                Instruction::Copy { src: Operand::Value(v), .. } => {
                    if param_alloca_set.contains(&v.0) {
                        escaped.insert(v.0);
                    }
                }
                // Any other instruction using the alloca value means escape
                _ => {
                    inst.for_each_used_value(|vid| {
                        if param_alloca_set.contains(&vid) {
                            escaped.insert(vid);
                        }
                    });
                }
            }
        }
    }

    escaped
}

/// Find addresses of locals whose address never escapes: non-volatile allocas,
/// and GEPs into them, where every address derived from the alloca is used
/// only as a Load/Store address or a GEP base. No call can write such a
/// local (a pointer to it is never passed on or stored anywhere).
fn find_local_only_addresses(func: &IrFunction) -> FxHashSet<u32> {
    let insts = || func.blocks.iter().flat_map(|b| &b.instructions);
    // Each candidate address -> the alloca it derives from.
    let mut root: FxHashMap<u32, u32> = FxHashMap::default();
    for inst in insts() {
        if let Instruction::Alloca { dest, volatile: false, .. } = inst {
            root.insert(dest.0, dest.0);
        }
    }
    if root.is_empty() {
        return FxHashSet::default();
    }
    loop {
        let mut changed = false;
        for inst in insts() {
            if let Instruction::GetElementPtr { dest, base, .. } = inst {
                if let Some(&r) = root.get(&base.0) {
                    if root.insert(dest.0, r).is_none() {
                        changed = true;
                    }
                }
            }
        }
        if !changed {
            break;
        }
    }
    let mut escaped: FxHashSet<u32> = FxHashSet::default();
    for inst in insts() {
        let mut escape = |v: u32| {
            if let Some(&r) = root.get(&v) {
                escaped.insert(r);
            }
        };
        match inst {
            Instruction::Load { .. } => {}
            Instruction::Store { val: Operand::Value(v), .. } => escape(v.0),
            Instruction::Store { .. } => {}
            Instruction::GetElementPtr { offset: Operand::Value(v), .. } => escape(v.0),
            Instruction::GetElementPtr { .. } => {}
            _ => inst.for_each_used_value(escape),
        }
    }
    root.into_iter().filter(|(_, r)| !escaped.contains(r)).map(|(v, _)| v).collect()
}

/// Run dominator-based GVN on a single function.
pub(crate) fn run_gvn_function(func: &mut IrFunction) -> usize {
    run_gvn_function_with(func, None)
}

/// GVN on one function, letting calls that write nothing visible (per
/// `mr`) keep cached loads.
pub(crate) fn run_gvn_function_with(func: &mut IrFunction, mr: Option<&ModRef>) -> usize {
    let num_blocks = func.blocks.len();
    if num_blocks == 0 {
        return 0;
    }

    let escaped = find_escaped_param_allocas(func);

    // Fast path for single-block functions: skip CFG/dominator computation.
    if num_blocks == 1 {
        let mut state = GvnState::new(func.max_value_id() as usize, escaped, find_local_only_addresses(func));
        state.silent_calls = silent_calls(func, mr);
        state.locs = pointer_locations(func);
        return process_block(0, func, &mut state);
    }

    // Build CFG and dominator tree
    let cfg = analysis::CfgAnalysis::build(func);
    run_gvn_with_analysis(func, &cfg, mr)
}

/// Run GVN using pre-computed CFG analysis (avoids redundant analysis when
/// called from a pipeline that shares analysis across GVN, LICM, IVSR).
pub(crate) fn run_gvn_with_analysis(func: &mut IrFunction, cfg: &analysis::CfgAnalysis, mr: Option<&ModRef>) -> usize {
    let num_blocks = func.blocks.len();
    if num_blocks == 0 {
        return 0;
    }

    let escaped = find_escaped_param_allocas(func);

    // Fast path for single-block functions.
    if num_blocks == 1 {
        let mut state = GvnState::new(func.max_value_id() as usize, escaped, find_local_only_addresses(func));
        state.silent_calls = silent_calls(func, mr);
        state.locs = pointer_locations(func);
        return process_block(0, func, &mut state);
    }

    let mut state = GvnState::new(func.max_value_id() as usize, escaped, find_local_only_addresses(func));
    state.silent_calls = silent_calls(func, mr);
    state.locs = pointer_locations(func);

    // DFS over the dominator tree
    gvn_dfs(0, func, &cfg.dom_children, &cfg.preds, &mut state);

    state.total_eliminated
}

/// Drop the oldest keys of a root's list once it holds twice the most a
/// store looks at, which keeps the copies `root_keys` logs small.
fn trim_keys<K>(keys: &mut Vec<K>) {
    if keys.len() > 2 * MAX_KEPT {
        keys.drain(..keys.len() - MAX_KEPT);
    }
}

/// Each constant-offset GEP's (root, byte offset): the pointer at the start
/// of its chain of such GEPs, and their offsets summed.
pub(crate) fn pointer_locations(func: &IrFunction) -> FxHashMap<u32, (Value, i64)> {
    let mut step: FxHashMap<u32, (Value, i64)> = FxHashMap::default();
    for inst in func.blocks.iter().flat_map(|b| &b.instructions) {
        if let Instruction::GetElementPtr { dest, base, offset: Operand::Const(c), .. } = inst {
            if let Some(k) = c.to_i64().filter(|k| k.unsigned_abs() < 1 << 31) {
                step.insert(dest.0, (*base, k));
            }
        }
    }
    let mut locs = FxHashMap::default();
    for &v in step.keys() {
        let (mut root, mut off) = step[&v];
        // Chains are short; the bound only guards against a malformed cycle.
        for _ in 0..64 {
            let Some(&(b, k)) = step.get(&root.0) else { break };
            root = b;
            off += k;
        }
        if off.unsigned_abs() < 1 << 40 {
            locs.insert(v, (root, off));
        }
    }
    locs
}

/// Positions of the calls in `func` that write no memory the caller can see.
fn silent_calls(func: &IrFunction, mr: Option<&ModRef>) -> FxHashSet<(usize, usize)> {
    let Some(mr) = mr else { return FxHashSet::default() };
    let mut out = FxHashSet::default();
    for (b, block) in func.blocks.iter().enumerate() {
        for (k, inst) in block.instructions.iter().enumerate() {
            if mr.writes_nothing(inst) {
                out.insert((b, k));
            }
        }
    }
    out
}

/// Recursive DFS over the dominator tree for GVN.
/// Processes block_idx, then recurses into dominated children.
/// Uses rollback logs to restore state on backtracking.
fn gvn_dfs(
    block_idx: usize,
    func: &mut IrFunction,
    dom_children: &[Vec<usize>],
    preds: &analysis::FlatAdj,
    state: &mut GvnState,
) {
    let checkpoint = state.save_scope();

    // At block entry, decide whether to invalidate inherited Load CSE entries.
    // Load CSE across blocks is safe when this block has exactly one CFG
    // predecessor (straight-line code). At merge points (multiple predecessors),
    // conservatively invalidate all Load entries because a non-dominating
    // predecessor may have stored to memory, making cached loads stale.
    //
    // Invalidation is O(1): just bump load_generation. Entries with older
    // generations are ignored during lookup.
    if block_idx != 0 && preds.len(block_idx) > 1 {
        state.load_generation += 1;
        state.local_generation += 1;
    }

    // Process instructions in this block
    let eliminated = process_block(block_idx, func, state);
    state.total_eliminated += eliminated;

    // Recurse into dominator tree children.
    // Iterate by index to avoid cloning the children Vec.
    let num_children = dom_children[block_idx].len();
    for ci in 0..num_children {
        let child = dom_children[block_idx][ci];
        gvn_dfs(child, func, dom_children, preds, state);
    }

    state.restore_scope(&checkpoint);
}

/// Check if an instruction may modify memory, invalidating cached load values.
/// This is conservative: any instruction that could write to memory or call
/// external code (which could write to memory) returns true.
fn clobbers_memory(inst: &Instruction) -> bool {
    matches!(
        inst,
        Instruction::Store { .. }
            | Instruction::Call { .. }
            | Instruction::CallIndirect { .. }
            | Instruction::Memcpy { .. }
            | Instruction::AtomicRmw { .. }
            | Instruction::AtomicCmpxchg { .. }
            | Instruction::AtomicStore { .. }
            | Instruction::Fence { .. }
            | Instruction::InlineAsm { .. }
            | Instruction::VaStart { .. }
            | Instruction::VaEnd { .. }
            | Instruction::VaCopy { .. }
    ) || matches!(inst, Instruction::Intrinsic { dest_ptr: Some(_), .. })
}

/// Check if a Store instruction is eligible for store-to-load forwarding.
/// Same restrictions as Load CSE: no segment overrides, no volatile, no
/// float/long-double/i128 types.
fn is_forwardable_store(inst: &Instruction) -> bool {
    match inst {
        Instruction::Store { ty, seg_override, volatile, .. } => {
            *seg_override == AddressSpace::Default
                && !volatile
                && !ty.is_float()
                && !ty.is_long_double()
                && !ty.is_128bit()
        }
        _ => false,
    }
}

/// Process a single basic block for GVN.
/// Returns the number of instructions eliminated.
///
/// Load CSE entries are stored separately in `state.load_expr_to_value`, tagged
/// with a generation counter for O(1) invalidation on memory clobber. Cross-block
/// Load CSE propagation is controlled by `gvn_dfs` which invalidates Load
/// entries at merge points (blocks with multiple CFG predecessors).
///
/// Store-to-load forwarding: when a Store writes value V to pointer P, subsequent
/// Loads from P (same VN, same type, no intervening memory clobber) are replaced
/// with Copy(V). This eliminates redundant loads after stores, a common pattern
/// in struct initialization, local variable access, etc.
fn process_block(
    block_idx: usize,
    func: &mut IrFunction,
    state: &mut GvnState,
) -> usize {
    let mut eliminated = 0;
    let mut new_instructions = Vec::with_capacity(func.blocks[block_idx].instructions.len());
    // GVN replaces instructions 1:1 (original or Copy), so spans stay parallel
    let new_spans = std::mem::take(&mut func.blocks[block_idx].source_spans);

    let instructions = std::mem::take(&mut func.blocks[block_idx].instructions);
    for (k, inst) in instructions.into_iter().enumerate() {
        // Before processing the instruction, check if it clobbers memory.
        // If so, invalidate all cached Load CSE and store forwarding entries
        // by bumping the generation counter. This is O(1) instead of iterating
        // all keys.
        if clobbers_memory(&inst) && !state.silent_calls.contains(&(block_idx, k)) {
            // A store to a constant offset from a root leaves the loads and
            // stores at other offsets from that same root, which it cannot
            // overlap.
            let mut kept = match &inst {
                Instruction::Store { ptr, ty, volatile: false, seg_override: AddressSpace::Default, .. } =>
                    state.disjoint_entries(*ptr, ty.size() as i64),
                _ => (Vec::new(), Vec::new()),
            };
            // With strict aliasing, nor those of other types.
            if let Instruction::Store { alias, volatile: false, seg_override: AddressSpace::Default, .. } = &inst {
                if *alias != AliasClass::ANY {
                    let (loads, stores) = state.other_class_entries(*alias);
                    let have: FxHashSet<ExprKey> = kept.0.iter().map(|(k, ..)| k.clone()).collect();
                    kept.0.extend(loads.into_iter().filter(|(k, ..)| !have.contains(k)));
                    let have: FxHashSet<StoreFwdKey> = kept.1.iter().map(|(k, _)| k.clone()).collect();
                    kept.1.extend(stores.into_iter().filter(|(k, _)| !have.contains(k)));
                }
            }
            state.load_generation += 1;
            if !matches!(inst, Instruction::Call { .. } | Instruction::CallIndirect { .. }) {
                state.local_generation += 1;
            }
            state.renew(kept);
        }

        // Store-to-load forwarding: record stored values for subsequent loads.
        // This happens AFTER the generation bump (since Store itself clobbers memory),
        // so the stored value is recorded at the new generation and will be visible
        // to subsequent loads (which haven't bumped the generation yet).
        //
        // Skip forwarding for stores to escaped param allocas: the backend's
        // ParamRef optimization reads from the alloca slot at point of use, so
        // forwarding a ParamRef through an escaped alloca is unsound when an
        // aliased pointer may write to the same slot.
        if is_forwardable_store(&inst) {
            if let Instruction::Store { val, ptr, ty, alias, .. } = &inst {
                if !state.escaped_param_allocas.contains(&ptr.0) {
                    let (ptr_vn, off) = state.location(*ptr);
                    let fwd_key = StoreFwdKey { ptr_vn, off, ty: *ty };
                    let fwd_key_for_log = fwd_key.clone();
                    let local = state.local_only.contains(&ptr.0);
                    let entry = (*val, state.load_generation, state.local_generation, local, (*alias, false));
                    let old_val = state.store_fwd_map.insert(fwd_key, entry);
                    state.note_store_root(&fwd_key_for_log, *alias);
                    state.store_fwd_rollback_log.push((fwd_key_for_log, old_val));
                }
            }
            // Store has no dest, so no VN to assign. Just keep the instruction.
            new_instructions.push(inst);
            continue;
        }

        let load_class = match &inst {
            Instruction::Load { alias, .. } => *alias,
            _ => AliasClass::ANY,
        };
        match state.make_expr_key(&inst) {
            Some((expr_key, dest)) => {
                let is_load = expr_key.is_load();

                // For loads, first try store-to-load forwarding before load CSE.
                // This catches the pattern: store V -> *P; load *P -> replace with V.
                if is_load {
                    if let ExprKey::Load { ptr: ref ptr_vn, off, ty } = expr_key {
                        let fwd_key = StoreFwdKey { ptr_vn: ptr_vn.clone(), off, ty };
                        if let Some(&(stored_op, gen, local_gen, local, tag)) = state.store_fwd_map.get(&fwd_key) {
                            if (gen == state.load_generation || (local && local_gen == state.local_generation))
                                && serves(tag, load_class) {
                                // Forward the stored value to the load destination.
                                // Assign the dest a VN matching the stored value.
                                let dest_idx = dest.0 as usize;
                                let forwarded_vn = match &stored_op {
                                    Operand::Value(v) => {
                                        let idx = v.0 as usize;
                                        if idx < state.value_numbers.len() && state.value_numbers[idx] != u32::MAX {
                                            state.value_numbers[idx]
                                        } else {
                                            state.fresh_vn()
                                        }
                                    }
                                    _ => state.fresh_vn(),
                                };
                                if dest_idx < state.value_numbers.len() {
                                    let old_vn = state.value_numbers[dest_idx];
                                    state.vn_log.push((dest_idx, old_vn));
                                    state.value_numbers[dest_idx] = forwarded_vn;
                                }
                                // Also update load CSE map so subsequent loads from the
                                // same pointer can CSE with this load's dest.
                                let load_key_for_log = expr_key.clone();
                                let old_load = state.load_expr_to_value.insert(
                                    expr_key,
                                    (dest, state.load_generation, (load_class, false)),
                                );
                                state.note_load_root(&load_key_for_log, load_class);
                                state.load_rollback_log.push((load_key_for_log, old_load));
                                new_instructions.push(Instruction::Copy {
                                    dest,
                                    src: stored_op,
                                });
                                eliminated += 1;
                                continue;
                            }
                        }
                    }
                }

                // Look up: check pure expr map, or load map with generation check
                let existing = if is_load {
                    state.load_expr_to_value.get(&expr_key).and_then(|&(val, gen, tag)| {
                        if gen == state.load_generation && serves(tag, load_class) { Some(val) } else { None }
                    })
                } else {
                    state.expr_to_value.get(&expr_key).copied()
                };

                if let Some(existing_value) = existing {
                    // This expression was already computed
                    let idx = existing_value.0 as usize;
                    let existing_vn = if idx < state.value_numbers.len() && state.value_numbers[idx] != u32::MAX {
                        state.value_numbers[idx]
                    } else {
                        state.fresh_vn()
                    };
                    let dest_idx = dest.0 as usize;
                    if dest_idx < state.value_numbers.len() {
                        let old_vn = state.value_numbers[dest_idx];
                        state.vn_log.push((dest_idx, old_vn));
                        state.value_numbers[dest_idx] = existing_vn;
                    }
                    new_instructions.push(Instruction::Copy {
                        dest,
                        src: Operand::Value(existing_value),
                    });
                    eliminated += 1;
                } else {
                    // New expression - assign value number and record it
                    let vn = state.fresh_vn();
                    let dest_idx = dest.0 as usize;
                    if dest_idx < state.value_numbers.len() {
                        let old_vn = state.value_numbers[dest_idx];
                        state.vn_log.push((dest_idx, old_vn));
                        state.value_numbers[dest_idx] = vn;
                    }
                    // Record in appropriate map with rollback
                    if is_load {
                        let key_for_log = expr_key.clone();
                        let old_val = state.load_expr_to_value.insert(expr_key, (dest, state.load_generation, (load_class, false)));
                        state.note_load_root(&key_for_log, load_class);
                        state.load_rollback_log.push((key_for_log, old_val));
                    } else {
                        let key_for_log = expr_key.clone();
                        let old_val = state.expr_to_value.insert(expr_key, dest);
                        state.rollback_log.push((key_for_log, old_val));
                    }
                    new_instructions.push(inst);
                }
            }
            None => {
                // Not a numberable expression (store, call, alloca, etc.)
                if let Some(dest) = inst.dest() {
                    state.assign_fresh_vn(dest);
                }
                new_instructions.push(inst);
            }
        }
    }

    func.blocks[block_idx].instructions = new_instructions;
    func.blocks[block_idx].source_spans = new_spans;
    eliminated
}

/// Canonicalize operand order for commutative operations.
/// Ensures (a + b) and (b + a) hash to the same key.
fn canonical_order(lhs: VNOperand, rhs: VNOperand) -> (VNOperand, VNOperand) {
    if should_swap(&lhs, &rhs) {
        (rhs, lhs)
    } else {
        (lhs, rhs)
    }
}

fn should_swap(lhs: &VNOperand, rhs: &VNOperand) -> bool {
    match (lhs, rhs) {
        (VNOperand::ValueNum(_), VNOperand::Const(_)) => true,
        (VNOperand::ValueNum(a), VNOperand::ValueNum(b)) => a > b,
        (VNOperand::Const(a), VNOperand::Const(b)) => a > b,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::types::AliasClass;
    use crate::ir::reexports::{BasicBlock, BlockId, CallInfo, IrConst, IrModule, Terminator};

    #[test]
    fn test_commutative_cse() {
        // Test that a + b and b + a are recognized as the same expression
        let block = BasicBlock {
            label: BlockId(0),
            instructions: vec![
                // %0 = add %a, %b
                Instruction::BinOp {
                    dest: Value(2),
                    op: IrBinOp::Add,
                    lhs: Operand::Value(Value(0)),
                    rhs: Operand::Value(Value(1)),
                    ty: IrType::I32,
                },
                // %1 = add %b, %a  (same expression, reversed operands)
                Instruction::BinOp {
                    dest: Value(3),
                    op: IrBinOp::Add,
                    lhs: Operand::Value(Value(1)),
                    rhs: Operand::Value(Value(0)),
                    ty: IrType::I32,
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
            source_spans: Vec::new(),
        };

        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks: vec![block],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 4,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        // Second instruction should be a Copy
        match &module.functions[0].blocks[0].instructions[1] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 3);
                assert_eq!(v.0, 2);
            }
            other => panic!("Expected Copy instruction, got {:?}", other),
        }
    }

    #[test]
    fn test_non_commutative_not_cse() {
        // Test that a - b and b - a are NOT treated as the same
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks: vec![BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::BinOp {
                        dest: Value(2),
                        op: IrBinOp::Sub,
                        lhs: Operand::Value(Value(0)),
                        rhs: Operand::Value(Value(1)),
                        ty: IrType::I32,
                    },
                    Instruction::BinOp {
                        dest: Value(3),
                        op: IrBinOp::Sub,
                        lhs: Operand::Value(Value(1)),
                        rhs: Operand::Value(Value(0)),
                        ty: IrType::I32,
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
                source_spans: Vec::new(),
            }],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 4,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 0);
    }

    #[test]
    fn test_constant_cse() {
        // Two identical constant expressions should be CSE'd
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks: vec![BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::BinOp {
                        dest: Value(0),
                        op: IrBinOp::Add,
                        lhs: Operand::Const(IrConst::I32(3)),
                        rhs: Operand::Const(IrConst::I32(4)),
                        ty: IrType::I32,
                    },
                    Instruction::BinOp {
                        dest: Value(1),
                        op: IrBinOp::Add,
                        lhs: Operand::Const(IrConst::I32(3)),
                        rhs: Operand::Const(IrConst::I32(4)),
                        ty: IrType::I32,
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(1)))),
                source_spans: Vec::new(),
            }],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 2,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);
    }

    #[test]
    fn test_is_commutative() {
        assert!(IrBinOp::Add.is_commutative());
        assert!(IrBinOp::Mul.is_commutative());
        assert!(!IrBinOp::Sub.is_commutative());
        assert!(!IrBinOp::SDiv.is_commutative());
    }

    #[test]
    fn test_cast_cse() {
        // Two identical casts should be CSE'd
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I64,
            blocks: vec![BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::Cast {
                        dest: Value(1),
                        src: Operand::Value(Value(0)),
                        from_ty: IrType::I32,
                        to_ty: IrType::I64,
                    },
                    Instruction::Cast {
                        dest: Value(2),
                        src: Operand::Value(Value(0)),
                        from_ty: IrType::I32,
                        to_ty: IrType::I64,
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
                source_spans: Vec::new(),
            }],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 3,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        match &module.functions[0].blocks[0].instructions[1] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 2);
                assert_eq!(v.0, 1);
            }
            other => panic!("Expected Copy instruction, got {:?}", other),
        }
    }

    #[test]
    fn test_gep_cse() {
        // Two identical GEPs should be CSE'd
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::Ptr,
            blocks: vec![BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::GetElementPtr {
                        dest: Value(2),
                        base: Value(0),
                        offset: Operand::Value(Value(1)),
                        ty: IrType::Ptr,
                    },
                    Instruction::GetElementPtr {
                        dest: Value(3),
                        base: Value(0),
                        offset: Operand::Value(Value(1)),
                        ty: IrType::Ptr,
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
                source_spans: Vec::new(),
            }],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 4,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);
    }

    #[test]
    fn test_cross_block_cse() {
        // Test that expressions in dominating blocks are visible to dominated blocks
        // CFG: block0 -> block1 (block0 dominates block1)
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks: vec![
                BasicBlock {
                    label: BlockId(0),
                    instructions: vec![
                        Instruction::BinOp {
                            dest: Value(2),
                            op: IrBinOp::Add,
                            lhs: Operand::Value(Value(0)),
                            rhs: Operand::Value(Value(1)),
                            ty: IrType::I32,
                        },
                    ],
                    terminator: Terminator::Branch(BlockId(1)),
                    source_spans: Vec::new(),
                },
                BasicBlock {
                    label: BlockId(1),
                    instructions: vec![
                        // Same expression as in block0 - should be CSE'd
                        Instruction::BinOp {
                            dest: Value(3),
                            op: IrBinOp::Add,
                            lhs: Operand::Value(Value(0)),
                            rhs: Operand::Value(Value(1)),
                            ty: IrType::I32,
                        },
                    ],
                    terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
                    source_spans: Vec::new(),
                },
            ],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 4,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        // The expression in block1 should be replaced with a Copy
        match &module.functions[0].blocks[1].instructions[0] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 3);
                assert_eq!(v.0, 2);
            }
            other => panic!("Expected Copy instruction, got {:?}", other),
        }
    }

    #[test]
    fn test_diamond_no_cse_between_branches() {
        // Diamond CFG: block0 -> {block1, block2} -> block3
        // Expressions in block1 and block2 should NOT be CSE'd with each other,
        // since neither dominates the other.
        let func = IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks: vec![
                // block0: entry, branches to block1 or block2
                BasicBlock {
                    label: BlockId(0),
                    instructions: vec![],
                    terminator: Terminator::CondBranch {
                        cond: Operand::Value(Value(0)),
                        true_label: BlockId(1),
                        false_label: BlockId(2),
                    },
                    source_spans: Vec::new(),
                },
                // block1: compute add (only reached via true branch)
                BasicBlock {
                    label: BlockId(1),
                    instructions: vec![
                        Instruction::BinOp {
                            dest: Value(2),
                            op: IrBinOp::Add,
                            lhs: Operand::Value(Value(0)),
                            rhs: Operand::Const(IrConst::I32(1)),
                            ty: IrType::I32,
                        },
                    ],
                    terminator: Terminator::Branch(BlockId(3)),
                    source_spans: Vec::new(),
                },
                // block2: compute same add (only reached via false branch)
                BasicBlock {
                    label: BlockId(2),
                    instructions: vec![
                        Instruction::BinOp {
                            dest: Value(3),
                            op: IrBinOp::Add,
                            lhs: Operand::Value(Value(0)),
                            rhs: Operand::Const(IrConst::I32(1)),
                            ty: IrType::I32,
                        },
                    ],
                    terminator: Terminator::Branch(BlockId(3)),
                    source_spans: Vec::new(),
                },
                // block3: merge
                BasicBlock {
                    label: BlockId(3),
                    instructions: vec![],
                    terminator: Terminator::Return(Some(Operand::Const(IrConst::I32(0)))),
                    source_spans: Vec::new(),
                },
            ],
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id: 4,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        };

        let mut module = IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        };

        let eliminated = module.for_each_function(run_gvn_function);
        // Neither branch dominates the other, so NO CSE should happen
        assert_eq!(eliminated, 0);

        // Both blocks should still have their original BinOp instructions
        assert!(matches!(
            &module.functions[0].blocks[1].instructions[0],
            Instruction::BinOp { op: IrBinOp::Add, .. }
        ));
        assert!(matches!(
            &module.functions[0].blocks[2].instructions[0],
            Instruction::BinOp { op: IrBinOp::Add, .. }
        ));
    }

    /// Helper to create a minimal IrFunction with given blocks.
    fn make_func(blocks: Vec<BasicBlock>, next_value_id: u32) -> IrFunction {
        IrFunction {
            name: "test".to_string(),
            params: vec![],
            return_type: IrType::I32,
            blocks,
            is_variadic: false,
            is_fastcall: false,
            is_naked: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            is_declaration: false,
            next_value_id,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            has_inlined_calls: false,
            param_alloca_values: Vec::new(),
            uses_sret: false,
            global_init_label_blocks: Vec::new(),
            ret_eightbyte_classes: Vec::new(),
            is_gnu_inline_def: false,
            split_copies: Vec::new(),
        }
    }

    fn make_module(func: IrFunction) -> IrModule {
        IrModule {
            functions: vec![func],
            globals: vec![],
            string_literals: vec![],
            wide_string_literals: vec![],
            constructors: vec![],
            destructors: vec![],
            aliases: vec![],
            toplevel_asm: vec![],
            symbol_attrs: vec![],
            char16_string_literals: vec![],
            symver_directives: vec![],
        }
    }

    #[test]
    fn test_global_addr_cse() {
        // Two addresses of the same symbol are one value; another symbol's is not.
        let global = |dest: u32, name: &str| Instruction::GlobalAddr { dest: Value(dest), name: name.to_string() };
        let block = BasicBlock {
            label: BlockId(0),
            instructions: vec![global(0, "g"), global(1, "h"), global(2, "g")],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        };
        let mut module = make_module(make_func(vec![block], 3));
        assert_eq!(module.for_each_function(run_gvn_function), 1);
        let insts = &module.functions[0].blocks[0].instructions;
        assert!(matches!(insts[1], Instruction::GlobalAddr { .. }));
        assert!(matches!(insts[2], Instruction::Copy { dest: Value(2), src: Operand::Value(Value(0)) }), "{:?}", insts[2]);
    }

    #[test]
    fn test_load_cse_same_block() {
        // Two loads from the same pointer in the same block should be CSE'd
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Load {
                    dest: Value(1),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(2),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        }], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        // Second load should be replaced with Copy
        match &module.functions[0].blocks[0].instructions[1] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 2);
                assert_eq!(v.0, 1);
            }
            other => panic!("Expected Copy, got {:?}", other),
        }
    }

    #[test]
    fn test_load_cse_invalidated_by_store() {
        // A store between two loads should prevent CSE
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Load {
                    dest: Value(1),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(2),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        }], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        // The store invalidates the first load's CSE entry, but store-to-load
        // forwarding can replace the second load with the stored value (42).
        assert_eq!(eliminated, 1);
    }

    #[test]
    fn test_load_cse_invalidated_by_call() {
        // A call between two loads should prevent CSE
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Load {
                    dest: Value(1),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Call {
                    func: "side_effect".to_string(),
                    info: CallInfo {
                        dest: Some(Value(2)),
                        args: vec![],
                        arg_types: vec![],
                        return_type: IrType::Void,
                        is_variadic: false,
                        num_fixed_args: 0,
                        struct_arg_sizes: vec![],
                        struct_arg_aligns: vec![],
                        struct_arg_classes: vec![],
                        struct_arg_riscv_float_classes: Vec::new(),
                        is_sret: false,
                        is_fastcall: false,
                        ret_eightbyte_classes: Vec::new(),
                    },
                },
                Instruction::Load {
                    dest: Value(3),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
            source_spans: Vec::new(),
        }], 4);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 0); // No CSE: call may modify memory
    }

    #[test]
    fn test_load_cse_across_dominating_block() {
        // Load in block0 should CSE with load in block1 (block0 dominates block1,
        // single predecessor, no memory clobber)
        let func = make_func(vec![
            BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::Load {
                        dest: Value(1),
                        ptr: Value(0),
                        ty: IrType::I32,
                        seg_override: AddressSpace::Default,
                        volatile: false, alias: AliasClass::ANY
                    },
                ],
                terminator: Terminator::Branch(BlockId(1)),
                source_spans: Vec::new(),
            },
            BasicBlock {
                label: BlockId(1),
                instructions: vec![
                    Instruction::Load {
                        dest: Value(2),
                        ptr: Value(0),
                        ty: IrType::I32,
                        seg_override: AddressSpace::Default,
                        volatile: false, alias: AliasClass::ANY
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
                source_spans: Vec::new(),
            },
        ], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        match &module.functions[0].blocks[1].instructions[0] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 2);
                assert_eq!(v.0, 1);
            }
            other => panic!("Expected Copy, got {:?}", other),
        }
    }

    #[test]
    fn test_load_cse_invalidated_at_merge_point() {
        // Diamond CFG: block0 -> {block1, block2} -> block3
        // block1 stores to memory, so Load CSE should be invalidated at block3
        let func = make_func(vec![
            // block0: entry, load and branch
            BasicBlock {
                label: BlockId(0),
                instructions: vec![
                    Instruction::Load {
                        dest: Value(2),
                        ptr: Value(0),
                        ty: IrType::I32,
                        seg_override: AddressSpace::Default,
                        volatile: false, alias: AliasClass::ANY
                    },
                ],
                terminator: Terminator::CondBranch {
                    cond: Operand::Value(Value(1)),
                    true_label: BlockId(1),
                    false_label: BlockId(2),
                },
                source_spans: Vec::new(),
            },
            // block1: stores to memory
            BasicBlock {
                label: BlockId(1),
                instructions: vec![
                    Instruction::Store {
                        val: Operand::Const(IrConst::I32(42)),
                        ptr: Value(0),
                        ty: IrType::I32,
                        seg_override: AddressSpace::Default,
                        volatile: false, alias: AliasClass::ANY
                    },
                ],
                terminator: Terminator::Branch(BlockId(3)),
                source_spans: Vec::new(),
            },
            // block2: no memory modification
            BasicBlock {
                label: BlockId(2),
                instructions: vec![],
                terminator: Terminator::Branch(BlockId(3)),
                source_spans: Vec::new(),
            },
            // block3: merge point - loads from same pointer
            BasicBlock {
                label: BlockId(3),
                instructions: vec![
                    Instruction::Load {
                        dest: Value(3),
                        ptr: Value(0),
                        ty: IrType::I32,
                        seg_override: AddressSpace::Default,
                        volatile: false, alias: AliasClass::ANY
                    },
                ],
                terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
                source_spans: Vec::new(),
            },
        ], 4);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        // Load in block3 should NOT be CSE'd because block3 is a merge point
        // and block1 (a predecessor) stores to memory.
        assert_eq!(eliminated, 0);

        // block3's load should remain as-is
        assert!(matches!(
            &module.functions[0].blocks[3].instructions[0],
            Instruction::Load { .. }
        ));
    }

    #[test]
    fn test_store_to_load_forwarding_same_block() {
        // store 42 -> *ptr; load *ptr => should be forwarded to Copy(42)
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(1),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(1)))),
            source_spans: Vec::new(),
        }], 2);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        // The load should be replaced with a Copy of the stored constant
        match &module.functions[0].blocks[0].instructions[1] {
            Instruction::Copy { dest, src: Operand::Const(IrConst::I32(42)) } => {
                assert_eq!(dest.0, 1);
            }
            other => panic!("Expected Copy of constant 42, got {:?}", other),
        }
    }

    #[test]
    fn test_store_to_load_forwarding_value() {
        // store %v -> *ptr; load *ptr => should be forwarded to Copy(%v)
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Store {
                    val: Operand::Value(Value(1)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(2),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        }], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        match &module.functions[0].blocks[0].instructions[1] {
            Instruction::Copy { dest, src: Operand::Value(v) } => {
                assert_eq!(dest.0, 2);
                assert_eq!(v.0, 1);
            }
            other => panic!("Expected Copy of Value(1), got {:?}", other),
        }
    }

    #[test]
    fn test_store_to_load_forwarding_invalidated_by_call() {
        // store 42 -> *ptr; call foo(); load *ptr => NOT forwarded (call may modify memory)
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Call {
                    func: "foo".to_string(),
                    info: CallInfo {
                        dest: Some(Value(1)),
                        args: vec![],
                        arg_types: vec![],
                        return_type: IrType::Void,
                        is_variadic: false,
                        num_fixed_args: 0,
                        struct_arg_sizes: vec![],
                        struct_arg_aligns: vec![],
                        struct_arg_classes: vec![],
                        struct_arg_riscv_float_classes: Vec::new(),
                        is_sret: false,
                        is_fastcall: false,
                        ret_eightbyte_classes: Vec::new(),
                    },
                },
                Instruction::Load {
                    dest: Value(2),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        }], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 0); // No forwarding: call invalidates the store
    }

    /// alloca %0; gep %1 = %0+4; store 42 -> *%1; call foo(args); load *%1.
    fn local_store_call_load(call_args: Vec<Operand>) -> IrFunction {
        let n = call_args.len();
        make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Alloca { dest: Value(0), ty: IrType::Ptr, size: 16, align: 0, volatile: false },
                Instruction::GetElementPtr {
                    dest: Value(1), base: Value(0), offset: Operand::Const(IrConst::I64(4)), ty: IrType::I32,
                },
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(1),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Call {
                    func: "foo".to_string(),
                    info: CallInfo {
                        dest: Some(Value(2)),
                        args: call_args,
                        arg_types: vec![IrType::Ptr; n],
                        return_type: IrType::Void,
                        is_variadic: false,
                        num_fixed_args: n,
                        struct_arg_sizes: vec![None; n],
                        struct_arg_aligns: vec![],
                        struct_arg_classes: vec![],
                        struct_arg_riscv_float_classes: Vec::new(),
                        is_sret: false,
                        is_fastcall: false,
                        ret_eightbyte_classes: Vec::new(),
                    },
                },
                Instruction::Load {
                    dest: Value(3),
                    ptr: Value(1),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
            source_spans: Vec::new(),
        }], 4)
    }

    #[test]
    fn test_store_to_local_forwarded_across_call() {
        // The local's address never escapes, so the call cannot modify it.
        let mut module = make_module(local_store_call_load(vec![]));
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);
        let last = &module.functions[0].blocks[0].instructions[4];
        assert!(matches!(last, Instruction::Copy { src: Operand::Const(IrConst::I32(42)), .. }));
    }

    #[test]
    fn test_store_to_escaped_local_not_forwarded_across_call() {
        // Passing the local's base address to the call lets it write the local.
        let mut module = make_module(local_store_call_load(vec![Operand::Value(Value(0))]));
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 0);
    }

    #[test]
    fn test_store_to_load_forwarding_different_store_invalidates() {
        // store 42 -> *ptr_a; store 99 -> *ptr_b; load *ptr_a
        // => NOT forwarded because the second store (to any address) invalidates all
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(99)),
                    ptr: Value(1),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(2),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(2)))),
            source_spans: Vec::new(),
        }], 3);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        // The second store bumps the generation, invalidating the first store's entry.
        // However, the second store also records *ptr_b -> 99 at the new generation.
        // The load from *ptr_a should NOT be forwarded because *ptr_a != *ptr_b.
        assert_eq!(eliminated, 0);
    }

    #[test]
    fn test_store_to_load_forwarding_same_ptr_overwrite() {
        // store 42 -> *ptr; store 99 -> *ptr; load *ptr => forwarded to 99
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(42)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Store {
                    val: Operand::Const(IrConst::I32(99)),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
                Instruction::Load {
                    dest: Value(1),
                    ptr: Value(0),
                    ty: IrType::I32,
                    seg_override: AddressSpace::Default,
                    volatile: false, alias: AliasClass::ANY
                },
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(1)))),
            source_spans: Vec::new(),
        }], 2);

        let mut module = make_module(func);
        let eliminated = module.for_each_function(run_gvn_function);
        assert_eq!(eliminated, 1);

        // Should forward the SECOND store's value (99), not the first (42)
        match &module.functions[0].blocks[0].instructions[2] {
            Instruction::Copy { dest, src: Operand::Const(IrConst::I32(99)) } => {
                assert_eq!(dest.0, 1);
            }
            other => panic!("Expected Copy of constant 99, got {:?}", other),
        }
    }

    fn field(dest: u32, base: u32, off: i64, ty: IrType) -> Instruction {
        Instruction::GetElementPtr { dest: Value(dest), base: Value(base), offset: Operand::Const(IrConst::I64(off)), ty }
    }

    fn load(dest: u32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Load { dest: Value(dest), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    fn store(val: i32, ptr: u32, ty: IrType) -> Instruction {
        Instruction::Store { val: Operand::Const(IrConst::I32(val)), ptr: Value(ptr), ty, seg_override: AddressSpace::Default, volatile: false, alias: AliasClass::ANY }
    }

    #[test]
    fn test_load_kept_across_store_to_another_field() {
        // s->valid (offset 8) read, s->buf (offset 4, 2 bytes) written, then
        // s->valid read again through a differently typed GEP: the same
        // value. A 4-byte store at offset 6 overlaps it, and a store through
        // another pointer may.
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                field(1, 0, 8, IrType::I32),
                load(2, 1, IrType::I32),
                field(3, 0, 4, IrType::U16),
                store(7, 3, IrType::U16),
                field(4, 0, 8, IrType::Ptr),
                load(5, 4, IrType::I32),
                field(6, 0, 6, IrType::I32),
                store(9, 6, IrType::I32),
                load(7, 1, IrType::I32),
                store(3, 10, IrType::U8),
                load(8, 4, IrType::I32),
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(8)))),
            source_spans: Vec::new(),
        }], 11);

        let mut module = make_module(func);
        assert_eq!(module.for_each_function(run_gvn_function), 1);
        let insts = &module.functions[0].blocks[0].instructions;
        assert!(matches!(insts[5], Instruction::Copy { dest: Value(5), src: Operand::Value(Value(2)) }), "{:?}", insts[5]);
        assert!(matches!(insts[8], Instruction::Load { .. }), "{:?}", insts[8]);
        assert!(matches!(insts[10], Instruction::Load { .. }), "{:?}", insts[10]);
    }

    #[test]
    fn test_store_forwarded_across_store_to_another_field() {
        // s->a = 5 (offset 0); s->b = 7 (offset 4); s->a read: 5.
        let func = make_func(vec![BasicBlock {
            label: BlockId(0),
            instructions: vec![
                store(5, 0, IrType::I32),
                field(1, 0, 4, IrType::I32),
                store(7, 1, IrType::I32),
                field(2, 0, 0, IrType::I8),
                load(3, 2, IrType::I32),
            ],
            terminator: Terminator::Return(Some(Operand::Value(Value(3)))),
            source_spans: Vec::new(),
        }], 4);

        let mut module = make_module(func);
        module.for_each_function(run_gvn_function);
        let insts = &module.functions[0].blocks[0].instructions;
        assert!(matches!(insts[4], Instruction::Copy { src: Operand::Const(IrConst::I32(5)), .. }), "{:?}", insts[4]);
    }

    #[test]
    fn test_load_kept_in_second_arm_after_first_arm_clobbers() {
        // s->valid read, then if/else: the first arm stores through another
        // pointer and then to s->buf (dropping s->valid from the keys at s); the
        // second arm stores only to s->buf and reads s->valid again: the same value.
        let block = |label: u32, instructions: Vec<Instruction>, terminator| BasicBlock {
            label: BlockId(label), instructions, terminator, source_spans: Vec::new(),
        };
        let func = make_func(vec![
            block(0, vec![field(1, 0, 8, IrType::I32), load(2, 1, IrType::I32), field(3, 0, 4, IrType::U16)],
                Terminator::CondBranch { cond: Operand::Value(Value(9)), true_label: BlockId(1), false_label: BlockId(2) }),
            block(1, vec![store(1, 10, IrType::U8), store(2, 3, IrType::U16)],
                Terminator::Return(Some(Operand::Value(Value(2))))),
            block(2, vec![store(3, 3, IrType::U16), load(5, 1, IrType::I32)],
                Terminator::Return(Some(Operand::Value(Value(5))))),
        ], 11);

        let mut module = make_module(func);
        assert_eq!(module.for_each_function(run_gvn_function), 1);
        let blocks = &module.functions[0].blocks;
        assert!(matches!(blocks[2].instructions[1], Instruction::Copy { dest: Value(5), src: Operand::Value(Value(2)) }),
            "{:?}", blocks[2].instructions[1]);
    }
}

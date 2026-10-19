//! ArmCodegen: global address operations.

use crate::ir::reexports::Value;
use super::emit::ArmCodegen;

impl ArmCodegen {
    pub(super) fn emit_global_addr_impl(&mut self, dest: &Value, name: &str) {
        if self.state.needs_got_aarch64(name) {
            self.state.emit_fmt(format_args!("    adrp x0, :got:{}", name));
            self.state.emit_fmt(format_args!("    ldr x0, [x0, :got_lo12:{}]", name));
        } else {
            self.state.emit_fmt(format_args!("    adrp x0, {}", name));
            self.state.emit_fmt(format_args!("    add x0, x0, :lo12:{}", name));
        }
        self.store_x0_to(dest);
    }

    pub(super) fn emit_label_addr_impl(&mut self, dest: &Value, label: &str) {
        self.state.emit_fmt(format_args!("    adrp x0, {}", label));
        self.state.emit_fmt(format_args!("    add x0, x0, :lo12:{}", label));
        self.store_x0_to(dest);
    }

    pub(super) fn emit_tls_global_addr_impl(&mut self, dest: &Value, name: &str) {
        if crate::common::types::target_is_darwin() {
            // Mach-O TLV: call the descriptor's thunk with x0 = &descriptor; it
            // returns the variable's address in x0 and clobbers only x16/x17/lr.
            // (`:tlvp:` is rewritten to `@TLVPPAGE`/`@TLVPPAGEOFF` by darwin.rs.)
            self.state.emit_fmt(format_args!("    adrp x0, :tlvp:{}", name));
            self.state.emit_fmt(format_args!("    ldr x0, [x0, :tlvp_lo12:{}]", name));
            self.state.emit("    ldr x16, [x0]");
            self.state.emit("    blr x16");
            self.state.reg_cache.invalidate_all();
            self.store_x0_to(dest);
            return;
        }
        self.state.emit("    mrs x0, tpidr_el0");
        self.state.emit_fmt(format_args!("    add x0, x0, :tprel_hi12:{}", name));
        self.state.emit_fmt(format_args!("    add x0, x0, :tprel_lo12_nc:{}", name));
        self.store_x0_to(dest);
    }
}

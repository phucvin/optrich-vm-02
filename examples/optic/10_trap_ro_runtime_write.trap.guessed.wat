;; Guessed WAT for examples/optic/10_trap_ro_runtime_write.opc (expected runtime trap)
(module
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))
  (string $l1 "step1.frozen_meta")
  (string $s_frozen "frozen")

  (func (export "main") (result i32)
    (local $box rw_handle)
    (local $ro ro_handle)
    (local $rw rw_handle)
    (local.set $ro (mem.const_str $s_frozen))
    (call $trace_meta (mem.const_str $l1) (local.get $ro))
    ;; Store read-only handle in a ref slot and attempt mem.read_ref_rw -> traps at runtime!
    (local.set $box (mem.alloc (i32.const 0) (i32.const 1)))
    (mem.write_ref (local.get $box) (i32.const 0) (local.get $ro))
    (local.set $rw (mem.read_ref_rw (local.get $box) (i32.const 0)))
    (mem.write_u8 (local.get $rw) (i32.const 0) (i32.const 88))
    (i32.const 0))
)

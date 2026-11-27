;; Guessed WAT for examples/optic/10_trap_ro_runtime_write.opc (expected runtime trap)
(module
  (import "host" "trace_meta" (func $trace_meta (param ro_handle handle)))
  (string $l1 "step1.frozen_meta")
  (string $s_frozen "frozen")

  (func (export "main") (result i32)
    (local $h handle)
    (local.set $h (mem.const_str $s_frozen))
    (call $trace_meta (mem.const_str $l1) (local.get $h))
    ;; Dynamic handle write on read-only block -> traps at runtime!
    (mem.write_u8 (local.get $h) (i32.const 0) (i32.const 88))
    (i32.const 0))
)

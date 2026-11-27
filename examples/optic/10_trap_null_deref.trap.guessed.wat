;; Guessed WAT for examples/optic/10_trap_null_deref.opc (expected runtime trap)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (string $l1 "step1.before_null_deref")

  (func (export "main") (result i32)
    (local $b ro_handle)
    (local.set $b (handle.null))
    (call $trace_i32 (mem.const_str $l1) (i32.const 1))
    (mem.read_i32 (local.get $b) (i32.const 0)))
)

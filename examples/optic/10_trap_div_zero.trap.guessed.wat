;; Guessed WAT for examples/optic/10_trap_div_zero.opc (expected runtime trap)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (string $l1 "step1.before_div_zero")

  (func (export "main") (result i32)
    (local $a i32)
    (local $b i32)
    (local.set $a (i32.const 42))
    (local.set $b (i32.const 0))
    (call $trace_i32 (mem.const_str $l1) (local.get $a))
    (i32.div_s (local.get $a) (local.get $b)))
)

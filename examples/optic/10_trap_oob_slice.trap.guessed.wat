;; Guessed WAT for examples/optic/10_trap_oob_slice.opc (expected runtime trap)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (string $l1 "step1.valid_elem")

  (func (export "main") (result i32)
    (local $arr rw_handle)
    (local.set $arr (mem.alloc (i32.const 12) (i32.const 0)))
    (mem.write_i32 (local.get $arr) (i32.const 0) (i32.const 11))
    (mem.write_i32 (local.get $arr) (i32.const 4) (i32.const 22))
    (mem.write_i32 (local.get $arr) (i32.const 8) (i32.const 33))
    (call $trace_i32 (mem.const_str $l1) (mem.read_i32 (local.get $arr) (i32.const 8)))
    ;; OOB read at byte offset 12 (len = 12) -> traps!
    (mem.read_i32 (local.get $arr) (i32.const 12)))
)

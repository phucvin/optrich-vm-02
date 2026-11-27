;; Guessed WAT for examples/optic/09b_cross_module_callbacks/main.opc
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (import "algo" "map_sum"   (func $map_sum   (param ro_handle handle) (result i32)))

  (global $affine_vt (mut handle) (handle.null))
  (string $l1 "step1.cross_mod_map_sum")

  (func $affine_body (param $env handle) (param $x i32) (result i32)
    (i32.add
      (i32.mul (local.get $x) (mem.read_i32 (local.get $env) (i32.const 0)))
      (mem.read_i32 (local.get $env) (i32.const 4))))

  (func (export "main") (result i32)
    (local $nums rw_handle)
    (local $vt rw_handle)
    (local $cl rw_handle)
    (local $res i32)

    (local.set $nums (mem.alloc (i32.const 12) (i32.const 0)))
    (mem.write_i32 (local.get $nums) (i32.const 0) (i32.const 2))
    (mem.write_i32 (local.get $nums) (i32.const 4) (i32.const 4))
    (mem.write_i32 (local.get $nums) (i32.const 8) (i32.const 6))

    (if (mem.is_null (global.get $affine_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 1)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $affine_body))
        (global.set $affine_vt (mem.readonly (local.get $vt)))))

    (local.set $cl (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $cl) (i32.const 0) (i32.const 3))
    (mem.write_i32 (local.get $cl) (i32.const 4) (i32.const 1))
    (mem.write_ref (local.get $cl) (i32.const 0) (global.get $affine_vt))

    (local.set $res (call $map_sum (mem.readonly (local.get $nums)) (mem.readonly (local.get $cl))))
    (call $trace_i32 (mem.const_str $l1) (local.get $res))
    (local.get $res))
)

;; Guessed WAT for examples/optic/09b_cross_module_callbacks/algo.opc
(module
  (type $cl_i32_i32 (func (param handle i32) (result i32)))

  (func (export "map_sum") (param $xs ro_handle) (param $f handle) (result i32)
    (local $acc i32)
    (local $i i32)
    (local $end i32)
    (local $fn funcref)
    (local.set $end (i32.shr_u (mem.byte_len (local.get $xs)) (i32.const 2)))
    (local.set $fn (mem.read_func (mem.read_ref (local.get $f) (i32.const 0)) (i32.const 0)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $acc
          (i32.add (local.get $acc)
            (call_ref (type $cl_i32_i32)
              (local.get $f)
              (mem.read_i32 (local.get $xs) (i32.shl (local.get $i) (i32.const 2)))
              (local.get $fn))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $acc))
)

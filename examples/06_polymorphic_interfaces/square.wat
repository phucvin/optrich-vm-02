;; Example 06 (Module 2/3) — `square` (Pure Go/Rust-style Trait Object!)
;; NO imports, NO tables, NO hardcoded slot IDs, NO startup registration!
;;
;; Pure data layout:   bytes[0..4) = side
;; Shape vtable:       [func 0 = $square_area, func 1 = $square_perimeter]
;; Shape trait object: ro_handle { refs[0] = data, refs[1] = vtable }
(module
  (global $vtable (mut handle) (handle.null))

  (func $square_area (param $self handle) (result i32)
    (local $s i32)
    (local.set $s (mem.read_i32 (local.get $self) (i32.const 0)))
    (i32.mul (local.get $s) (local.get $s)))

  (func $square_perimeter (param $self handle) (result i32)
    (i32.mul (i32.const 4) (mem.read_i32 (local.get $self) (i32.const 0))))

  (func $get_vtable (result handle)
    (local $vt rw_handle)
    (if (mem.is_null (global.get $vtable))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 2)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $square_area))
        (mem.write_func (local.get $vt) (i32.const 1) (ref.func $square_perimeter))
        (global.set $vtable (mem.readonly (local.get $vt)))))
    (global.get $vtable))

  (func (export "new_square") (param $side i32) (result ro_handle)
    (local $data rw_handle)
    (local $trait rw_handle)
    (local.set $data (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $side))

    (local.set $trait (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $trait) (i32.const 0) (local.get $data))
    (mem.write_ref (local.get $trait) (i32.const 1) (call $get_vtable))
    (mem.readonly (local.get $trait)))
)

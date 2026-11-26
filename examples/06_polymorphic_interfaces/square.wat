;; Example 06 (Module 4/5) — `square` Implementer (Implements Shape)
;; Pure data layout: bytes[0..4) = side.
;; Dynamically registers its own slot in `shape_iface` — never collides with `rect`!
(module
  (import "shape_iface" "register" (func $reg_shape (param funcref) (result i32)))
  (import "shape_iface" "wrap"     (func $wrap_shape (param handle i32) (result ro_handle)))

  (global $shape_slot (mut i32) (i32.const -1))

  (func $square_area (param $self handle) (result i32)
    (local $s i32)
    (local.set $s (mem.read_i32 (local.get $self) (i32.const 0)))
    (i32.mul (local.get $s) (local.get $s)))

  (func $init
    (global.set $shape_slot (call $reg_shape (ref.func $square_area))))
  (start $init)

  (func (export "new_square") (param $side i32) (result handle)
    (local $o rw_handle)
    (local.set $o (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $o) (i32.const 0) (local.get $side))
    (local.get $o))

  (func (export "as_shape") (param $s handle) (result ro_handle)
    (call $wrap_shape (local.get $s) (global.get $shape_slot)))
)

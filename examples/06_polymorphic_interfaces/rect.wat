;; Example 06 (Module 3/5) — `rect` Implementer (Implements BOTH Shape & Perimeter!)
;; Pure data layout (no vtable tag embedded!):
;;   bytes[0..4) = width
;;   bytes[4..8) = height
(module
  (import "shape_iface"     "register" (func $reg_shape (param funcref) (result i32)))
  (import "shape_iface"     "wrap"     (func $wrap_shape (param handle i32) (result ro_handle)))
  (import "perimeter_iface" "register" (func $reg_perim (param funcref) (result i32)))
  (import "perimeter_iface" "wrap"     (func $wrap_perim (param handle i32) (result ro_handle)))

  ;; Dynamically assigned slot IDs (populated automatically on module instantiation)
  (global $shape_slot (mut i32) (i32.const -1))
  (global $perim_slot (mut i32) (i32.const -1))

  (func $rect_area (param $self handle) (result i32)
    (i32.mul
      (mem.read_i32 (local.get $self) (i32.const 0))
      (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $rect_perimeter (param $self handle) (result i32)
    (i32.mul (i32.const 2)
      (i32.add
        (mem.read_i32 (local.get $self) (i32.const 0))
        (mem.read_i32 (local.get $self) (i32.const 4)))))

  ;; Automatic startup registration via `(start $init)`
  (func $init
    (global.set $shape_slot (call $reg_shape (ref.func $rect_area)))
    (global.set $perim_slot (call $reg_perim (ref.func $rect_perimeter))))
  (start $init)

  (func (export "new_rect") (param $w i32) (param $h i32) (result handle)
    (local $o rw_handle)
    (local.set $o (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $o) (i32.const 0) (local.get $w))
    (mem.write_i32 (local.get $o) (i32.const 4) (local.get $h))
    (local.get $o))

  (func (export "as_shape") (param $r handle) (result ro_handle)
    (call $wrap_shape (local.get $r) (global.get $shape_slot)))

  (func (export "as_perimeter") (param $r handle) (result ro_handle)
    (call $wrap_perim (local.get $r) (global.get $perim_slot)))
)

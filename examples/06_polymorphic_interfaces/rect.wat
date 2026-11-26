;; Example 06 (Module 2/4) — Rectangle Implementer (`rect`)
;; Registers `rect_area` into `shape_iface.vtable` at slot 0.
;; Layout: bytes[0..4)=slot(0), bytes[4..8)=width, bytes[8..12)=height
(module
  (import "shape_iface" "vtable" (table $vtable 8 funcref))

  (func $rect_area (param $self handle) (result i32)
    (i32.mul
      (mem.read_i32 (local.get $self) (i32.const 4))
      (mem.read_i32 (local.get $self) (i32.const 8))))

  (elem (i32.const 0) $rect_area)

  (func (export "new_rect") (param $w i32) (param $h i32) (result handle)
    (local $o rw_handle)
    (local.set $o (mem.alloc (i32.const 12) (i32.const 0)))
    (mem.write_i32 (local.get $o) (i32.const 0) (i32.const 0)) ;; vtable slot 0
    (mem.write_i32 (local.get $o) (i32.const 4) (local.get $w))
    (mem.write_i32 (local.get $o) (i32.const 8) (local.get $h))
    (local.get $o))
)

;; Example 06 (Module 3/4) — Square Implementer (`square`)
;; Registers `square_area` into `shape_iface.vtable` at slot 1.
;; Layout: bytes[0..4)=slot(1), bytes[4..8)=side
(module
  (import "shape_iface" "vtable" (table $vtable 8 funcref))

  (func $square_area (param $self handle) (result i32)
    (local $s i32)
    (local.set $s (mem.read_i32 (local.get $self) (i32.const 4)))
    (i32.mul (local.get $s) (local.get $s)))

  (elem (i32.const 1) $square_area)

  (func (export "new_square") (param $side i32) (result handle)
    (local $o rw_handle)
    (local.set $o (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $o) (i32.const 0) (i32.const 1)) ;; vtable slot 1
    (mem.write_i32 (local.get $o) (i32.const 4) (local.get $side))
    (local.get $o))
)

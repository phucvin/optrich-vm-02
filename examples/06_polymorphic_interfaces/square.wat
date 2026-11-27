;; Example 06 (Module 3/4) — `square` Implementer
;; Pure data layout: bytes[0..4) = side.
;; Zero magic vtable slot indices — delegates vtable/trait construction to `shape`.
(module
  (import "shape" "make_vtable" (func $make_vtable (param funcref funcref) (result ro_handle)))
  (import "shape" "wrap"        (func $wrap        (param handle handle) (result ro_handle)))

  (global $vtable (mut handle) (handle.null))

  (func $square_area (param $self handle) (result i32)
    (local $s i32)
    (local.set $s (mem.read_i32 (local.get $self) (i32.const 0)))
    (i32.mul (local.get $s) (local.get $s)))

  (func $square_scale (param $self handle) (param $factor i32)
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $factor))))

  (func $get_vtable (result handle)
    (if (mem.is_null (global.get $vtable))
      (then
        (global.set $vtable
          (call $make_vtable (ref.func $square_area) (ref.func $square_scale)))))
    (global.get $vtable))

  (func (export "new_square") (param $side i32) (result ro_handle)
    (local $data rw_handle)
    (local.set $data (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $side))
    (call $wrap (local.get $data) (call $get_vtable)))
)

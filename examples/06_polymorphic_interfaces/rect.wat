;; Example 06 (Module 2/4) — `rect` Implementer
;; Pure data layout: bytes[0..4) = width, bytes[4..8) = height.
;; Zero magic vtable slot indices — delegates vtable/trait construction to `shape`.
(module
  (import "shape" "make_vtable" (func $make_vtable (param funcref funcref) (result ro_handle)))
  (import "shape" "wrap"        (func $wrap        (param rw_handle ro_handle) (result ro_handle)))

  (global $vtable (mut ro_handle) (handle.null))

  (func $rect_area (param $self ro_handle) (result i32)
    (i32.mul
      (mem.read_i32 (local.get $self) (i32.const 0))
      (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $rect_scale (param $self rw_handle) (param $factor i32)
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $factor)))
    (mem.write_i32 (local.get $self) (i32.const 4)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 4)) (local.get $factor))))

  (func $get_vtable (result ro_handle)
    (if (mem.is_null (global.get $vtable))
      (then
        (global.set $vtable
          (call $make_vtable (ref.func $rect_area) (ref.func $rect_scale)))))
    (global.get $vtable))

  (func (export "new_rect") (param $w i32) (param $h i32) (result ro_handle)
    (local $data rw_handle)
    (local.set $data (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $w))
    (mem.write_i32 (local.get $data) (i32.const 4) (local.get $h))
    (call $wrap (local.get $data) (call $get_vtable)))
)

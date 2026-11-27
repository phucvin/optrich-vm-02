;; Example 06 (Module 1/3) — `rect` (Pure Go/Rust-style Trait Object!)
;; NO imports, NO tables, NO hardcoded slot IDs, NO startup registration!
;;
;; Pure data layout:   bytes[0..4) = width, bytes[4..8) = height
;; Shape vtable:       [func 0 = $rect_area, func 1 = $rect_perimeter]
;; Shape trait object: ro_handle { refs[0] = data, refs[1] = vtable }
(module
  ;; Cached read-only vtable (allocated lazily once and shared by all rects)
  (global $vtable (mut handle) (handle.null))

  (func $rect_area (param $self handle) (result i32)
    (i32.mul
      (mem.read_i32 (local.get $self) (i32.const 0))
      (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $rect_perimeter (param $self handle) (result i32)
    (i32.mul (i32.const 2)
      (i32.add
        (mem.read_i32 (local.get $self) (i32.const 0))
        (mem.read_i32 (local.get $self) (i32.const 4)))))

  (func $get_vtable (result handle)
    (local $vt rw_handle)
    (if (mem.is_null (global.get $vtable))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 2)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $rect_area))
        (mem.write_func (local.get $vt) (i32.const 1) (ref.func $rect_perimeter))
        (global.set $vtable (mem.readonly (local.get $vt)))))
    (global.get $vtable))

  (func (export "new_rect") (param $w i32) (param $h i32) (result ro_handle)
    (local $data rw_handle)
    (local $trait rw_handle)
    (local.set $data (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $w))
    (mem.write_i32 (local.get $data) (i32.const 4) (local.get $h))

    ;; Trait object = read-only 2-ref wrapper: [refs[0] = data, refs[1] = vtable]
    (local.set $trait (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $trait) (i32.const 0) (local.get $data))
    (mem.write_ref (local.get $trait) (i32.const 1) (call $get_vtable))
    (mem.readonly (local.get $trait)))
)

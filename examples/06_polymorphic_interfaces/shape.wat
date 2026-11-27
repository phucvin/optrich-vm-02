;; Example 06 (Module 1/4) — Stateless `shape` Interface Module
;; Single source of truth for the `Shape` trait layout — ZERO tables, ZERO global state!
;;
;; Methods (with DIFFERENT signatures in the same vtable!):
;;   slot 0: area(self: handle) -> i32
;;   slot 1: scale(self: handle, factor: i32) -> ()
;;
;; Trait object layout (ro_handle):
;;   refs[0] = underlying data handle
;;   refs[1] = frozen vtable handle
(module
  (type $area_sig  (func (param ro_handle) (result i32)))
  (type $scale_sig (func (param rw_handle i32)))

  ;; Build a frozen Shape vtable (only shape.wat knows slot 0 = area, slot 1 = scale)
  (func (export "make_vtable") (param $area_fn funcref) (param $scale_fn funcref) (result ro_handle)
    (local $vt rw_handle)
    (local.set $vt (mem.alloc_vtable (i32.const 2)))
    (mem.write_func (local.get $vt) (i32.const 0) (local.get $area_fn))
    (mem.write_func (local.get $vt) (i32.const 1) (local.get $scale_fn))
    (mem.readonly (local.get $vt)))

  ;; Wrap (data, vtable) into a read-only Shape trait object
  (func (export "wrap") (param $data rw_handle) (param $vt ro_handle) (result ro_handle)
    (local $trait rw_handle)
    (local.set $trait (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $trait) (i32.const 0) (local.get $data))
    (mem.write_ref (local.get $trait) (i32.const 1) (local.get $vt))
    (mem.readonly (local.get $trait)))

  ;; Polymorphic dispatch: shape.area(s) -> i32
  (func (export "area") (param $s ro_handle) (result i32)
    (call_ref (type $area_sig)
      (mem.read_ref_ro (local.get $s) (i32.const 0))
      (mem.read_func (mem.read_ref_ro (local.get $s) (i32.const 1)) (i32.const 0))))

  ;; Polymorphic dispatch: shape.scale(s, factor) -> ()
  (func (export "scale") (param $s ro_handle) (param $factor i32)
    (call_ref (type $scale_sig)
      (mem.read_ref_rw (local.get $s) (i32.const 0))
      (local.get $factor)
      (mem.read_func (mem.read_ref_ro (local.get $s) (i32.const 1)) (i32.const 1))))
)

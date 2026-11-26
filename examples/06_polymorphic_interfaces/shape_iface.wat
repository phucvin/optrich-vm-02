;; Example 06 (Module 1/4) — Polymorphic Shape Interface (`shape_iface`)
;; Owns the shared `vtable` table and dispatches `area(obj)` across module boundaries
;; using `call_indirect`. Every `FuncRef` in `vtable` carries its owning ModuleInstance.
;;
;; Interface handle convention (see docs/shared-conventions.md):
;;   bytes[0..4) : i32 vtable slot index
;;   bytes[4..)  : implementer-private state
(module
  (type $area_sig (func (param handle) (result i32)))
  (table $vtable (export "vtable") 8 funcref)

  (func (export "area") (param $shape handle) (result i32)
    (local $slot i32)
    (local.set $slot (mem.read_i32 (local.get $shape) (i32.const 0)))
    (call_indirect (type $area_sig) (local.get $shape) (local.get $slot)))
)

;; Example 06 (Module 1/5) — `shape_iface` Interface
;; Owns a PRIVATE vtable (no external module can overwrite slots!).
;; Implementers register dynamically via `register(ref.func $fn) -> slot_id`
;; using `table.grow` — zero hardcoded slot indices, zero collisions!
;;
;; Trait-object wrapper layout (Go `(itab, data)` / Rust `dyn Trait` style):
;;   ro_handle: bytes[0..4) = vtable slot id, refs[0] = underlying data handle
(module
  (type $area_sig (func (param handle) (result i32)))
  (table $vtable 0 funcref) ;; private, grows dynamically from 0

  ;; Dynamically allocate a new vtable slot for `fn` and return its slot ID
  (func (export "register") (param $fn funcref) (result i32)
    (table.grow $vtable (local.get $fn) (i32.const 1)))

  ;; Wrap any object `data` + `slot` into a read-only trait-object handle
  (func (export "wrap") (param $data handle) (param $slot i32) (result ro_handle)
    (local $obj rw_handle)
    (local.set $obj (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $obj) (i32.const 0) (local.get $slot))
    (mem.write_ref (local.get $obj) (i32.const 0) (local.get $data))
    (mem.readonly (local.get $obj)))

  ;; Polymorphic dispatch: unpack (slot, data) from trait object and call_indirect
  (func (export "area") (param $trait_obj ro_handle) (result i32)
    (local $slot i32)
    (local $data handle)
    (local.set $slot (mem.read_i32 (local.get $trait_obj) (i32.const 0)))
    (local.set $data (mem.read_ref (local.get $trait_obj) (i32.const 0)))
    (call_indirect $vtable (type $area_sig) (local.get $data) (local.get $slot)))
)

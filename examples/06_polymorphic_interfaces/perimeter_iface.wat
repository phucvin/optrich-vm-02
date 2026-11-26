;; Example 06 (Module 2/5) — `perimeter_iface` (A Second Independent Interface!)
;; Completely independent of `shape_iface`: owns its own private vtable.
;; Any type (such as `rect`) can implement BOTH `shape_iface` and `perimeter_iface`
;; without reserving bytes in the object or coordinating slot IDs.
(module
  (type $perim_sig (func (param handle) (result i32)))
  (table $vtable 0 funcref)

  (func (export "register") (param $fn funcref) (result i32)
    (table.grow $vtable (local.get $fn) (i32.const 1)))

  (func (export "wrap") (param $data handle) (param $slot i32) (result ro_handle)
    (local $obj rw_handle)
    (local.set $obj (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $obj) (i32.const 0) (local.get $slot))
    (mem.write_ref (local.get $obj) (i32.const 0) (local.get $data))
    (mem.readonly (local.get $obj)))

  (func (export "perimeter") (param $trait_obj ro_handle) (result i32)
    (local $slot i32)
    (local $data handle)
    (local.set $slot (mem.read_i32 (local.get $trait_obj) (i32.const 0)))
    (local.set $data (mem.read_ref (local.get $trait_obj) (i32.const 0)))
    (call_indirect $vtable (type $perim_sig) (local.get $data) (local.get $slot)))
)

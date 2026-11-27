# Polymorphic Cross-Module Interfaces (Go/Rust-Style Trait Objects)

This document explains how Optrich VM 02 implements clean, zero-boilerplate polymorphism across modules using **first-class `funcref` values**, **read-only vtable blocks**, and **`call_ref`**—the same model as Go interfaces (`(data*, itab*)`) and Rust trait objects (`dyn Trait`).

---

## Why Static Wasm Tables (`call_indirect` + `elem`) Are Awkward for Interfaces

Traditional WebAssembly MVP polymorphism relies on a shared `funcref` table populated by `(elem (i32.const N) $fn)`:

1. **Hardcoded slot collisions:** Independently authored modules have no way to coordinate slot indices without a central registry or dynamic `table.grow` + `(start)` boilerplate.
2. **Implicit table index `0`:** Multi-interface modules must juggle explicit table indices.
3. **Object layout conflicts:** Embedding an integer vtable tag inside `bytes[0..4)` of the object breaks as soon as one type implements two independent interfaces.
4. **Forgeability:** An integer slot index inside a mutable buffer can be tampered with or forged.

---

## Optrich VM 02's Model: Trait Objects + `call_ref`

Instead of tables and integer slot IDs, Optrich VM 02 provides:
* **`funcref` & `ref.func $fn`** — unforgeable function references carrying `{ ModuleInstance*, localFuncIdx }`.
* **`mem.alloc_vtable(num_funcs)`**, **`mem.write_func`**, **`mem.read_func`**, **`mem.func_len`** — split-block vtables that hold `funcref`s directly and can be frozen with `mem.readonly`.
* **`call_ref (type $sig)`** — pops a `funcref` directly off the stack, verifies its signature, switches `ModuleInstance*`, and pushes a `CallFrame` directly onto the shared call stack with zero copying.

```text
  Trait Object (ro_handle: 0 bytes, 2 refs)
  ┌──────────────────┬──────────────────┐
  │ refs[0]: data ───┼──▶ [ width | height ]  (pure data — no tags!)
  │ refs[1]: vtable ─┼──┐
  └──────────────────┘  │
                        ▼
             Read-Only Vtable (ro_handle: 2 funcrefs with different signatures)
             ┌───┬─────────────────────────────────────────────────────────┐
             │ 0 │ FuncRef { inst: &rect, fn: rect_area  (handle)->i32   } │
             │ 1 │ FuncRef { inst: &rect, fn: rect_scale (handle,i32)->() }│
             └───┴─────────────────────────────────────────────────────────┘
```

### Key Properties

1. **Stateless interface module as single source of truth:**
   `shape.wat` owns **zero tables and zero global state**—it simply encapsulates the vtable layout via `make_vtable(area_fn, scale_fn)`, `wrap(data, vtable)`, `area(s)`, and `scale(s, factor)`. Neither implementers (`rect.wat`, `square.wat`) nor callers (`main.wat`) ever touch raw vtable slot indices.
2. **Multiple method signatures in one vtable:**
   Because `call_ref (type $sig)` checks the expected signature at each call site, a single vtable can hold methods with completely different parameter/return types (e.g., `area(handle) -> i32` and `scale(handle, i32) -> ()`).
3. **Pure data objects & unlimited interfaces:**
   The underlying `data` block stores only private fields (`[width, height]`). The exact same `data` handle can be wrapped with different vtables (`Shape`, `Drawable`, `Serializable`) with zero layout conflicts.
4. **100% unforgeable capabilities & automatic GC:**
   Both `vtable` and `trait_obj` are `ro_handle`s (preventing callers from tampering with the vtable), `funcref`s cannot be forged from integers, and the precise GC traces both `refs[0]` (`data`) and `refs[1]` (`vtable`) automatically.

### Direct Dispatch Inside `shape.wat`

```wat
(type $area_sig  (func (param handle) (result i32)))
(type $scale_sig (func (param handle i32)))

(func (export "area") (param $s handle) (result i32)
  (call_ref (type $area_sig)
    (mem.read_ref  (local.get $s) (i32.const 0))
    (mem.read_func (mem.read_ref (local.get $s) (i32.const 1)) (i32.const 0))))

(func (export "scale") (param $s handle) (param $factor i32)
  (call_ref (type $scale_sig)
    (mem.read_ref  (local.get $s) (i32.const 0))
    (local.get $factor)
    (mem.read_func (mem.read_ref (local.get $s) (i32.const 1)) (i32.const 1))))
```

See [`examples/06_polymorphic_interfaces/`](../examples/06_polymorphic_interfaces/) for the complete runnable 4-module example (`shape.wat`, `rect.wat`, `square.wat`, `main.wat`).

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
             Read-Only Vtable (ro_handle: 2 funcrefs, shared by all rects)
             ┌───┬─────────────────────────────────────────────┐
             │ 0 │ FuncRef { inst: &rect, fn: rect_area      } │
             │ 1 │ FuncRef { inst: &rect, fn: rect_perimeter } │
             └───┴─────────────────────────────────────────────┘
```

### Key Properties

1. **Zero tables, zero registration, zero slot collisions:**
   `rect.wat` and `square.wat` don't even need to import an interface module or run a startup registrar. Each builds its own read-only vtable once (cached in a module global) and wraps `(data, vtable)` into a 2-ref `ro_handle`.
2. **Pure data objects & unlimited interfaces:**
   The underlying `data` block stores only private fields (`[width, height]`). The exact same `data` handle can be wrapped with different vtables (`Shape`, `Drawable`, `Serializable`) with zero layout conflicts.
3. **100% unforgeable capabilities:**
   Both `vtable` and `trait_obj` are `ro_handle`s, and `funcref`s cannot be forged from integers.
4. **Automatic GC:**
   Because `data` and `vtable` live in `refs[0]` and `refs[1]` of the trait object, the precise split-block GC automatically keeps both alive whenever the trait object is reachable and reclaims them when dropped.

### Direct Dispatch in WAT

```wat
(type $shape_method (func (param handle) (result i32)))

;; Call method 0 (area) on any Shape trait object — zero tables!
(call_ref (type $shape_method)
  (mem.read_ref  (local.get $trait) (i32.const 0))                 ;; self data
  (mem.read_func (mem.read_ref (local.get $trait) (i32.const 1))  ;; vtable
                 (i32.const 0)))                                  ;; method 0 = area
```

See [`examples/06_polymorphic_interfaces/`](../examples/06_polymorphic_interfaces/) for a complete runnable 3-module example (`rect.wat`, `square.wat`, `main.wat`).

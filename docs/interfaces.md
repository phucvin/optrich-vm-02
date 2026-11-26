# Polymorphic Cross-Module Interfaces (Trait Objects & Dynamic Vtables)

This document explains how Optrich VM 02 supports open-world, multi-module polymorphism cleanly—and why naive WebAssembly MVP static tables are not enough.

---

## Why Static Wasm MVP `(elem (i32.const N))` Tables Break Down

A naive Wasm MVP vtable exports a shared table and has each implementer populate a hardcoded slot:
```wat
(import "shape_iface" "vtable" (table $vtable 8 funcref))
(elem (i32.const 0) $rect_area) ;; hardcoded slot 0!
```
This has four major architectural problems in a multi-module system:

1. **Implicit table index 0:** `(elem (i32.const 0) ...)` defaults to table `0`. As soon as a module implements two interfaces, it must track explicit `(table $t)` indices everywhere.
2. **Hardcoded slot collisions:** Independently authored modules (`rect.wat`, `triangle.wat`) have no way to coordinate slot IDs—two modules both writing slot `0` silently overwrite each other.
3. **Object layout conflicts across multiple interfaces:** Embedding the vtable slot inside `bytes[0..4)` of the object itself breaks as soon as one type implements two independent interfaces (`Shape` and `Perimeter`), because both interfaces would fight over `bytes[0..4)`.
4. **Mutable / forgeable dispatch tables:** Exporting a shared table lets any module overwrite another module's slots, and embedding a plain `i32` slot in a mutable object lets callers tamper with the tag.

---

## Optrich VM 02's Solution: Dynamic `funcref` Registration + Read-Only Trait Objects

Optrich VM 02 provides first-class **`funcref` values**, **`ref.func`**, **dynamic table ops (`table.grow`, `table.set`, `table.get`, `table.size`)**, and **`(start $init)`** module initializers. Together with split-block handles, they enable **Go/Rust-style trait objects** (`(itab, data)` / `dyn Trait`):

```text
  Trait Object (ro_handle)
  ┌──────────────────┬──────────────────┐
  │ bytes[0..4)      │ refs[0]          │
  │ vtable_slot (i32)│ data_handle ─────┼──▶ [ width | height ] (pure data!)
  └────────┬─────────┴──────────────────┘
           │
           ▼
  shape_iface's PRIVATE vtable (table 0 funcref)
  ┌───┬─────────────────────────────────────────┐
  │ 0 │ FuncRef { inst: &rect,   fn: rect_area }│
  │ 1 │ FuncRef { inst: &square, fn: sq_area   }│
  └───┴─────────────────────────────────────────┘
```

### How It Works Step by Step

1. **Private Vtable per Interface:**
   Each interface module (`shape_iface.wat`, `perimeter_iface.wat`) keeps its `funcref` table **completely private** (not exported) and starts it at size `0`:
   ```wat
   (table $vtable 0 funcref)
   ```
   No outside module can overwrite or inspect its slots.

2. **Collision-Free Dynamic Registration (`table.grow` + `ref.func` + `(start $init)`):**
   The interface exports `register(fn: funcref) -> i32`, which grows its private table by 1 and returns the newly allocated slot index:
   ```wat
   (func (export "register") (param $fn funcref) (result i32)
     (table.grow $vtable (local.get $fn) (i32.const 1)))
   ```
   Each implementer (`rect.wat`, `square.wat`) passes `ref.func $method` inside its `(start $init)` function when instantiated by `Runtime`. Because `FuncRef` carries `{ ModuleInstance*, localFuncIdx }`, the interface stores a direct cross-module callable reference with zero hardcoded slot IDs.

3. **Zero-Conflict Multi-Interface Trait Objects (`ro_handle`):**
   The underlying object (`rect`) stores **only its own data** (`[width, height]`)—no vtable tag in its bytes.
   To view `rect` as a `Shape` or as a `Perimeter`, `wrap(data, slot)` creates a tiny **read-only** split-block trait handle (`bytes[0..4) = slot`, `refs[0] = data`):
   * One `rect` handle can be wrapped as both a `Shape` and a `Perimeter` simultaneously with **zero layout conflicts**.
   * Because the wrapper is a `ro_handle`, callers cannot tamper with `vtable_slot`.
   * Because `data` is stored in `refs[0]`, the **precise GC** automatically traces and retains `data` whenever the trait object is reachable.

4. **Direct Cross-Module Dispatch:**
   Calling `shape_iface.area(trait_obj)` unpacks `(slot, data)` and executes `call_indirect $vtable (type $area_sig) (data) (slot)`. The VM switches directly to the target `ModuleInstance` (`rect` or `square`) and pushes a `CallFrame` onto the shared call stack with zero copying or host-wrapper boxing.

---

## Comparison with Go & Rust

| Concept | Go Interface | Rust `dyn Trait` | Optrich VM 02 Trait Object |
| :--- | :--- | :--- | :--- |
| Fat representation | `(itab*, data*)` | `(vtable*, data*)` | `ro_handle` (`bytes[0..4): slot`, `refs[0]: data`) |
| Underlying object layout | Pure data | Pure data | Pure data (split block) |
| Multiple interfaces per type | Yes (separate `itab`) | Yes (separate `vtable`) | Yes (separate interface wrapper) |
| Slot / vtable coordination | Runtime / linker | Compiler | Dynamic `table.grow` in private vtable |
| GC lifetime of `data` | Traced via `data*` | Borrow checker / `Arc` | Traced via `refs[0]` in split block |

See [`examples/06_polymorphic_interfaces/`](../examples/06_polymorphic_interfaces/) for a complete runnable 5-module implementation (`shape_iface`, `perimeter_iface`, `rect`, `square`, `main`).

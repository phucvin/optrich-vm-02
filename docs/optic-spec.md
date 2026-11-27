# Optic (`.opc`) Language Specification — v0.1 (Draft)

**Status:** Working draft (Iteration 1). Evolved alongside runnable `.opc` / `.guessed.wat` / `.expected.txt` golden tests in `examples/optic/`.

Optic is a small, statically typed, capability-safe language designed to lower **1:1 with zero impedance mismatch** to Optrich VM 02's split-block memory model (`rw_handle` / `ro_handle`, zero-copy `mem.span`, `mem.readonly`, `mem.clone_span`, precise GC, and `call_ref` vtables).

---

## 1. Lexical & Module Structure

* **Comments:** `// line comment`, `/* block comment */`.
* **Identifiers:** `[A-Za-z_][A-Za-z0-9_]*`.
* **Literals:**
  * Integer: `42`, `0xff`, `-1` (default `i32`, or contextual `i64`/`u8`).
  * Float: `3.14` (default `f64`, or contextual `f32`).
  * Bool: `true` (`i32.const 1`), `false` (`i32.const 0`).
  * Byte char: `b','`, `b'\n'` (`u8` / `i32.const`).
  * String: `"hello"` (type `str` = `ro []u8`, lowered to module `(string $sN "...")` + `mem.const_str $sN`).
  * Null: `null` (valid only for nullable `T?`, lowered to `handle.null`).
* **Top-level items in a `.opc` module:**
  * `import mod::name;` (imports an exported function or host function; e.g. `import host::trace_i32;`)
  * `struct Name { ... }`
  * `impl Name { ... }`
  * `trait Name { ... }` *(planned Iteration 2)*
  * `impl Trait for Name { ... }` *(planned Iteration 2)*
  * `[export] fn name(params...) [-> RetType] { ... }`

---

## 2. Type System & WAT Lowering

### 2.1 Primitives (Unboxed Stack/Local Values)

| Optic Type | WAT Type | Size in `bytes` | Read / Write Opcodes |
| :--- | :--- | :--- | :--- |
| `u8` | `i32` (0..255) | 1 | `mem.read_u8` / `mem.write_u8` |
| `bool` | `i32` (0 or 1) | 1 | `mem.read_u8` / `mem.write_u8` |
| `i32` | `i32` | 4 | `mem.read_i32` / `mem.write_i32` |
| `u32` | `i32` (unsigned ops `_u`) | 4 | `mem.read_i32` / `mem.write_i32` |
| `i64` | `i64` | 8 | `mem.read_i64` / `mem.write_i64` |
| `u64` | `i64` (unsigned ops `_u`) | 8 | `mem.read_i64` / `mem.write_i64` |
| `f32` | `f32` | 4 | `mem.read_f32` / `mem.write_f32` |
| `f64` | `f64` | 8 | `mem.read_f64` / `mem.write_f64` |

Explicit numeric conversions use `expr as Type` (e.g., `ch as i32`, `n as f64`, `x as u32`). Unsigned types (`u32`, `u64`, `u8`) lower comparisons, division, remainder, and right-shift to Wasm unsigned opcodes (`_u`).

**Handle Identity:**
* `a === b` / `a !== b` compare handle identity via `mem.eq` (same underlying handle ID) rather than structural field equality.

### 2.2 Capability Reference Types (Split-Block Handles)

| Optic Type | WAT Type | Semantics |
| :--- | :--- | :--- |
| `MyStruct` | `rw_handle` | Non-null mutable managed struct instance |
| `ro MyStruct` | `ro_handle` | Non-null read-only view of a struct (field writes rejected at compile time) |
| `T?` (where `T` is a handle type) | Same as `T` (`rw_handle` / `ro_handle`) | Nullable handle; `null` lowers to `handle.null`, `x != null` lowers to `i32.eqz (mem.is_null x)` |
| `[]T` | `rw_handle` | Mutable contiguous slice/buffer of `T` |
| `ro []T` | `ro_handle` | Read-only zero-copy slice of `T` |
| `str` | `ro_handle` | Type alias for `ro []u8` (UTF-8 bytes, no null terminator) |
| `Vec<T>` | `rw_handle` | Growable list (header split block + backing buffer) |

**Capability Subtyping / Downgrade:**
* `readonly(x)` converts `T` → `ro T` (or `[]T` → `ro []T`) via `mem.readonly`.
* Passing a mutable `T` where `ro T` is expected requires explicit `readonly(x)` (or compiler-inserted `mem.readonly` — *see Open Question #1*).
* `ro T` is **shallow** on the struct handle itself unless nested reference fields are also typed `ro ...` (*see Open Question #2*).

---

## 3. Split-Block Struct Layout

Every `struct` is allocated as a single split block via `mem.alloc(num_bytes, num_refs)`:

1. **Primitive fields** are placed into `bytes` in declaration order, each aligned to its natural size (`u8`/`bool`: 1, `i32`/`f32`: 4, `i64`/`f64`: 8). `num_bytes` is the total byte size (no trailing padding needed unless desired).
2. **Reference fields** (`Struct`, `T?`, `[]T`, `ro []T`, `str`, `Vec<T>`) are placed into `refs` in declaration order at indices `0, 1, 2, ...`. `num_refs` is the count of reference fields.

Example:
```rust
struct Entry {
    id: i32,       // bytes[0..4)
    score: f64,    // bytes[8..16) (8-byte aligned)
    name: str,     // refs[0]
    left: Entry?,  // refs[1]
    right: Entry?, // refs[2]
}
// Allocates: (mem.alloc (i32.const 16) (i32.const 3))
```

**Important VM Note on `mem.read_ref`:**
`mem.read_ref` returns dynamic `handle` in Optrich WAT (the VM does not know statically whether a ref slot held `rw_handle` or `ro_handle`). Therefore, in WAT lowering, locals/expressions receiving a `mem.read_ref` result use `handle` when needed, or `ro_handle` after `mem.readonly` if the field is `ro`. *(See Open Question #3 — we may want typed ref reads or `handle` locals for struct fields.)*

### 3.1 Inline Nested Structs & `mem.sub_block` *(Implemented)*

By default, nested struct fields (e.g., `origin: Point` inside `Rect`) are stored as separate heap allocations referenced via a `refs` slot. To eliminate extra allocations, GC objects, and pointer chasing for small or embedded structs, Optic can support **inlined nested structs** (e.g., `value struct` or an `inline` field modifier) backed by a minimal VM extension: **`mem.sub_block`**.

#### 1. VM Addition: `mem.sub_block`
`MemoryBlock` in `include/optrich/memory_store.hpp` already carries both byte and ref view ranges (`rootHandle`, `byteOffset`, `byteSize`, `refOffset`, `refCount`, `readOnly`), and `as_readonly`, `clone_span`, `read_ref`/`write_ref`, `copy_refs`, and `collect_garbage` already handle blocks with both regions active. Only a combined constructor is needed:

* **WAT / Bytecode opcode:** `(mem.sub_block h byte_off byte_len ref_off ref_len) -> handle`
  * Static return type: preserves `rw_handle` / `ro_handle` / `handle` from `h` (same as `mem.span`).
* **`MemoryStore::make_sub_block(HandleId h, uint32_t byte_off, uint32_t byte_len, uint32_t ref_off, uint32_t ref_len)`:**
  1. Bounds-check `byte_off + byte_len <= src.byteSize` and `ref_off + ref_len <= src.refCount` (trap on OOB).
  2. Resolve `root = (src.rootHandle != kNullHandle) ? src.rootHandle : h`.
  3. Allocate a span slot with:
     * `rootHandle = root`
     * `byteOffset = src.byteOffset + byte_off`, `byteSize = byte_len`
     * `refOffset  = src.refOffset + ref_off`, `refCount = ref_len`
     * `readOnly   = src.readOnly`
  4. Return `to_fat_handle(id)`.

#### 2. Compiler Lowering Rules
* **Flattened layout:** An inline nested struct `Inner` with `(inner_bytes, inner_refs)` is laid out directly inside `Outer`'s split block at static `(base_byte_off, base_ref_off)` (respecting natural alignment for `bytes`).
  * Example: `Rect { w: i32, h: i32, origin: inline Point }` → `mem.alloc(16, 0)` with `w` at `bytes[0..4)`, `h` at `bytes[4..8)`, `origin.x` at `bytes[8..12)`, `origin.y` at `bytes[12..16)`.
* **Direct field access (zero overhead):** `r.origin.x` compiles directly to `(mem.read_i32 $r (i32.const 8))` (and nested ref fields to `(mem.read_ref $r (i32.const base_ref_off + field_idx))`) — **no `mem.sub_block` is allocated**.
* **Passing / escaping as a handle:** When `r.origin` is passed to a function, method, `dyn Trait`, or cross-module call expecting a `Point` / `ro Point` handle, emit:
  ```wat
  (mem.sub_block (local.get $r) (i32.const 8) (i32.const 8) (i32.const 0) (i32.const 0))
  ```
  *(For pure-byte structs with `inner_refs == 0`, `mem.span` is equivalent; `mem.sub_block` handles both pure-byte and mixed `bytes + refs` structs uniformly.)*
* **Flat arrays of inline structs (bonus):** `[]InlineStruct` can be allocated as a single flat block `mem.alloc(N * b_stride, N * r_stride)`. Direct element field access (`arr[i].field`) uses `i * b_stride + field_off` / `i * r_stride + ref_idx`; taking a handle to `arr[i]` uses `mem.sub_block`, and `clone(arr[i])` detaches via `mem.clone_span`.

#### 3. Semantic Notes & Caveats
* **GC retention:** Like `mem.span`, a `mem.sub_block` view pins `rootHandle` alive until dropped or detached with `mem.clone_span`.
* **Handle identity (`===`):** Each `mem.sub_block` call allocates a fresh span `HandleId`, so two views of the same inline field will not compare equal under `mem.eq`. Inline structs should disallow `===` (or compare structurally).
* **Nullability & copying:** Inline fields are non-null by construction; assigning an inline struct copies its bytes (`mem.copy`) and refs (`mem.copy_refs`) into the destination range.

### 3.2 Planned Extension: `value struct` Semantics & Lowering Options

In Optic v0.1, all `struct` types are heap-allocated reference types (`rw_handle` / `ro_handle`). A future `value struct` declaration (for small, data-oriented aggregates like `Point`, `Vec2`, `Color`, `Range`, or `Token`) would provide **value semantics**:

* **Copy-by-value:** Assignment (`let b = a;`) and pass-by-value copy fields rather than aliasing a handle.
* **No handle identity (`===`) & non-nullable:** No `null` directly (use `Option<T>`), structural equality (`==`), and non-recursive (finite size).
* **May contain reference fields:** e.g., `value struct Token { start: i32, len: i32, text: str }` copies `text` by handle reference while the struct itself stays unboxed/inlined.

#### Lowering Options Considered for `optrich-vm-02`

1. **Scalarization + Inlining (Best for small value structs):**
   * Locals → separate Wasm locals (`(local $p_x i32) (local $p_y i32)`), zero heap allocation.
   * Params / returns → exploded parameters + multi-value returns `(result i32 i32)`.
   * Nested fields & `[]ValueStruct` → flattened into parent split blocks (`bytes` + `refs`); `mem.sub_block` (Section 3.1) is emitted only when a handle view escapes (e.g., `dyn Trait` or cross-module handle APIs).
   * *Tradeoff:* Parameter/local bloat for large structs (e.g., >4–6 fields), and stack locals are not addressable (`&mut p`).
2. **Shadow Stack (Compiler-Managed Stack Split-Block):**
   * Allocate a reusable stack split-block (e.g., global `$stack` + `$sp_bytes` / `$sp_refs`). Entering a function bumps `$sp_*`; exiting decrements them and zeroes popped `refs` slots for precise GC.
   * *Tradeoff:* Handles large and address-taken (`&mut p`) value structs with zero GC allocations, at the cost of `$sp` management, ref-slot zeroing on all exit paths, and escape checks.
3. **Immutable Reference-Backed Value Types (`ro_handle`):**
   * Represent `value struct`s as immutable heap split blocks (`ro_handle`). Aliasing is unobservable, so passing/assignment is $O(1)$ and ABI matches normal structs; updates construct a new block.
   * *Tradeoff:* Simplest compiler, but still heap-allocates and chases pointers unless paired with a later scalar-replacement pass.
4. **Mutable Copy-on-Assign Handles (`mem.clone_span`):**
   * Uniform handle ABI; emit `mem.clone_span` on every assignment/pass-by-value.
   * *Tradeoff:* Trivial to implement, but high allocation/copy overhead in hot loops.
5. **VM-Level Scoped Arenas (`arena.enter` / `arena.alloc` / `arena.leave`):**
   * Fast $O(1)$ bump-allocated scope in `MemoryStore` freed on scope exit without GC (see [docs/gc.md](gc.md)).
   * *Tradeoff:* Uniform handle ABI and low GC pressure, but requires escape safety or `mem.clone_span` promotion.
6. **Hybrid Threshold *(Recommended Long-Term)*:**
   * **Small `value struct`s** (e.g., ≤ 4 fields) → **scalarized** into Wasm locals / multi-value returns.
   * **Large or address-taken `value struct`s** → **shadow stack** (or passed via `(handle, b_off, r_off)` / `mem.sub_block`).
   * **Nested fields & arrays** → **flattened** into split blocks with `mem.sub_block` on escape.

*(Note: Variable-sized inline structs inside `optrich::Value` on the VM operand stack are intentionally avoided, as `Value` relies on fixed-size tagged slots for fast interpreter dispatch and simple precise GC root scanning.)*

---

## 4. Statements & Control Flow Lowering

### 4.1 Variables
* `let x = expr;` — immutable local binding (compile-time error to reassign).
* `let mut x = expr;` — mutable local binding.

### 4.2 `for i in start..end` (Superinstruction-Friendly + `continue`-Safe)
To automatically hit Optrich VM's Phase 5 peephole superinstructions (`BrIfLocalGeS` and `LocalIncI32`), `for i in start..end { body }` lowers to:

```wat
(local.set $i <start>)
(local.set $end <end>)
(block $break
  (loop $cont
    (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
    (block $step
      ;; body (`break` -> br $break; `continue` -> br $step)
    )
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $cont)))
```
*(When the loop body contains no `continue`, the compiler omits `(block $step ...)`.)*

### 4.3 `while cond { body }`
```wat
(block $break
  (loop $cont
    (br_if $break (i32.eqz <cond>))
    ;; body (`break` -> br $break; `continue` -> br $cont)
    (br $cont)))
```

### 4.4 Short-Circuit `&&` / `||`
* `a && b` → `(if (result i32) <a> (then (i32.ne <b> (i32.const 0))) (else (i32.const 0)))`
* `a || b` → `(if (result i32) <a> (then (i32.const 1)) (else (i32.ne <b> (i32.const 0))))`

### 4.5 `if cond { a } else { b }`
Lowers to Wasm `(if (result T) <cond> (then ...) (else ...))` for expressions, or `(if <cond> (then ...) (else ...))` for statements.

---

## 5. Slices, Strings & `Vec<T>`

### 5.1 Primitive Slices (`[]u8`, `[]i32`, `ro []u8`, `str`)
* `new [T; n]` → `(mem.alloc (i32.mul n (i32.const sizeof(T))) (i32.const 0))`
* `s.len` → `(mem.byte_len s)` (for `u8`/`bool`), or `(i32.div_u (mem.byte_len s) (i32.const sizeof(T)))` (or `i32.shr_u` by shift) for `i32`/`i64`/`f32`/`f64`.
* `s[i]` → `(mem.read_<T> s (i32.mul i (i32.const sizeof(T))))` (shift for power-of-2 sizes).
* `s[start..end]` → `(mem.span s (i32.mul start sizeof(T)) (i32.mul (i32.sub end start) sizeof(T)))` — **O(1) zero-copy**, inherits `ro`/`rw`.
* `readonly(s)` → `(mem.readonly s)`.
* `clone(s)` → `(mem.clone_span s)` — detaches slice into a fresh owning block so the original large backing buffer can be GC'd.

### 5.2 Reference Slices (`[]MyStruct`, `[]str`, `ro []str`)
* `new [T; n]` → `(mem.alloc (i32.const 0) n)`
* `s.len` → `(mem.ref_len s)`
* `s[i]` → `(mem.read_ref s i)`
* `s[start..end]` → `(mem.ref_span s start (i32.sub end start))`

### 5.3 Growable `Vec<T>` (Header + Backing Block Pattern)
A `Vec<T>` is a 2-part split-block container:
* **Header block:** `mem.alloc(8, 1)`
  * `bytes[0..4)`: `len` (`i32`)
  * `bytes[4..8)`: `cap` (`i32`)
  * `refs[0]`: backing buffer (`mem.alloc(cap * sizeof(T), 0)` for primitives, or `mem.alloc(0, cap)` for references)
* **Growth:** When `len == cap`, allocate `new_cap = max(cap * 2, 4)` backing block, copy existing elements (`mem.copy` for primitives; `mem.copy_refs` for references), and update `refs[0]` and `cap`.

---

## 6. Inherent Methods (`impl Type`)

```rust
impl Counter {
    fn inc(self, delta: i32) {
        self.value += delta;
    }
}
```
Lowers to a regular module function `$Counter__inc` taking `(param $self rw_handle)` (or `ro_handle` if `self: ro Counter`) as its first parameter.

---

## 7. Enums, `match`, Tuples, `Result`, `?` & `defer`

### 7.1 Tagged Split-Block `enum`s
An `enum` with variants is lowered to a single split block:
* `bytes[0..4)`: `i32` discriminant tag (`0, 1, ..., N-1`).
* `bytes[4..)`: union of primitive payload fields across all variants (each field naturally aligned; total `num_bytes` is the max byte size across variants).
* `refs[0..)`: union of reference payload fields across all variants (`num_refs` is the max ref count across variants).
* **GC-safe variant overwrite:** Whenever an enum instance is overwritten in-place with a variant that uses fewer `refs` slots than `num_refs`, the compiler **must write `handle.null`** to all unused `refs` slots so the precise GC does not retain dead references from the previous variant.

### 7.2 `match` Lowering
* Multi-variant `match e { ... }` lowers to a Wasm `br_table` over the discriminant tag loaded from `bytes[0..4)` (dense `0..N-1` jump table).
* Two-variant `Option` / `Result` matches lower directly to `(if ...)`.

### 7.3 `Option<T>`, `Result<T, E>` & Unboxed Stack Tuples `(A, B)`
* **Unboxed stack tuples `(A, B)`:** Lowered to multiple Wasm stack values / locals (`$t_0`, `$t_1`) and multi-value function returns `(result A B)` with zero heap allocation.
* **`Option<T>`:**
  * When `T` is a handle type (`MyStruct`, `str`, `[]u8`), `Option<T>` uses **null-niche optimization** and lowers identically to `T?` (`handle.null` = `None`, non-null = `Some(h)`).
  * When `T` is a primitive (`i32`, `f64`, etc.), `Option<T>` lowers on the stack/return boundary to an unboxed multi-value pair `(is_some: i32, val: T)` (or a `mem.alloc(4 + sizeof(T), 0)` split block when boxed in a heap container).
* **`Result<T, E>`:**
  * Lowers on the stack/return boundary to an unboxed multi-value tuple `(is_err: i32, ok_val: T, err_val: E)` (or `(is_err: i32, payload: T)` when `T` and `E` share the same Wasm `ValType`), avoiding heap allocation on the happy path.
* **`?` Error Propagation:**
  * `let x = expr?;` checks `is_err`; if non-zero, executes all active `defer` blocks in LIFO order and returns the `Err` multi-value immediately.

### 7.4 `defer { ... }`
`defer` registers cleanup code to run in **LIFO (last-in, first-out) order** on every exit path from the enclosing scope (normal fallthrough, explicit `return`, or `?` early return). The compiler lowers `defer` by emitting the active deferred blocks before each return point (or branching to a shared cleanup epilogue).

---

## 8. Generics & Monomorphization

Generic structs (`struct Box<T>`, `struct Pair<A, B>`) and generic functions (`fn f<T: Trait>(...)`) are **monomorphized at compile time** per concrete type instantiation (like Rust):
* **Split-block field partitioning per instantiation:**
  * `Box<i32>` → `mem.alloc(4, 0)` (`bytes[0..4)`)
  * `Box<str>` → `mem.alloc(0, 1)` (`refs[0]`)
  * `Pair<i32, str>` → `mem.alloc(4, 1)` (`bytes[0..4)` + `refs[0]`)
  * `Pair<f64, i32>` → `mem.alloc(12, 0)` (`bytes[0..8)` + `bytes[8..12)`)
* **Static trait-bound dispatch:** Generic functions with trait bounds (`fn total_score<T: Scorable>(items: ro []T) -> i32`) monomorphize into direct `call $ConcreteType__method` instructions with zero vtables and zero boxing.
* **Mangling convention:** `$Struct_Arg1_Arg2__method` and `$func__Arg1`.

---

## 9. Multi-Module Sharing, Globals & Host Capabilities

* **Cross-module `import` / `export`:** Functions marked `export fn` lower to `(func (export "name") ...)`; `import mod::func;` lowers to `(import "mod" "func" (func $func ...))`. Handles (`rw_handle`, `ro_handle`, `handle`) cross module boundaries directly with zero copying because all modules share one `MemoryStore` and one precise GC.
* **Module `const` and `static mut` globals:**
  * `const X: i32 = 42;` lowers to an immutable Wasm `(global $X i32 (i32.const 42))` (or inline constant).
  * `static mut COUNT: i32 = 0;` lowers to `(global $COUNT (mut i32) (i32.const 0))`.
  * Module globals are initialized in dependency/instantiation order when each module is instantiated.
* **Capability-safe Host I/O:** Host APIs accept `ro []u8` for read-only outputs (`io_write`), `[]u8` (`rw_handle`) for writable receive buffers (`io_read`), and return opaque unforgeable `ro_handle`s for host-managed resources (`io_open_reader`).

---

## 10. Open Design Questions (To Resolve Across Iterations)

1. **Implicit vs. explicit `readonly(...)`:** Should passing `[]u8` to a `ro []u8` parameter require explicit `readonly(buf)` or auto-insert `mem.readonly`? (Explicit makes the capability downgrade visible; implicit is more ergonomic.)
2. **Shallow vs. transitive `ro T`:** If `e: ro Entry` has a mutable child field `child: Node`, should `e.child` be `ro Node` (transitive) or `Node` (shallow)?
3. **WAT `mem.read_ref` return type:** `mem.read_ref` currently returns dynamic `handle` in Optrich VM. When a struct stores a `rw_handle` or `ro_handle` in a `refs` slot and reads it back, WAT requires `handle` (not `rw_handle`/`ro_handle`) unless the VM or compiler knows the static type. Let's observe how much friction this causes in `.guessed.wat` during Iteration 1!
4. ~~**VM `mem.copy_refs` opcode:**~~ **Resolved:** Added `mem.copy_refs(dst, dst_off, src, src_off, count)` and made `mem.readonly` a zero-allocation no-op on already-`ro` handles (eliminating redundant wrapper blocks on `mem.readonly(mem.read_ref(...))`).

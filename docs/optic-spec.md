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
| `i64` | `i64` | 8 | `mem.read_i64` / `mem.write_i64` |
| `f32` | `f32` | 4 | `mem.read_f32` / `mem.write_f32` |
| `f64` | `f64` | 8 | `mem.read_f64` / `mem.write_f64` |

Explicit numeric conversions use `expr as Type` (e.g., `ch as i32`, `n as f64`).

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

---

## 4. Statements & Control Flow Lowering

### 4.1 Variables
* `let x = expr;` — immutable local binding (compile-time error to reassign).
* `let mut x = expr;` — mutable local binding.

### 4.2 `for i in start..end` (Superinstruction-Friendly Pattern)
To automatically hit Optrich VM's Phase 5 peephole superinstructions (`BrIfLocalGeS` and `LocalIncI32`), `for i in start..end { body }` lowers to:

```wat
(local.set $i <start>)
(local.set $end <end>)
(block $break
  (loop $cont
    (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
    ;; body
    (local.set $i (i32.add (local.get $i) (i32.const 1)))
    (br $cont)))
```

### 4.3 `while cond { body }`
```wat
(block $break
  (loop $cont
    (br_if $break (i32.eqz <cond>))
    ;; body
    (br $cont)))
```

### 4.4 `if cond { a } else { b }`
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
* **Growth:** When `len == cap`, allocate `new_cap = max(cap * 2, 4)` backing block, copy existing elements (`mem.copy` for primitives; loop of `mem.read_ref`/`mem.write_ref` for refs until `mem.copy_refs` is added), and update `refs[0]` and `cap`.

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

## 7. Open Design Questions (To Resolve Across Iterations)

1. **Implicit vs. explicit `readonly(...)`:** Should passing `[]u8` to a `ro []u8` parameter require explicit `readonly(buf)` or auto-insert `mem.readonly`? (Explicit makes the capability downgrade visible; implicit is more ergonomic.)
2. **Shallow vs. transitive `ro T`:** If `e: ro Entry` has a mutable child field `child: Node`, should `e.child` be `ro Node` (transitive) or `Node` (shallow)?
3. **WAT `mem.read_ref` return type:** `mem.read_ref` currently returns dynamic `handle` in Optrich VM. When a struct stores a `rw_handle` or `ro_handle` in a `refs` slot and reads it back, WAT requires `handle` (not `rw_handle`/`ro_handle`) unless the VM or compiler knows the static type. Let's observe how much friction this causes in `.guessed.wat` during Iteration 1!
4. **VM `mem.copy_refs` opcode:** Observe how verbose ref-`Vec` growth is without `mem.copy_refs`.

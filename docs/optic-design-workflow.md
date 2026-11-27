# Optic (`.opc`) Design & Golden-Test Workflow

Before implementing the Optic compiler, we design the language **spec-first and lowering-first** using runnable golden tests on `optrich-vm-02`.

---

## 1. Iterative Design Loop

1. **Write / refine the language spec** (`docs/optic-spec.md`): syntax, type system, capability rules (`ro`, `T?`, slices, structs, traits, modules), and WAT lowering conventions.
2. **Write Optic examples** (`examples/optic/<name>.opc`): realistic programs exercising the language features—no compiler required yet.
3. **Hand-lower to `.guessed.wat`** (`examples/optic/<name>.guessed.wat`): translate each `.opc` example into the Optrich WAT that the compiler *should* emit according to the spec and VM conventions.
4. **Verify via golden tests** (`examples/optic/<name>.expected.txt`): run the `.guessed.wat` on `optrich-vm-02` and verify **every intermediate step and final return value** against the golden output. If anything fails, feels awkward, or misses VM optimizations, update the spec, `.opc` example, or `.guessed.wat`.
5. **Review & refine**: inspect both `.opc` ergonomics and `.guessed.wat` code quality (e.g., split-block layout, zero-copy spans, GC behavior, Phase 5 peephole superinstructions).
6. **Repeat** until we have broad example coverage with clean, optimal, verified lowerings—then implement the Optic compiler against the exact same `.opc` + `.expected.txt` golden suite.

---

## 2. Example & Golden File Layout

Each single-module example lives as a triplet under `examples/optic/`:

```text
examples/optic/
  01_basics.opc              # Optic source
  01_basics.guessed.wat      # Hand-lowered Optrich WAT per docs/optic-spec.md
  01_basics.expected.txt     # Golden trace of intermediate steps + return value
```

Multi-module examples use a subdirectory with one `.opc` + `.guessed.wat` per module and a shared `expected.txt`:

```text
examples/optic/07_multimodule_sharing/
  inspector.opc
  inspector.guessed.wat
  producer.opc
  producer.guessed.wat
  expected.txt
```

Negative and runtime-trap tests *(planned in Iteration 10)* use:
* `<name>.fail.opc` — programs that the compiler must reject statically (with expected diagnostic/rule comments).
* `<name>.trap.guessed.wat` + `<name>.trap.expected.txt` — programs expected to trap at runtime (e.g., OOB slice/index, `null` deref, division by zero).

---

## 3. Step-Level Verification (`host::trace_*`)

To verify intermediate execution steps (not just a single final integer), the golden test harness (`tests/test_optic_golden.cpp`, run via `make test-optic`) registers lightweight host trace functions via `Runtime::register_host_func`:

| Host Import (`module` `"host"`) | WAT Signature | Golden Output Format |
| :--- | :--- | :--- |
| `trace_i32` | `(param ro_handle i32)` | `<label>: <i32>` |
| `trace_i64` | `(param ro_handle i64)` | `<label>: <i64>` |
| `trace_f64` | `(param ro_handle f64)` | `<label>: <f64>` |
| `trace_str` | `(param ro_handle ro_handle)` | `<label>: "<utf8 bytes>"` |
| `trace_meta` | `(param ro_handle handle)` | `<label>: ro=<true\|false> bytes=<N> refs=<M>` (or `null`) |
| `trace_gc` | `(param ro_handle)` | `<label>: live=<N> swept=<M>` |

After `main` completes, the runner appends the final return value(s) as `return: <val>` and performs an exact line-by-line diff against `.expected.txt`.

### Example `.expected.txt`

```text
step1.tokens_count: 3
step2.tok0: "apple"
step3.tok1: "banana"
step4.tok1_meta: ro=true bytes=6 refs=0
return: 361
```

---

## 4. Planned Coverage Across Iterations

To ensure Optic is not just a minimal demo language but a practical capability-safe alternative for workloads people write in C, Rust, or Go, the golden-test suite expands across 12 iterations:

1. **Core basics & control flow:** primitives (`u8`, `i32`, `i64`, `f32`, `f64`, `bool`), `let` / `let mut`, `if`/`else` (statement & expression), `while`, `for i in a..b`, `break`/`continue`, early `return`, short-circuit `&&`/`||`, `as` casts, signed/unsigned & bitwise ops, recursion (`fib`), tight loops (verifying Phase 5 superinstructions).
2. **Split-block structs, nullability & `ro` capabilities:** mixed primitive+ref `struct`, nested structs, `[]MyStruct`, automatic `bytes`/`refs` partitioning, `T?` / `null`, `ro T`, inherent `impl` methods, handle identity (`mem.eq`) vs. value equality.
3. **Zero-copy slices, strings, `Vec<T>` & GC lifecycle:** `ro []u8`, `[]T`, `new [T; n]`, `s[start..end]` (`mem.span`), chained spans & mutable aliasing, `readonly(s)`, `clone(s)` (`mem.clone_span` + `trace_gc` proof), growable `Vec<T>`, cyclic object graphs + GC collection verification.
4. **Enums, `match`, tuples & error handling:** tagged split-block `enum`s (including GC-safe nulling of inactive ref slots), `Option<T>` (for both primitives and handles), `Result<T, E>`, `match` lowering, multi-value returns / unboxed stack tuples `(T, E)`, `?` error propagation, and `defer` for deterministic resource cleanup across all exit paths.
5. **Generics & monomorphization:** user-defined generic structs & functions (e.g., `Pair<A, B>`, `Box<T>`) instantiated with both primitives (`bytes`) and handles (`refs`), plus trait bounds.
6. **Binary buffers, inline structs & low-level systems ops:** structured little/big-endian reads/writes over `[]u8` / `ro []u8` (wire/packet/file parsing without C pointer UB), bulk `mem.copy`, `ByteBuf`/`StringBuilder`, formatting, UTF-8 vs. byte iteration, bit manipulation (`clz`/`ctz`/`popcnt`, wrapping arithmetic — e.g., CRC32/hashing), and **inlined nested structs** (`value struct` / `inline` fields + flat arrays of inline structs) backed by the `mem.sub_block` VM opcode (see [docs/optic-spec.md](optic-spec.md#31-planned-extension-inline-nested-structs--memsub_block)).
7. **Multi-module sharing, globals & host capabilities:** cross-module `import` / `export` with zero-copy `rw` → `ro` slice passing, module `const`/globals & init order, and capability-safe host I/O interfaces (`Reader`/`Writer` over `[]u8`, opaque host resource handles).
8. **Traits, iterators & dynamic dispatch (`dyn Trait`):** `trait`, `impl Trait for Struct`, zero-allocation iterator protocol (`for x in iter`), `dyn Trait` (`call_ref` + frozen vtable blocks), including cross-module dispatch.
9. **Closures & mutable captures:** non-capturing `fn` values, capturing closures (environment split blocks), heap-promoted `let mut` capture cells, and cross-module callbacks via `call_ref`.
10. **Negative & trap test suite:** compile-time rejection specs (`.fail.opc` for `ro` writes, unchecked `T?`, `let` mutation, capability rules) and runtime trap specs (`.trap.expected.txt` for OOB access, null deref, div-by-zero).
11. **Core data structures:** generic `HashMap<K, V>` (`Hash` + `Eq` traits), `Set<T>`, and ring buffer / `VecDeque<T>` validating generics + traits + slices + GC working together.
12. **Capstone integration & benchmark parity:** realistic end-to-end programs (e.g., a zero-copy tokenizer/JSON or binary protocol parser, a multi-module pipeline) and Optic ports of Phase 5 benchmarks (`sieve`, `array_sum`) verifying performance parity with hand-written WAT.

> **Post-v1 / VM-dependent horizon:** Cooperative concurrency (state-machine `async` / coroutines or actor message-passing using zero-copy `ro` handles) and SIMD/vectorized byte ops are intentionally deferred until after the v1 compiler and golden suite are solid.

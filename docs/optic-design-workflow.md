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
examples/optic/04_multimodule_sharing/
  inspector.opc
  inspector.guessed.wat
  producer.opc
  producer.guessed.wat
  expected.txt
```

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

1. **Core basics:** primitives, `let` / `let mut`, `if`/`else`, `while`, `for i in a..b`, recursion (`fib`), tight loops (verifying Phase 5 superinstructions).
2. **Split-block structs & nullability:** mixed primitive+ref `struct`, automatic `bytes`/`refs` partitioning, `T?` / `null`, `ro T`, inherent `impl` methods.
3. **Zero-copy slices, strings & `Vec<T>`:** `ro []u8`, `[]T`, `new [T; n]`, `s[start..end]` (`mem.span`), `readonly(s)`, `clone(s)` (`mem.clone_span` + `trace_gc` proof), growable `Vec<T>`.
4. **Multi-module sharing:** cross-module `import` / `export` with zero-copy `rw` → `ro` slice passing.
5. **Polymorphic traits & closures:** `trait`, `impl Trait for Struct`, `dyn Trait` (`call_ref` + frozen vtable blocks), and closures.

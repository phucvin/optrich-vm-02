# Optrich VM 02 Examples

Build the CLI first with `make`, or run all examples at once with `make examples`.

| Example | Phase | Showcases | Expected Output |
| :--- | :---: | :--- | :--- |
| [`span_demo.wat`](span_demo.wat) | 1 & 2 | Zero-copy `ro_handle` string slice checksum | `872` |
| [`01_core_wasm.wat`](01_core_wasm.wat) | 1 | `i32`/`f64`, bitwise ops, conversions, `if`/`else`, `br_table`, recursion, globals | `264` |
| [`02_split_blocks_linked_list.wat`](02_split_blocks_linked_list.wat) | 1 & 2 | Split-block `mem.alloc(8, 1)` linked list (`bytes` + GC `refs`) | `95` |
| [`03_bulk_and_spans.wat`](03_bulk_and_spans.wat) | 2 | `mem.fill`, overlapping `mem.copy`, nested `mem.span`, `mem.readonly` | `260` |
| [`04_gc_and_clone_span.wat`](04_gc_and_clone_span.wat) | 3 | Precise cycle collection, auto-GC threshold, `mem.clone_span` detaching 4 KiB buffers | `4950` + `[gc] ...` |
| [`05_multimodule_sharing/`](05_multimodule_sharing/) | 4 | Direct cross-module calls passing zero-copy `ro_handle` spans + `mem.cmp` | `102` |
| [`06_polymorphic_interfaces/`](06_polymorphic_interfaces/) | 4 | Stateless `shape.wat` interface + Go/Rust trait objects (`ro_handle` `[data, vtable]`), multi-signature `call_ref` (`area`, `scale`) | `316` + `[gc] ...` |

## Running Individually

```bash
./build/optrich-vm examples/span_demo.wat main
./build/optrich-vm examples/01_core_wasm.wat main
./build/optrich-vm examples/02_split_blocks_linked_list.wat main
./build/optrich-vm examples/03_bulk_and_spans.wat main
./build/optrich-vm --gc-threshold 32 --gc-stats examples/04_gc_and_clone_span.wat main

# Multi-module zero-copy sharing
./build/optrich-vm \
  --link strlib=examples/05_multimodule_sharing/strlib.wat \
  examples/05_multimodule_sharing/app.wat main

# Cross-module Go/Rust-style trait objects (stateless shape.wat + vtable blocks + call_ref)
./build/optrich-vm \
  --link shape=examples/06_polymorphic_interfaces/shape.wat \
  --link rect=examples/06_polymorphic_interfaces/rect.wat \
  --link square=examples/06_polymorphic_interfaces/square.wat \
  --gc-stats \
  examples/06_polymorphic_interfaces/main.wat main
```

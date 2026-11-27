# Optrich VM 02 Benchmarks

Micro-benchmarks for measuring the current computed-goto bytecode interpreter, fat-handle memory operations, zero-copy spans, polymorphic dispatch, and precise GC.

## Running

```bash
# Build and run the full benchmark suite (includes fib(40) and native C++ -O2 baselines)
make bench

# Run a single benchmark by substring filter
./build/bench_vm --filter fib
./build/bench_vm --filter span

# Override fib(N) or iteration count
./build/bench_vm --fib 35 --runs 3

# Run any .wat benchmark directly through the CLI
./build/optrich-vm benchmarks/fib.wat fib 40
./build/optrich-vm benchmarks/arith_loop.wat sum_loop 10000000
./build/optrich-vm benchmarks/array_ops.wat array_sum 65536 50
```

## Benchmarks Included

| File | Benchmark(s) | What It Measures |
| :--- | :--- | :--- |
| `fib.wat` | `fib(40)` | Recursive call/return frame overhead, branching, `i32` arithmetic (331,160,281 calls) |
| `arith_loop.wat` | `sum_loop(10M)`, `sieve(100K)` | Tight loop ALU throughput and byte-array access |
| `array_ops.wat` | `array_sum(64K x 50)` | Fat-handle `mem.write_i32` / `mem.read_i32` bounds-checked throughput |
| `span_vs_copy.wat` | `span_zero_copy`, `span_copy_based` | $O(1)$ `mem.span` + `mem.readonly` vs. `mem.alloc` + `mem.copy` on 64 KB buffers |
| `dispatch.wat` | `dispatch_direct`, `dispatch_indirect`, `dispatch_call_ref` | Direct `call` vs. `call_indirect` table vs. `call_ref` vtable dispatch |
| `gc_stress.wat` | `gc_raw_bytes`, `gc_ref_chain` | Precise GC throughput on raw-byte-only blocks ($O(1)$ mark) vs. ref-heavy linked lists |

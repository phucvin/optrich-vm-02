;; Benchmark: Fat-Handle Typed Array Throughput
;; Stresses: mem.write_i32 / mem.read_i32 bounds-checked fat-handle fast path.
;;
;; Run: ./build/optrich-vm benchmarks/array_ops.wat array_sum 65536 50
(module
  ;; Allocate an array of `len` i32s, initialize arr[i] = i, then sum it `iters` times.
  (func (export "array_sum") (param $len i32) (param $iters i32) (result i64)
    (local $buf rw_handle)
    (local $i i32)
    (local $off i32)
    (local $it i32)
    (local $total i64)
    (local $byte_len i32)

    (local.set $byte_len (i32.shl (local.get $len) (i32.const 2)))
    (local.set $buf (mem.alloc (local.get $byte_len) (i32.const 0)))

    ;; Fill array: buf[i] = i
    (block $fill_done
      (loop $fill
        (br_if $fill_done (i32.ge_u (local.get $off) (local.get $byte_len)))
        (mem.write_i32 (local.get $buf) (local.get $off) (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (local.set $off (i32.add (local.get $off) (i32.const 4)))
        (br $fill)))

    ;; Sum array `iters` times
    (block $outer_done
      (loop $outer
        (br_if $outer_done (i32.ge_s (local.get $it) (local.get $iters)))
        (local.set $off (i32.const 0))
        (block $sum_done
          (loop $sum_lp
            (br_if $sum_done (i32.ge_u (local.get $off) (local.get $byte_len)))
            (local.set $total
              (i64.add (local.get $total)
                       (i64.extend_i32_u (mem.read_i32 (local.get $buf) (local.get $off)))))
            (local.set $off (i32.add (local.get $off) (i32.const 4)))
            (br $sum_lp)))
        (local.set $it (i32.add (local.get $it) (i32.const 1)))
        (br $outer)))

    (local.get $total))
)

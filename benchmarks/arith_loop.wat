;; Benchmark: Tight Arithmetic Loops & Sieve of Eratosthenes
;; Stresses: local.get/set, loop/br_if dispatch, i32/i64 ALU, and byte array access.
;;
;; Run: ./build/optrich-vm benchmarks/arith_loop.wat sum_loop 10000000
;;      ./build/optrich-vm benchmarks/arith_loop.wat sieve 100000
(module
  ;; Tight integer arithmetic loop: sum_{i=0}^{n-1} (i * 3 + 1) ^ (i >> 2)
  (func (export "sum_loop") (param $n i32) (result i64)
    (local $i i32)
    (local $acc i64)
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $acc
          (i64.add
            (local.get $acc)
            (i64.extend_i32_u
              (i32.xor
                (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1))
                (i32.shr_u (local.get $i) (i32.const 2))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $acc))

  ;; Sieve of Eratosthenes: count primes <= limit (using split-block byte array)
  ;; sieve(100000) = 9592
  (func (export "sieve") (param $limit i32) (result i32)
    (local $flags rw_handle)
    (local $p i32)
    (local $m i32)
    (local $count i32)

    (local.set $flags (mem.alloc (i32.add (local.get $limit) (i32.const 1)) (i32.const 0)))
    (mem.fill (local.get $flags) (i32.const 0) (i32.const 1) (i32.add (local.get $limit) (i32.const 1)))
    (mem.write_u8 (local.get $flags) (i32.const 0) (i32.const 0))
    (mem.write_u8 (local.get $flags) (i32.const 1) (i32.const 0))

    (local.set $p (i32.const 2))
    (block $outer_done
      (loop $outer
        (br_if $outer_done (i32.gt_s (i32.mul (local.get $p) (local.get $p)) (local.get $limit)))
        (if (mem.read_u8 (local.get $flags) (local.get $p))
          (then
            (local.set $m (i32.mul (local.get $p) (local.get $p)))
            (block $inner_done
              (loop $inner
                (br_if $inner_done (i32.gt_s (local.get $m) (local.get $limit)))
                (mem.write_u8 (local.get $flags) (local.get $m) (i32.const 0))
                (local.set $m (i32.add (local.get $m) (local.get $p)))
                (br $inner)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $outer)))

    (local.set $p (i32.const 2))
    (block $count_done
      (loop $count_lp
        (br_if $count_done (i32.gt_s (local.get $p) (local.get $limit)))
        (local.set $count (i32.add (local.get $count) (mem.read_u8 (local.get $flags) (local.get $p))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $count_lp)))

    (local.get $count))
)

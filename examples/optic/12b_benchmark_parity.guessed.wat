;; Guessed WAT for examples/optic/12b_benchmark_parity.opc
;; Tight loops follow Optic spec 4.2 to hit Optrich VM Phase 5 peephole superinstructions
;; (BrIfLocalGeS, LocalIncI32, LocalMemReadU8, LocalMemReadI32).
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))

  (string $l1 "step1.primes_le_1000")
  (string $l2 "step2.array_sum_1000")

  (func $sieve_count (param $limit i32) (result i32)
    (local $flags rw_handle)
    (local $end i32)
    (local $i i32)
    (local $p i32)
    (local $m i32)
    (local $count i32)

    (local.set $end (i32.add (local.get $limit) (i32.const 1)))
    (local.set $flags (mem.alloc (local.get $end) (i32.const 0)))
    (mem.fill (local.get $flags) (i32.const 2) (i32.const 1) (i32.sub (local.get $end) (i32.const 2)))

    (local.set $p (i32.const 2))
    (block $done_p
      (loop $lp_p
        (br_if $done_p (i32.gt_s (i32.mul (local.get $p) (local.get $p)) (local.get $limit)))
        (if (mem.read_u8 (local.get $flags) (local.get $p))
          (then
            (local.set $m (i32.mul (local.get $p) (local.get $p)))
            (block $done_m
              (loop $lp_m
                (br_if $done_m (i32.gt_s (local.get $m) (local.get $limit)))
                (mem.write_u8 (local.get $flags) (local.get $m) (i32.const 0))
                (local.set $m (i32.add (local.get $m) (local.get $p)))
                (br $lp_m)))))
        (local.set $p (i32.add (local.get $p) (i32.const 1)))
        (br $lp_p)))

    (local.set $i (i32.const 2))
    (block $done_c
      (loop $lp_c
        (br_if $done_c (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $count (i32.add (local.get $count) (mem.read_u8 (local.get $flags) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp_c)))
    (local.get $count))

  (func $array_sum (param $n i32) (result i32)
    (local $arr rw_handle)
    (local $i i32)
    (local $off i32)
    (local $byte_end i32)
    (local $sum i32)

    (local.set $byte_end (i32.shl (local.get $n) (i32.const 2)))
    (local.set $arr (mem.alloc (local.get $byte_end) (i32.const 0)))

    (local.set $i (i32.const 0))
    (block $done_fill
      (loop $lp_fill
        (br_if $done_fill (i32.ge_s (local.get $i) (local.get $n)))
        (mem.write_i32 (local.get $arr) (i32.shl (local.get $i) (i32.const 2)) (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp_fill)))

    (local.set $off (i32.const 0))
    (block $done_sum
      (loop $lp_sum
        (br_if $done_sum (i32.ge_s (local.get $off) (local.get $byte_end)))
        (local.set $sum (i32.add (local.get $sum) (mem.read_i32 (local.get $arr) (local.get $off))))
        (local.set $off (i32.add (local.get $off) (i32.const 4)))
        (br $lp_sum)))
    (local.get $sum))

  (func (export "main") (result i32)
    (local $primes i32)
    (local $sum i32)
    (local.set $primes (call $sieve_count (i32.const 1000)))
    (local.set $sum (call $array_sum (i32.const 1000)))
    (call $trace_i32 (mem.const_str $l1) (local.get $primes))
    (call $trace_i32 (mem.const_str $l2) (local.get $sum))
    (i32.add (local.get $primes) (local.get $sum)))
)

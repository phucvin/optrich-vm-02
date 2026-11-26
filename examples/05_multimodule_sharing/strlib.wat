;; Example 05 (Module 1/2) — Zero-Copy String Library (`strlib`)
;; Exports functions operating directly on caller-owned `ro_handle` views with zero copying.
(module
  ;; Count occurrences of byte `ch` in read-only span `s`
  (func (export "count_byte") (param $s ro_handle) (param $ch i32) (result i32)
    (local $i i32)
    (local $n i32)
    (local $cnt i32)
    (local.set $n (mem.byte_len (local.get $s)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.eq (mem.read_u8 (local.get $s) (local.get $i)) (local.get $ch))
          (then (local.set $cnt (i32.add (local.get $cnt) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $cnt))

  ;; Return 1 if `s` starts with `prefix`, else 0 (uses bulk native `mem.cmp`)
  (func (export "starts_with") (param $s ro_handle) (param $prefix ro_handle) (result i32)
    (local $plen i32)
    (local.set $plen (mem.byte_len (local.get $prefix)))
    (if (i32.lt_u (mem.byte_len (local.get $s)) (local.get $plen))
      (then (return (i32.const 0))))
    (i32.eqz (mem.cmp (local.get $s) (i32.const 0)
                      (local.get $prefix) (i32.const 0)
                      (local.get $plen))))
)

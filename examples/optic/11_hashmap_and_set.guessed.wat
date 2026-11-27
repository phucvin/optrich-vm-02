;; Guessed WAT for examples/optic/11_hashmap_and_set.opc
;; Monomorphized HashMap<str, i32>:
;;   header: mem.alloc(8, 3) -> bytes[0..4)=len, bytes[4..8)=cap, refs[0]=states([]u8), refs[1]=keys([]str), refs[2]=vals([]i32)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (import "host" "trace_gc"  (func $trace_gc  (param ro_handle)))

  (string $l1  "step1.map_len")
  (string $l2  "step2.map_cap")
  (string $l3  "step3.get_beta")
  (string $l4  "step4.get_missing")
  (string $l5  "step5.removed_gamma")
  (string $l6  "step6.get_gamma_after")
  (string $l7  "step7.get_delta_after")
  (string $l8  "step8.set_len")
  (string $l9  "step9.set_has_green")
  (string $l10 "step10.gc_after_rehash")

  (string $s_alpha "alpha")
  (string $s_beta  "beta")
  (string $s_gamma "gamma")
  (string $s_delta "delta")
  (string $s_omega "omega")
  (string $s_red   "red")
  (string $s_green "green")

  ;; --- str impl Hash & Eq ---
  (func $str__hash (param $s handle) (result i32)
    (local $h i32)
    (local $i i32)
    (local $end i32)
    (local.set $h (i32.const -2128831035))
    (local.set $end (mem.byte_len (local.get $s)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $h (i32.mul (i32.xor (local.get $h) (mem.read_u8 (local.get $s) (local.get $i)))
                               (i32.const 16777619)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $h))

  (func $str__eq (param $a handle) (param $b handle) (result i32)
    (local $la i32)
    (local.set $la (mem.byte_len (local.get $a)))
    (if (i32.ne (local.get $la) (mem.byte_len (local.get $b)))
      (then (return (i32.const 0))))
    (i32.eqz (mem.cmp (local.get $a) (i32.const 0) (local.get $b) (i32.const 0) (local.get $la))))

  ;; --- Monomorphized HashMap<str, i32> ---
  (func $HashMap_str_i32__new (param $cap i32) (result rw_handle)
    (local $m rw_handle)
    (local.set $m (mem.alloc (i32.const 8) (i32.const 3)))
    (mem.write_i32 (local.get $m) (i32.const 0) (i32.const 0))
    (mem.write_i32 (local.get $m) (i32.const 4) (local.get $cap))
    (mem.write_ref (local.get $m) (i32.const 0) (mem.alloc (local.get $cap) (i32.const 0)))
    (mem.write_ref (local.get $m) (i32.const 1) (mem.alloc (i32.const 0) (local.get $cap)))
    (mem.write_ref (local.get $m) (i32.const 2) (mem.alloc (i32.shl (local.get $cap) (i32.const 2)) (i32.const 0)))
    (local.get $m))

  (func $HashMap_str_i32__insert_raw
    (param $states handle) (param $keys handle) (param $vals handle) (param $cap i32)
    (param $k handle) (param $v i32) (result i32)
    (local $mask i32)
    (local $idx i32)
    (local $st i32)
    (local $first_tomb i32)
    (local.set $mask (i32.sub (local.get $cap) (i32.const 1)))
    (local.set $idx (i32.and (call $str__hash (local.get $k)) (local.get $mask)))
    (local.set $first_tomb (i32.const -1))
    (block $done
      (loop $probe
        (local.set $st (mem.read_u8 (local.get $states) (local.get $idx)))
        (if (i32.eqz (local.get $st))
          (then
            (if (i32.ge_s (local.get $first_tomb) (i32.const 0))
              (then (local.set $idx (local.get $first_tomb))))
            (mem.write_u8  (local.get $states) (local.get $idx) (i32.const 1))
            (mem.write_ref (local.get $keys)   (local.get $idx) (local.get $k))
            (mem.write_i32 (local.get $vals)   (i32.shl (local.get $idx) (i32.const 2)) (local.get $v))
            (return (i32.const 1)))) ;; inserted new
        (if (i32.eq (local.get $st) (i32.const 1))
          (then
            (if (call $str__eq (mem.read_ref (local.get $keys) (local.get $idx)) (local.get $k))
              (then
                (mem.write_i32 (local.get $vals) (i32.shl (local.get $idx) (i32.const 2)) (local.get $v))
                (return (i32.const 0)))))) ;; updated existing
        (if (i32.eq (local.get $st) (i32.const 2))
          (then
            (if (i32.lt_s (local.get $first_tomb) (i32.const 0))
              (then (local.set $first_tomb (local.get $idx))))))
        (local.set $idx (i32.and (i32.add (local.get $idx) (i32.const 1)) (local.get $mask)))
        (br $probe)))
    (i32.const 0))

  (func $HashMap_str_i32__rehash (param $m rw_handle)
    (local $old_cap i32)
    (local $new_cap i32)
    (local $old_st handle)
    (local $old_k handle)
    (local $old_v handle)
    (local $new_st rw_handle)
    (local $new_k rw_handle)
    (local $new_v rw_handle)
    (local $i i32)

    (local.set $old_cap (mem.read_i32 (local.get $m) (i32.const 4)))
    (local.set $new_cap (i32.shl (local.get $old_cap) (i32.const 1)))
    (local.set $old_st (mem.read_ref (local.get $m) (i32.const 0)))
    (local.set $old_k  (mem.read_ref (local.get $m) (i32.const 1)))
    (local.set $old_v  (mem.read_ref (local.get $m) (i32.const 2)))

    (local.set $new_st (mem.alloc (local.get $new_cap) (i32.const 0)))
    (local.set $new_k  (mem.alloc (i32.const 0) (local.get $new_cap)))
    (local.set $new_v  (mem.alloc (i32.shl (local.get $new_cap) (i32.const 2)) (i32.const 0)))

    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $old_cap)))
        (if (i32.eq (mem.read_u8 (local.get $old_st) (local.get $i)) (i32.const 1))
          (then
            (drop (call $HashMap_str_i32__insert_raw
                    (local.get $new_st) (local.get $new_k) (local.get $new_v) (local.get $new_cap)
                    (mem.read_ref (local.get $old_k) (local.get $i))
                    (mem.read_i32 (local.get $old_v) (i32.shl (local.get $i) (i32.const 2)))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))

    (mem.write_i32 (local.get $m) (i32.const 4) (local.get $new_cap))
    (mem.write_ref (local.get $m) (i32.const 0) (local.get $new_st))
    (mem.write_ref (local.get $m) (i32.const 1) (local.get $new_k))
    (mem.write_ref (local.get $m) (i32.const 2) (local.get $new_v)))

  (func $HashMap_str_i32__insert (param $m rw_handle) (param $k handle) (param $v i32)
    (local $len i32)
    (local $cap i32)
    (local.set $len (mem.read_i32 (local.get $m) (i32.const 0)))
    (local.set $cap (mem.read_i32 (local.get $m) (i32.const 4)))
    (if (i32.gt_s (i32.shl (local.get $len) (i32.const 1)) (local.get $cap))
      (then
        (call $HashMap_str_i32__rehash (local.get $m))
        (local.set $cap (mem.read_i32 (local.get $m) (i32.const 4)))))
    (if (call $HashMap_str_i32__insert_raw
          (mem.read_ref (local.get $m) (i32.const 0))
          (mem.read_ref (local.get $m) (i32.const 1))
          (mem.read_ref (local.get $m) (i32.const 2))
          (local.get $cap) (local.get $k) (local.get $v))
      (then
        (mem.write_i32 (local.get $m) (i32.const 0) (i32.add (local.get $len) (i32.const 1))))))

  ;; Returns unboxed Option<i32> = (is_some: i32, val: i32)
  (func $HashMap_str_i32__get (param $m handle) (param $k handle) (result i32 i32)
    (local $cap i32)
    (local $mask i32)
    (local $idx i32)
    (local $st i32)
    (local $states handle)
    (local $keys handle)
    (local $vals handle)
    (local.set $cap (mem.read_i32 (local.get $m) (i32.const 4)))
    (local.set $mask (i32.sub (local.get $cap) (i32.const 1)))
    (local.set $states (mem.read_ref (local.get $m) (i32.const 0)))
    (local.set $keys   (mem.read_ref (local.get $m) (i32.const 1)))
    (local.set $vals   (mem.read_ref (local.get $m) (i32.const 2)))
    (local.set $idx (i32.and (call $str__hash (local.get $k)) (local.get $mask)))
    (block $done
      (loop $probe
        (local.set $st (mem.read_u8 (local.get $states) (local.get $idx)))
        (if (i32.eqz (local.get $st))
          (then (return (i32.const 0) (i32.const 0))))
        (if (i32.eq (local.get $st) (i32.const 1))
          (then
            (if (call $str__eq (mem.read_ref (local.get $keys) (local.get $idx)) (local.get $k))
              (then
                (return (i32.const 1)
                        (mem.read_i32 (local.get $vals) (i32.shl (local.get $idx) (i32.const 2))))))))
        (local.set $idx (i32.and (i32.add (local.get $idx) (i32.const 1)) (local.get $mask)))
        (br $probe)))
    (i32.const 0) (i32.const 0))

  (func $HashMap_str_i32__remove (param $m rw_handle) (param $k handle) (result i32)
    (local $cap i32)
    (local $mask i32)
    (local $idx i32)
    (local $st i32)
    (local $states handle)
    (local $keys handle)
    (local.set $cap (mem.read_i32 (local.get $m) (i32.const 4)))
    (local.set $mask (i32.sub (local.get $cap) (i32.const 1)))
    (local.set $states (mem.read_ref (local.get $m) (i32.const 0)))
    (local.set $keys   (mem.read_ref (local.get $m) (i32.const 1)))
    (local.set $idx (i32.and (call $str__hash (local.get $k)) (local.get $mask)))
    (block $done
      (loop $probe
        (local.set $st (mem.read_u8 (local.get $states) (local.get $idx)))
        (if (i32.eqz (local.get $st))
          (then (return (i32.const 0))))
        (if (i32.eq (local.get $st) (i32.const 1))
          (then
            (if (call $str__eq (mem.read_ref (local.get $keys) (local.get $idx)) (local.get $k))
              (then
                (mem.write_u8  (local.get $states) (local.get $idx) (i32.const 2))
                (mem.write_ref (local.get $keys)   (local.get $idx) (handle.null))
                (mem.write_i32 (local.get $m) (i32.const 0)
                  (i32.sub (mem.read_i32 (local.get $m) (i32.const 0)) (i32.const 1)))
                (return (i32.const 1))))))
        (local.set $idx (i32.and (i32.add (local.get $idx) (i32.const 1)) (local.get $mask)))
        (br $probe)))
    (i32.const 0))

  (func $unwrap_or (param $ok i32) (param $val i32) (param $def i32) (result i32)
    (if (result i32) (local.get $ok)
      (then (local.get $val))
      (else (local.get $def))))

  (func (export "main") (result i32)
    (local $m rw_handle)
    (local $s rw_handle)
    (local $ok i32)
    (local $v i32)
    (local $va i32)
    (local $vb i32)
    (local $vd i32)

    (local.set $m (call $HashMap_str_i32__new (i32.const 4)))
    (call $HashMap_str_i32__insert (local.get $m) (mem.const_str $s_alpha) (i32.const 10))
    (call $HashMap_str_i32__insert (local.get $m) (mem.const_str $s_beta)  (i32.const 20))
    (call $HashMap_str_i32__insert (local.get $m) (mem.const_str $s_gamma) (i32.const 30))
    (call $HashMap_str_i32__insert (local.get $m) (mem.const_str $s_delta) (i32.const 40))
    (call $HashMap_str_i32__insert (local.get $m) (mem.const_str $s_beta)  (i32.const 25))

    (call $trace_i32 (mem.const_str $l1) (mem.read_i32 (local.get $m) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l2) (mem.read_i32 (local.get $m) (i32.const 4)))

    (call $HashMap_str_i32__get (local.get $m) (mem.const_str $s_beta))
    (local.set $v) (local.set $ok)
    (local.set $vb (call $unwrap_or (local.get $ok) (local.get $v) (i32.const -1)))
    (call $trace_i32 (mem.const_str $l3) (local.get $vb))

    (call $HashMap_str_i32__get (local.get $m) (mem.const_str $s_omega))
    (local.set $v) (local.set $ok)
    (call $trace_i32 (mem.const_str $l4) (call $unwrap_or (local.get $ok) (local.get $v) (i32.const -1)))

    (call $trace_i32 (mem.const_str $l5) (call $HashMap_str_i32__remove (local.get $m) (mem.const_str $s_gamma)))

    (call $HashMap_str_i32__get (local.get $m) (mem.const_str $s_gamma))
    (local.set $v) (local.set $ok)
    (call $trace_i32 (mem.const_str $l6) (call $unwrap_or (local.get $ok) (local.get $v) (i32.const -1)))

    (call $HashMap_str_i32__get (local.get $m) (mem.const_str $s_delta))
    (local.set $v) (local.set $ok)
    (local.set $vd (call $unwrap_or (local.get $ok) (local.get $v) (i32.const -1)))
    (call $trace_i32 (mem.const_str $l7) (local.get $vd))

    ;; Set<str> backed by HashMap<str, i32>
    (local.set $s (call $HashMap_str_i32__new (i32.const 4)))
    (call $HashMap_str_i32__insert (local.get $s) (mem.const_str $s_red)   (i32.const 1))
    (call $HashMap_str_i32__insert (local.get $s) (mem.const_str $s_green) (i32.const 1))
    (call $HashMap_str_i32__insert (local.get $s) (mem.const_str $s_red)   (i32.const 1))
    (call $trace_i32 (mem.const_str $l8) (mem.read_i32 (local.get $s) (i32.const 0)))
    (call $HashMap_str_i32__get (local.get $s) (mem.const_str $s_green))
    (local.set $v) (local.set $ok)
    (call $trace_i32 (mem.const_str $l9) (local.get $ok))

    ;; Live: 17 const strings + m(1+3=4) + s(1+3=4) = 25
    ;; Swept: 3 old cap=4 backing arrays (states, keys, vals) from m's rehash!
    (call $trace_gc (mem.const_str $l10))

    (call $HashMap_str_i32__get (local.get $m) (mem.const_str $s_alpha))
    (local.set $v) (local.set $ok)
    (local.set $va (call $unwrap_or (local.get $ok) (local.get $v) (i32.const 0)))
    (i32.add (i32.add (local.get $va) (local.get $vb)) (local.get $vd)))
)

;; Guessed WAT for examples/optic/03b_prim_vec_spans_and_cycles.opc
;; Vec<i32> layout:
;;   header: mem.alloc(8, 1) -> bytes[0..4)=len, bytes[4..8)=cap, refs[0]=mem.alloc(cap*4, 0)
;; CycleNode: mem.alloc(4, 1) -> bytes[0..4)=id, refs[0]=next
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))
  (import "host" "trace_gc"   (func $trace_gc   (param ro_handle)))

  (string $l1 "step1.vec_len")
  (string $l2 "step2.vec_pop")
  (string $l3 "step3.vec_len_after_pop")
  (string $l4 "step4.buf3_aliased")
  (string $l5 "step5.s2_meta")
  (string $l6 "step6.gc_sweeps_cycle")

  ;; --- Compiler-synthesized Vec<i32> helpers ---
  (func $Vec_i32__new (param $cap i32) (result rw_handle)
    (local $v rw_handle)
    (local $buf rw_handle)
    (local.set $v (mem.alloc (i32.const 8) (i32.const 1)))
    (local.set $buf (mem.alloc (i32.shl (local.get $cap) (i32.const 2)) (i32.const 0)))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.const 0))
    (mem.write_i32 (local.get $v) (i32.const 4) (local.get $cap))
    (mem.write_ref (local.get $v) (i32.const 0) (local.get $buf))
    (local.get $v))

  (func $Vec_i32__push (param $v rw_handle) (param $val i32)
    (local $len i32)
    (local $cap i32)
    (local $buf rw_handle)
    (local $new_cap i32)
    (local $new_buf rw_handle)

    (local.set $len (mem.read_i32 (local.get $v) (i32.const 0)))
    (local.set $cap (mem.read_i32 (local.get $v) (i32.const 4)))
    (local.set $buf (mem.read_ref_rw (local.get $v) (i32.const 0)))

    (if (i32.ge_s (local.get $len) (local.get $cap))
      (then
        (local.set $new_cap
          (if (result i32) (i32.gt_s (local.get $cap) (i32.const 0))
            (then (i32.mul (local.get $cap) (i32.const 2)))
            (else (i32.const 4))))
        (local.set $new_buf (mem.alloc (i32.shl (local.get $new_cap) (i32.const 2)) (i32.const 0)))
        (mem.copy (local.get $new_buf) (i32.const 0)
                  (local.get $buf)     (i32.const 0)
                  (i32.shl (local.get $len) (i32.const 2)))
        (mem.write_i32 (local.get $v) (i32.const 4) (local.get $new_cap))
        (mem.write_ref (local.get $v) (i32.const 0) (local.get $new_buf))
        (local.set $buf (local.get $new_buf))))

    (mem.write_i32 (local.get $buf) (i32.shl (local.get $len) (i32.const 2)) (local.get $val))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.add (local.get $len) (i32.const 1))))

  (func $Vec_i32__pop (param $v rw_handle) (result i32)
    (local $len i32)
    (local.set $len (mem.read_i32 (local.get $v) (i32.const 0)))
    (if (i32.le_s (local.get $len) (i32.const 0))
      (then (unreachable)))
    (local.set $len (i32.sub (local.get $len) (i32.const 1)))
    (mem.write_i32 (local.get $v) (i32.const 0) (local.get $len))
    (mem.read_i32 (mem.read_ref_ro (local.get $v) (i32.const 0))
                  (i32.shl (local.get $len) (i32.const 2))))

  (func $Vec_i32__len (param $v ro_handle) (result i32)
    (mem.read_i32 (local.get $v) (i32.const 0)))

  (func $Vec_i32__get (param $v ro_handle) (param $idx i32) (result i32)
    (if (i32.ge_u (local.get $idx) (mem.read_i32 (local.get $v) (i32.const 0)))
      (then (unreachable)))
    (mem.read_i32 (mem.read_ref_ro (local.get $v) (i32.const 0))
                  (i32.shl (local.get $idx) (i32.const 2))))

  ;; --- User functions ---
  (func $make_and_drop_cycle
    (local $a rw_handle)
    (local $b rw_handle)
    (local.set $a (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $a) (i32.const 0) (i32.const 1))
    (local.set $b (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $b) (i32.const 0) (i32.const 2))
    (mem.write_ref (local.get $a) (i32.const 0) (local.get $b))
    (mem.write_ref (local.get $b) (i32.const 0) (local.get $a)))

  (func (export "main") (result i32)
    (local $nums rw_handle)
    (local $i i32)
    (local $popped i32)
    (local $buf rw_handle)
    (local $s1 rw_handle)
    (local $s2 rw_handle)

    (local.set $nums (call $Vec_i32__new (i32.const 2)))
    (local.set $i (i32.const 1))
    (block $b1
      (loop $l1_lp
        (br_if $b1 (i32.ge_s (local.get $i) (i32.const 6)))
        (call $Vec_i32__push (local.get $nums) (i32.mul (local.get $i) (i32.const 10)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l1_lp)))

    (call $trace_i32 (mem.const_str $l1) (call $Vec_i32__len (local.get $nums)))
    (local.set $popped (call $Vec_i32__pop (local.get $nums)))
    (call $trace_i32 (mem.const_str $l2) (local.get $popped))
    (call $trace_i32 (mem.const_str $l3) (call $Vec_i32__len (local.get $nums)))

    (local.set $buf (mem.alloc (i32.const 32) (i32.const 0)))
    (local.set $i (i32.const 0))
    (block $b2
      (loop $l2_lp
        (br_if $b2 (i32.ge_s (local.get $i) (i32.const 8)))
        (mem.write_i32 (local.get $buf) (i32.shl (local.get $i) (i32.const 2)) (local.get $i))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l2_lp)))

    (local.set $s1 (mem.span (local.get $buf) (i32.const 8) (i32.const 20)))
    (local.set $s2 (mem.span (local.get $s1)  (i32.const 4) (i32.const 12)))
    (mem.write_i32 (local.get $s2) (i32.const 0) (i32.const 99))

    (call $trace_i32 (mem.const_str $l4) (mem.read_i32 (local.get $buf) (i32.const 12)))
    (call $trace_meta (mem.const_str $l5) (local.get $s2))

    (call $make_and_drop_cycle)
    ;; Live: 6 strings + nums header + nums backing(cap 8) + buf + s1 + s2 = 11
    ;; Swept: 2 old Vec backing buffers (cap 2, cap 4) + 2 cyclic CycleNodes = 4!
    (call $trace_gc (mem.const_str $l6))

    (i32.add
      (i32.add (call $Vec_i32__get (local.get $nums) (i32.const 0))
               (call $Vec_i32__get (local.get $nums) (i32.const 3)))
      (i32.add (local.get $popped)
               (mem.read_i32 (local.get $buf) (i32.const 12)))))
)

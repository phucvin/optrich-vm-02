;; Guessed WAT for examples/optic/12_capstone_kv_parser/main.opc
(module
  (import "host"   "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host"   "trace_str"  (func $trace_str  (param ro_handle ro_handle)))
  (import "host"   "trace_meta" (func $trace_meta (param ro_handle ro_handle)))
  (import "lexer"  "tokenize"   (func $tokenize   (param ro_handle) (result rw_handle)))
  (import "parser" "parse_kv"   (func $parse_kv   (param ro_handle) (result rw_handle)))

  (string $l1 "step1.tok_count")
  (string $l2 "step2.entry_count")
  (string $l3 "step3.key0")
  (string $l4 "step4.val0")
  (string $l5 "step5.key1")
  (string $l6 "step6.key1_meta")
  (string $l7 "step7.key2")
  (string $l8 "step8.sum_values")
  (string $s_cfg "width=640; height=480; fps=60;")

  (func (export "main") (result i32)
    (local $toks rw_handle)
    (local $entries rw_handle)
    (local $ebuf ro_handle)
    (local $e ro_handle)
    (local $i i32)
    (local $n i32)
    (local $total i32)

    (local.set $toks (call $tokenize (mem.const_str $s_cfg)))
    (call $trace_i32 (mem.const_str $l1) (mem.read_i32 (local.get $toks) (i32.const 0)))

    (local.set $entries (call $parse_kv (local.get $toks)))
    (local.set $n (mem.read_i32 (local.get $entries) (i32.const 0)))
    (local.set $ebuf (mem.read_ref_ro (local.get $entries) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l2) (local.get $n))

    (local.set $e (mem.read_ref_ro (local.get $ebuf) (i32.const 0)))
    (call $trace_str (mem.const_str $l3) (mem.read_ref_ro (local.get $e) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l4) (mem.read_i32 (local.get $e) (i32.const 0)))

    (local.set $e (mem.read_ref_ro (local.get $ebuf) (i32.const 1)))
    (call $trace_str  (mem.const_str $l5) (mem.read_ref_ro (local.get $e) (i32.const 0)))
    (call $trace_meta (mem.const_str $l6) (mem.read_ref_ro (local.get $e) (i32.const 0)))

    (local.set $e (mem.read_ref_ro (local.get $ebuf) (i32.const 2)))
    (call $trace_str (mem.const_str $l7) (mem.read_ref_ro (local.get $e) (i32.const 0)))

    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $total
          (i32.add (local.get $total)
                   (mem.read_i32 (mem.read_ref_ro (local.get $ebuf) (local.get $i)) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (call $trace_i32 (mem.const_str $l8) (local.get $total))
    (local.get $total))
)

;; Guessed WAT for examples/optic/03_slices_and_vec.opc
;; Vec<str> layout:
;;   header: mem.alloc(8, 1)
;;     bytes[0..4) : len (i32)
;;     bytes[4..8) : cap (i32)
;;     refs[0]     : backing ref array (mem.alloc(0, cap))
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_str"  (func $trace_str  (param ro_handle ro_handle)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))
  (import "host" "trace_gc"   (func $trace_gc   (param ro_handle)))

  (string $s_csv "alpha,beta,gamma,delta")
  (string $l1 "step1.count")
  (string $l2 "step2.tok0")
  (string $l3 "step3.tok1")
  (string $l4 "step4.tok2")
  (string $l5 "step5.tok3")
  (string $l6 "step6.tok2_meta")
  (string $l7 "step7.header")
  (string $l8 "step8.header_meta")
  (string $l9 "step9.gc")

  ;; --- Compiler-synthesized Vec<ref> helpers ---
  (func $Vec_ref__new (param $cap i32) (result rw_handle)
    (local $v rw_handle)
    (local $buf rw_handle)
    (local.set $v (mem.alloc (i32.const 8) (i32.const 1)))
    (local.set $buf (mem.alloc (i32.const 0) (local.get $cap)))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.const 0))
    (mem.write_i32 (local.get $v) (i32.const 4) (local.get $cap))
    (mem.write_ref (local.get $v) (i32.const 0) (local.get $buf))
    (local.get $v))

  (func $Vec_ref__push (param $v rw_handle) (param $item ro_handle)
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
        (local.set $new_buf (mem.alloc (i32.const 0) (local.get $new_cap)))
        (mem.copy_refs (local.get $new_buf) (i32.const 0) (local.get $buf) (i32.const 0) (local.get $len))
        (mem.write_i32 (local.get $v) (i32.const 4) (local.get $new_cap))
        (mem.write_ref (local.get $v) (i32.const 0) (local.get $new_buf))
        (local.set $buf (local.get $new_buf))))

    (mem.write_ref (local.get $buf) (local.get $len) (local.get $item))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.add (local.get $len) (i32.const 1))))

  (func $Vec_ref__len (param $v ro_handle) (result i32)
    (mem.read_i32 (local.get $v) (i32.const 0)))

  (func $Vec_ref__get (param $v ro_handle) (param $idx i32) (result ro_handle)
    ;; Bounds check against logical len (not just backing cap)
    (if (i32.ge_u (local.get $idx) (mem.read_i32 (local.get $v) (i32.const 0)))
      (then (unreachable)))
    (mem.read_ref_ro (mem.read_ref_ro (local.get $v) (i32.const 0)) (local.get $idx)))

  ;; --- User functions ---
  (func $split_byte (param $input ro_handle) (param $sep i32) (result rw_handle)
    (local $out rw_handle)
    (local $start i32)
    (local $i i32)
    (local $end i32)

    (local.set $out (call $Vec_ref__new (i32.const 2)))
    (local.set $start (i32.const 0))
    (local.set $i (i32.const 0))
    (local.set $end (mem.byte_len (local.get $input)))

    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (if (i32.eq (mem.read_u8 (local.get $input) (local.get $i)) (local.get $sep))
          (then
            (call $Vec_ref__push (local.get $out)
              (mem.span (local.get $input) (local.get $start)
                        (i32.sub (local.get $i) (local.get $start))))
            (local.set $start (i32.add (local.get $i) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))

    (call $Vec_ref__push (local.get $out)
      (mem.span (local.get $input) (local.get $start)
                (i32.sub (local.get $end) (local.get $start))))
    (local.get $out))

  (func $make_detached_header (result ro_handle)
    (local $huge rw_handle)
    (local $view ro_handle)
    (local.set $huge (mem.alloc (i32.const 4096) (i32.const 0)))
    (mem.write_u8 (local.get $huge) (i32.const 0) (i32.const 79)) ;; 'O'
    (mem.write_u8 (local.get $huge) (i32.const 1) (i32.const 80)) ;; 'P'
    (mem.write_u8 (local.get $huge) (i32.const 2) (i32.const 84)) ;; 'T'
    (mem.write_u8 (local.get $huge) (i32.const 3) (i32.const 73)) ;; 'I'
    (mem.write_u8 (local.get $huge) (i32.const 4) (i32.const 67)) ;; 'C'
    (local.set $view (mem.readonly (mem.span (local.get $huge) (i32.const 0) (i32.const 5))))
    (mem.clone_span (local.get $view)))

  (func (export "main") (result i32)
    (local $parts rw_handle)
    (local $hdr ro_handle)

    (local.set $parts (call $split_byte (mem.const_str $s_csv) (i32.const 44)))
    (call $trace_i32 (mem.const_str $l1) (call $Vec_ref__len (local.get $parts)))
    (call $trace_str (mem.const_str $l2) (call $Vec_ref__get (local.get $parts) (i32.const 0)))
    (call $trace_str (mem.const_str $l3) (call $Vec_ref__get (local.get $parts) (i32.const 1)))
    (call $trace_str (mem.const_str $l4) (call $Vec_ref__get (local.get $parts) (i32.const 2)))
    (call $trace_str (mem.const_str $l5) (call $Vec_ref__get (local.get $parts) (i32.const 3)))
    (call $trace_meta (mem.const_str $l6) (call $Vec_ref__get (local.get $parts) (i32.const 2)))

    (local.set $hdr (call $make_detached_header))
    (call $trace_str (mem.const_str $l7) (local.get $hdr))
    (call $trace_meta (mem.const_str $l8) (local.get $hdr))

    (call $trace_gc (mem.const_str $l9))

    (i32.add
      (i32.mul (call $Vec_ref__len (local.get $parts)) (i32.const 10))
      (mem.byte_len (local.get $hdr))))
)

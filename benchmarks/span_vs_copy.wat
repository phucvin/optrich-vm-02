;; Benchmark: Zero-Copy Span Passing vs. Copy-Based Buffer Passing
;; Compares creating read-only zero-copy spans (`mem.span` + `mem.readonly`)
;; against allocating + copying (`mem.alloc` + `mem.copy`) for a 64 KB buffer.
;;
;; Run: ./build/optrich-vm benchmarks/span_vs_copy.wat zero_copy_pass 65536 20000
;;      ./build/optrich-vm benchmarks/span_vs_copy.wat copy_based_pass 65536 20000
(module
  ;; Callee inspects first, middle, and last byte of a read-only buffer
  (func $inspect (param $view ro_handle) (result i32)
    (local $len i32)
    (local.set $len (mem.byte_len (local.get $view)))
    (i32.add
      (mem.read_u8 (local.get $view) (i32.const 0))
      (i32.add
        (mem.read_u8 (local.get $view) (i32.shr_u (local.get $len) (i32.const 1)))
        (mem.read_u8 (local.get $view) (i32.sub (local.get $len) (i32.const 1))))))

  ;; Zero-copy: slice & downgrade to ro_handle in O(1) without copying bytes
  (func (export "zero_copy_pass") (param $buf_size i32) (param $iters i32) (result i32)
    (local $buf rw_handle)
    (local $ro ro_handle)
    (local $i i32)
    (local $acc i32)
    (local $slice_len i32)

    (local.set $buf (mem.alloc (local.get $buf_size) (i32.const 0)))
    (mem.fill (local.get $buf) (i32.const 0) (i32.const 7) (local.get $buf_size))
    (local.set $slice_len (i32.sub (local.get $buf_size) (i32.const 64)))

    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $iters)))
        (local.set $ro
          (mem.readonly (mem.span (local.get $buf) (i32.const 32) (local.get $slice_len))))
        (local.set $acc (i32.add (local.get $acc) (call $inspect (local.get $ro))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))

    (local.get $acc))

  ;; Copy-based: allocate a fresh buffer and mem.copy the slice on every call
  (func (export "copy_based_pass") (param $buf_size i32) (param $iters i32) (result i32)
    (local $buf rw_handle)
    (local $tmp rw_handle)
    (local $i i32)
    (local $acc i32)
    (local $slice_len i32)

    (local.set $buf (mem.alloc (local.get $buf_size) (i32.const 0)))
    (mem.fill (local.get $buf) (i32.const 0) (i32.const 7) (local.get $buf_size))
    (local.set $slice_len (i32.sub (local.get $buf_size) (i32.const 64)))

    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $iters)))
        (local.set $tmp (mem.alloc (local.get $slice_len) (i32.const 0)))
        (mem.copy (local.get $tmp) (i32.const 0) (local.get $buf) (i32.const 32) (local.get $slice_len))
        (local.set $acc (i32.add (local.get $acc) (call $inspect (mem.readonly (local.get $tmp)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))

    (local.get $acc))
)

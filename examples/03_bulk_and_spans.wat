;; Example 03 — Zero-Copy Spans, Nested Views, Read-Only Capabilities & Bulk Ops (Phase 2)
;; Showcases: mem.fill, mem.copy (including overlapping memmove), mem.cmp,
;;            nested mem.span (flattened rootHandle), mem.readonly, mem.is_readonly.
;;
;; Run: ./build/optrich-vm examples/03_bulk_and_spans.wat main
(module
  ;; Reads a read-only view (writes to `ro_handle` are rejected at compile time)
  (func $sum_view (param $v ro_handle) (result i32)
    (local $i i32)
    (local $n i32)
    (local $sum i32)
    (local.set $n (mem.byte_len (local.get $v)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $sum (i32.add (local.get $sum) (mem.read_u8 (local.get $v) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $sum))

  (func (export "main") (result i32)
    (local $buf rw_handle)
    (local $s1 rw_handle)
    (local $s2 rw_handle)
    (local $ro ro_handle)

    ;; Allocate 16-byte buffer and fill first half with 10, second half with 20
    (local.set $buf (mem.alloc (i32.const 16) (i32.const 0)))
    (mem.fill (local.get $buf) (i32.const 0) (i32.const 10) (i32.const 8))
    (mem.fill (local.get $buf) (i32.const 8) (i32.const 20) (i32.const 8))

    ;; Overlapping copy: shift buf[4..12) into buf[6..14)
    (mem.copy (local.get $buf) (i32.const 6) (local.get $buf) (i32.const 4) (i32.const 8))

    ;; Nested zero-copy span: s1 = buf[4..12), s2 = s1[2..6) == buf[6..10)
    (local.set $s1 (mem.span (local.get $buf) (i32.const 4) (i32.const 8)))
    (local.set $s2 (mem.span (local.get $s1) (i32.const 2) (i32.const 4)))

    ;; Mutate through nested span s2 (writes directly to root backing buffer!)
    (mem.fill (local.get $s2) (i32.const 0) (i32.const 50) (i32.const 4))

    ;; Downgrade s1 to read-only before passing to $sum_view
    ;; s1 bytes are now: [10, 10, 50, 50, 50, 50, 20, 20] => sum = 260
    (local.set $ro (mem.readonly (local.get $s1)))
    (if (i32.eqz (mem.is_readonly (local.get $ro))) (then (unreachable)))
    (call $sum_view (local.get $ro)))
)

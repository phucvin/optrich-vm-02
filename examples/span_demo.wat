;; Optrich VM 02 — Zero-Copy Split-Block & Read-Only Span Demo
;; Run: ./build/optrich-vm examples/span_demo.wat main
(module
  (string $title "Optrich VM 02: Zero-Copy Spans!")

  ;; Computes a simple byte checksum over a statically read-only handle.
  (func $checksum (param $view ro_handle) (result i32)
    (local $i i32)
    (local $n i32)
    (local $sum i32)
    (local.set $n (mem.byte_len (local.get $view)))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $sum
          (i32.add (local.get $sum) (mem.read_u8 (local.get $view) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $loop)))
    (local.get $sum))

  (func (export "main") (result i32)
    (local $s ro_handle)
    (local $slice ro_handle)
    (local.set $s (mem.const_str $title))
    ;; Zero-copy sub-span "Zero-Copy" (offset 15, len 9)
    (local.set $slice (mem.span (local.get $s) (i32.const 15) (i32.const 9)))
    (call $checksum (local.get $slice)))
)

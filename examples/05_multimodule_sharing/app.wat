;; Example 05 (Module 2/2) — Multi-Module Application (`app`)
;; Imports `strlib` directly (no host-wrapper boxing, zero-copy `ro_handle` passing).
;;
;; Run:
;;   ./build/optrich-vm --link strlib=examples/05_multimodule_sharing/strlib.wat \
;;                      examples/05_multimodule_sharing/app.wat main
(module
  (import "strlib" "count_byte"  (func $count_byte  (param ro_handle i32) (result i32)))
  (import "strlib" "starts_with" (func $starts_with (param ro_handle ro_handle) (result i32)))

  (string $doc    "optrich-vm-02: zero-copy capability spans")
  (string $prefix "zero-copy")

  (func (export "main") (result i32)
    (local $slice ro_handle)
    ;; Zero-copy slice starting at "zero-copy..." (offset 15, len 26)
    (local.set $slice (mem.span (mem.const_str $doc) (i32.const 15) (i32.const 26)))

    ;; Verify prefix match (1) and count 'o' (ASCII 111) in slice -> 2 ('zero', 'copy')
    ;; Result: 1 * 100 + 2 = 102
    (i32.add
      (i32.mul (call $starts_with (local.get $slice) (mem.const_str $prefix)) (i32.const 100))
      (call $count_byte (local.get $slice) (i32.const 111))))
)

;; Example 04 — Precise Mark-and-Sweep GC & mem.clone_span (Phase 3)
;; Showcases:
;;   1. Automatic GC reclaiming temporary reference cycles (a <-> b) mid-loop
;;   2. mem.clone_span detaching a small 4-byte slice from a 4 KiB buffer so GC
;;      can collect the 4 KiB backing buffer while keeping the detached slice alive
;;
;; Run with --gc-threshold 32 --gc-stats to watch live blocks stay tiny:
;;   ./build/optrich-vm --gc-threshold 32 --gc-stats examples/04_gc_and_clone_span.wat main
(module
  ;; Creates an unreachable 2-node reference cycle to prove precise cycle collection
  (func $make_cycle
    (local $a rw_handle)
    (local $b rw_handle)
    (local.set $a (mem.alloc (i32.const 16) (i32.const 1)))
    (local.set $b (mem.alloc (i32.const 16) (i32.const 1)))
    (mem.write_ref (local.get $a) (i32.const 0) (local.get $b))
    (mem.write_ref (local.get $b) (i32.const 0) (local.get $a)))

  ;; Allocates a 4 KiB buffer, writes `val` at offset 1024, slices 4 bytes, and
  ;; detaches with `mem.clone_span` so the 4 KiB buffer becomes immediately collectable.
  (func $extract_detached (param $val i32) (result ro_handle)
    (local $big rw_handle)
    (local $slice ro_handle)
    (local.set $big (mem.alloc (i32.const 4096) (i32.const 0)))
    (mem.write_i32 (local.get $big) (i32.const 1024) (local.get $val))
    (local.set $slice (mem.span (mem.readonly (local.get $big)) (i32.const 1024) (i32.const 4)))
    (mem.clone_span (local.get $slice)))

  (func (export "main") (result i32)
    (local $i i32)
    (local $sum i32)
    (local $detached ro_handle)

    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (i32.const 100)))
        (call $make_cycle)
        (local.set $detached (call $extract_detached (local.get $i)))
        (local.set $sum (i32.add (local.get $sum) (mem.read_i32 (local.get $detached) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    ;; Sum of 0..99 = 4950
    (local.get $sum))
)

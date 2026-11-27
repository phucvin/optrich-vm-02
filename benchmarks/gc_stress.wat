;; Benchmark: GC Allocation & Precise Marking Throughput
;; 1. `alloc_raw_bytes`: allocates many 4 KB byte-only blocks (0 refs) — GC skips raw bytes in O(1).
;; 2. `alloc_ref_chain`: builds & discards linked lists of split-block nodes (8 bytes, 1 ref).
;;
;; Run: ./build/optrich-vm --gc-stats benchmarks/gc_stress.wat alloc_raw_bytes 20000
;;      ./build/optrich-vm --gc-stats benchmarks/gc_stress.wat alloc_ref_chain 100 1000
(module
  ;; Allocate `n` 4KB raw byte buffers (only keeping the last one alive), reading 1 byte each.
  (func (export "alloc_raw_bytes") (param $n i32) (result i32)
    (local $buf rw_handle)
    (local $i i32)
    (local $acc i32)
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $buf (mem.alloc (i32.const 4096) (i32.const 0)))
        (mem.write_u8 (local.get $buf) (i32.const 0) (i32.and (local.get $i) (i32.const 255)))
        (local.set $acc (i32.add (local.get $acc) (mem.read_u8 (local.get $buf) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $acc))

  ;; Build `rounds` linked lists of length `len`, summing their i32 values, then discarding.
  (func (export "alloc_ref_chain") (param $rounds i32) (param $len i32) (result i32)
    (local $r i32)
    (local $i i32)
    (local $head rw_handle)
    (local $node rw_handle)
    (local $cur ro_handle)
    (local $acc i32)

    (block $r_done
      (loop $r_lp
        (br_if $r_done (i32.ge_s (local.get $r) (local.get $rounds)))
        (local.set $head (handle.null))
        (local.set $i (i32.const 0))
        (block $build_done
          (loop $build_lp
            (br_if $build_done (i32.ge_s (local.get $i) (local.get $len)))
            (local.set $node (mem.alloc (i32.const 4) (i32.const 1)))
            (mem.write_i32 (local.get $node) (i32.const 0) (local.get $i))
            (mem.write_ref (local.get $node) (i32.const 0) (local.get $head))
            (local.set $head (local.get $node))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $build_lp)))

        ;; Walk the chain
        (local.set $cur (local.get $head))
        (block $walk_done
          (loop $walk_lp
            (br_if $walk_done (mem.is_null (local.get $cur)))
            (local.set $acc (i32.add (local.get $acc) (mem.read_i32 (local.get $cur) (i32.const 0))))
            (local.set $cur (mem.read_ref_ro (local.get $cur) (i32.const 0)))
            (br $walk_lp)))

        (local.set $r (i32.add (local.get $r) (i32.const 1)))
        (br $r_lp)))

    (local.get $acc))
)

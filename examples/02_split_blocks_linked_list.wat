;; Example 02 — Split-Block Object Graph (Phases 1 & 2)
;; Showcases: mem.alloc(num_bytes, num_refs), disjoint raw bytes vs. GC-tracked refs,
;;            typed reads/writes (i32, f32), mem.read_ref/write_ref, mem.is_null.
;;
;; Node layout:
;;   bytes[0..4) : i32 value
;;   bytes[4..8) : f32 weight
;;   refs[0]     : handle next node (or null)
;;
;; Run: ./build/optrich-vm examples/02_split_blocks_linked_list.wat main
(module
  (func $new_node (param $val i32) (param $weight f32) (param $next handle) (result handle)
    (local $n rw_handle)
    (local.set $n (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $n) (i32.const 0) (local.get $val))
    (mem.write_f32 (local.get $n) (i32.const 4) (local.get $weight))
    (mem.write_ref (local.get $n) (i32.const 0) (local.get $next))
    (local.get $n))

  ;; Weighted sum over the linked list: sum(val * weight)
  (func $weighted_sum (param $cur handle) (result f32)
    (local $acc f32)
    (block $done
      (loop $walk
        (br_if $done (mem.is_null (local.get $cur)))
        (local.set $acc
          (f32.add
            (local.get $acc)
            (f32.mul
              (f32.convert_i32_s (mem.read_i32 (local.get $cur) (i32.const 0)))
              (mem.read_f32 (local.get $cur) (i32.const 4)))))
        (local.set $cur (mem.read_ref (local.get $cur) (i32.const 0)))
        (br $walk)))
    (local.get $acc))

  ;; Build 3-node list: (10, 0.5) -> (20, 1.5) -> (30, 2.0) => 5 + 30 + 60 = 95.0
  (func (export "main") (result f32)
    (local $head handle)
    (local.set $head (call $new_node (i32.const 30) (f32.const 2.0) (handle.null)))
    (local.set $head (call $new_node (i32.const 20) (f32.const 1.5) (local.get $head)))
    (local.set $head (call $new_node (i32.const 10) (f32.const 0.5) (local.get $head)))
    (call $weighted_sum (local.get $head)))
)

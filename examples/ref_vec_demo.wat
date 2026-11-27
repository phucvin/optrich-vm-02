;; Optrich VM 02 — Growable Handle List (RefVec) Demo
;;
;; Because MemoryBlock byte/ref capacities are fixed at mem.alloc time,
;; a growable list of handles uses a fixed header block + replaceable backing block:
;;
;;   Header block: mem.alloc(8, 1)
;;     bytes[0..4) : len (i32)
;;     bytes[4..8) : cap (i32)
;;     refs[0]     : backing ref array (mem.alloc(0, cap))
;;
;; Callers hold the header handle; when len == cap, a larger backing array is
;; allocated, existing handles are copied over, refs[0] is updated, and the old
;; backing array is automatically reclaimed by GC.
;;
;; Run: ./build/optrich-vm examples/ref_vec_demo.wat main
(module
  (string $s0 "alpha")    ;; len 5
  (string $s1 "beta")     ;; len 4
  (string $s2 "gamma")    ;; len 5
  (string $s3 "delta")    ;; len 5
  (string $s4 "epsilon")  ;; len 7  (total = 26)

  ;; Create a new RefVec with initial capacity `init_cap` (>= 1).
  (func $vec_new (param $init_cap i32) (result rw_handle)
    (local $hdr rw_handle)
    (local $backing rw_handle)
    (if (i32.lt_s (local.get $init_cap) (i32.const 1))
      (then (local.set $init_cap (i32.const 1))))
    (local.set $hdr (mem.alloc (i32.const 8) (i32.const 1)))
    (local.set $backing (mem.alloc (i32.const 0) (local.get $init_cap)))
    (mem.write_i32 (local.get $hdr) (i32.const 0) (i32.const 0))         ;; len = 0
    (mem.write_i32 (local.get $hdr) (i32.const 4) (local.get $init_cap)) ;; cap = init_cap
    (mem.write_ref (local.get $hdr) (i32.const 0) (local.get $backing))  ;; refs[0] = backing
    (local.get $hdr))

  ;; Return current number of elements in the RefVec.
  (func $vec_len (param $v ro_handle) (result i32)
    (mem.read_i32 (local.get $v) (i32.const 0)))

  ;; Return element handle at index `idx` (traps if out of bounds).
  (func $vec_get (param $v ro_handle) (param $idx i32) (result ro_handle)
    (local $len i32)
    (local $backing ro_handle)
    (local.set $len (mem.read_i32 (local.get $v) (i32.const 0)))
    (if (i32.ge_u (local.get $idx) (local.get $len))
      (then (unreachable)))
    (local.set $backing (mem.read_ref_ro (local.get $v) (i32.const 0)))
    (mem.read_ref_ro (local.get $backing) (local.get $idx)))

  ;; Append a handle to the RefVec, doubling the backing ref array when full.
  (func $vec_push (param $v rw_handle) (param $item ro_handle)
    (local $len i32)
    (local $cap i32)
    (local $new_cap i32)
    (local $backing rw_handle)
    (local $new_backing rw_handle)
    (local $i i32)

    (local.set $len (mem.read_i32 (local.get $v) (i32.const 0)))
    (local.set $cap (mem.read_i32 (local.get $v) (i32.const 4)))
    (local.set $backing (mem.read_ref_rw (local.get $v) (i32.const 0)))

    (if (i32.ge_u (local.get $len) (local.get $cap))
      (then
        (local.set $new_cap (i32.mul (local.get $cap) (i32.const 2)))
        (local.set $new_backing (mem.alloc (i32.const 0) (local.get $new_cap)))
        ;; Copy existing refs from old backing block to new backing block
        (local.set $i (i32.const 0))
        (block $copy_done
          (loop $copy_loop
            (br_if $copy_done (i32.ge_u (local.get $i) (local.get $len)))
            (mem.write_ref
              (local.get $new_backing)
              (local.get $i)
              (mem.read_ref_ro (local.get $backing) (local.get $i)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $copy_loop)))
        ;; Swap in new backing block and updated capacity
        (mem.write_ref (local.get $v) (i32.const 0) (local.get $new_backing))
        (mem.write_i32 (local.get $v) (i32.const 4) (local.get $new_cap))
        (local.set $backing (local.get $new_backing))))

    ;; Store item at backing[len] and increment len
    (mem.write_ref (local.get $backing) (local.get $len) (local.get $item))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.add (local.get $len) (i32.const 1))))

  ;; Demo: start with capacity 2, push 5 string handles (triggering 2 -> 4 -> 8 growth),
  ;; then sum the byte lengths of all stored handles (returns 26).
  (func (export "main") (result i32)
    (local $list rw_handle)
    (local $i i32)
    (local $n i32)
    (local $total_bytes i32)

    (local.set $list (call $vec_new (i32.const 2)))
    (call $vec_push (local.get $list) (mem.const_str $s0))
    (call $vec_push (local.get $list) (mem.const_str $s1))
    (call $vec_push (local.get $list) (mem.const_str $s2))
    (call $vec_push (local.get $list) (mem.const_str $s3))
    (call $vec_push (local.get $list) (mem.const_str $s4))

    (local.set $n (call $vec_len (local.get $list)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $total_bytes
          (i32.add
            (local.get $total_bytes)
            (mem.byte_len (call $vec_get (local.get $list) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $total_bytes))
)

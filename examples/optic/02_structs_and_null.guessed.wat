;; Guessed WAT for examples/optic/02_structs_and_null.opc
;; Struct `Entry` split-block layout:
;;   bytes[0..4)  : id (i32)
;;   bytes[8..16) : score (f64, 8-byte aligned)
;;   refs[0]      : name (str / ro_handle)
;;   refs[1]      : left (Entry? / handle)
;;   refs[2]      : right (Entry? / handle)
;;   => mem.alloc(16, 3)
(module
  (import "host" "trace_f64"  (func $trace_f64  (param ro_handle f64)))
  (import "host" "trace_str"  (func $trace_str  (param ro_handle ro_handle)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle handle)))

  (string $s_root      "root")
  (string $s_left      "left")
  (string $s_right     "right")
  (string $s_mid       "mid")
  (string $s_not_found "<not-found>")
  (string $l1          "step1.root_rw")
  (string $l2          "step2.root_ro")
  (string $l3          "step3.score_25")
  (string $l4          "step4.name_25")
  (string $l5          "step5.score_99")
  (string $l6          "step6.name_99")

  (func $Entry__new (param $id i32) (param $score f64) (param $name ro_handle) (result rw_handle)
    (local $e rw_handle)
    (local.set $e (mem.alloc (i32.const 16) (i32.const 3)))
    (mem.write_i32 (local.get $e) (i32.const 0) (local.get $id))
    (mem.write_f64 (local.get $e) (i32.const 8) (local.get $score))
    (mem.write_ref (local.get $e) (i32.const 0) (local.get $name))
    ;; refs[1], refs[2] are null-initialized by mem.alloc
    (local.get $e))

  ;; NOTE (Spec Friction #1): `cur` must be `handle` (not `rw_handle`) because
  ;; `mem.read_ref` returns dynamic `handle` in Optrich WAT.
  (func $Entry__insert (param $self rw_handle) (param $node rw_handle)
    (local $cur handle)
    (local $next handle)
    (local.set $cur (local.get $self))
    (block $break
      (loop $cont
        (if (i32.lt_s
              (mem.read_i32 (local.get $node) (i32.const 0))
              (mem.read_i32 (local.get $cur)  (i32.const 0)))
          (then
            (local.set $next (mem.read_ref (local.get $cur) (i32.const 1)))
            (if (mem.is_null (local.get $next))
              (then
                (mem.write_ref (local.get $cur) (i32.const 1) (local.get $node))
                (return)))
            (local.set $cur (local.get $next)))
          (else
            (local.set $next (mem.read_ref (local.get $cur) (i32.const 2)))
            (if (mem.is_null (local.get $next))
              (then
                (mem.write_ref (local.get $cur) (i32.const 2) (local.get $node))
                (return)))
            (local.set $cur (local.get $next))))
        (br $cont))))

  ;; NOTE (Spec Friction #2): `cur` is `handle` because `cur.left`/`cur.right` come from `mem.read_ref`.
  (func $find_score (param $root ro_handle) (param $target_id i32) (result f64)
    (local $cur handle)
    (local.set $cur (local.get $root))
    (block $break
      (loop $cont
        (br_if $break (mem.is_null (local.get $cur)))
        (if (i32.eq (local.get $target_id) (mem.read_i32 (local.get $cur) (i32.const 0)))
          (then
            (return (mem.read_f64 (local.get $cur) (i32.const 8)))))
        (local.set $cur
          (if (result handle)
            (i32.lt_s (local.get $target_id) (mem.read_i32 (local.get $cur) (i32.const 0)))
            (then (mem.read_ref (local.get $cur) (i32.const 1)))
            (else (mem.read_ref (local.get $cur) (i32.const 2)))))
        (br $cont)))
    (f64.const -1.0))

  ;; NOTE (Spec Friction #3): returning `str` (`ro_handle`) from `mem.read_ref` requires
  ;; wrapping with `mem.readonly` to satisfy static `ro_handle` return type!
  (func $find_name (param $root ro_handle) (param $target_id i32) (result ro_handle)
    (local $cur handle)
    (local.set $cur (local.get $root))
    (block $break
      (loop $cont
        (br_if $break (mem.is_null (local.get $cur)))
        (if (i32.eq (local.get $target_id) (mem.read_i32 (local.get $cur) (i32.const 0)))
          (then
            (return (mem.readonly (mem.read_ref (local.get $cur) (i32.const 0))))))
        (local.set $cur
          (if (result handle)
            (i32.lt_s (local.get $target_id) (mem.read_i32 (local.get $cur) (i32.const 0)))
            (then (mem.read_ref (local.get $cur) (i32.const 1)))
            (else (mem.read_ref (local.get $cur) (i32.const 2)))))
        (br $cont)))
    (mem.const_str $s_not_found))

  (func (export "main") (result i32)
    (local $root rw_handle)
    (local $ro_root ro_handle)
    (local $s25 f64)
    (local $n25 ro_handle)
    (local $s99 f64)
    (local $n99 ro_handle)

    (local.set $root (call $Entry__new (i32.const 20) (f64.const 9.5) (mem.const_str $s_root)))
    (call $Entry__insert (local.get $root) (call $Entry__new (i32.const 10) (f64.const 4.25) (mem.const_str $s_left)))
    (call $Entry__insert (local.get $root) (call $Entry__new (i32.const 30) (f64.const 7.75) (mem.const_str $s_right)))
    (call $Entry__insert (local.get $root) (call $Entry__new (i32.const 25) (f64.const 8.5) (mem.const_str $s_mid)))

    (call $trace_meta (mem.const_str $l1) (local.get $root))

    (local.set $ro_root (mem.readonly (local.get $root)))
    (call $trace_meta (mem.const_str $l2) (local.get $ro_root))

    (local.set $s25 (call $find_score (local.get $ro_root) (i32.const 25)))
    (call $trace_f64 (mem.const_str $l3) (local.get $s25))

    (local.set $n25 (call $find_name (local.get $ro_root) (i32.const 25)))
    (call $trace_str (mem.const_str $l4) (local.get $n25))

    (local.set $s99 (call $find_score (local.get $ro_root) (i32.const 99)))
    (call $trace_f64 (mem.const_str $l5) (local.get $s99))

    (local.set $n99 (call $find_name (local.get $ro_root) (i32.const 99)))
    (call $trace_str (mem.const_str $l6) (local.get $n99))

    (i32.and
      (f64.eq (local.get $s25) (f64.const 8.5))
      (f64.eq (local.get $s99) (f64.const -1.0))))
)

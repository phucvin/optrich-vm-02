;; Guessed WAT for examples/optic/05_generics_monomorphization.opc
;; Monomorphized layouts:
;;   Box<i32>       : mem.alloc(4, 0)  -> bytes[0..4)=val
;;   Box<str>       : mem.alloc(0, 1)  -> refs[0]=val
;;   Pair<i32, str> : mem.alloc(4, 1)  -> bytes[0..4)=first, refs[0]=second
;;   Pair<f64, i32> : mem.alloc(12, 0) -> bytes[0..8)=first, bytes[8..12)=second
;;   Task           : mem.alloc(8, 0)  -> bytes[0..4)=points, bytes[4..8)=multiplier
;;   Bonus          : mem.alloc(4, 0)  -> bytes[0..4)=flat
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_f64"  (func $trace_f64  (param ro_handle f64)))
  (import "host" "trace_str"  (func $trace_str  (param ro_handle ro_handle)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))

  (string $l1 "step1.box_i32_meta")
  (string $l2 "step2.box_str_meta")
  (string $l3 "step3.box_i32_val")
  (string $l4 "step4.box_str_val")
  (string $l5 "step5.pair_i32_str_meta")
  (string $l6 "step6.pair_f64_i32_meta")
  (string $l7 "step7.pair_f64_first")
  (string $l8 "step8.tasks_score")
  (string $l9 "step9.bonuses_score")
  (string $s_optic "optic")
  (string $s_lucky "lucky")

  ;; --- Monomorphized Box<i32> & Box<str> ---
  (func $Box_i32__new (param $v i32) (result rw_handle)
    (local $b rw_handle)
    (local.set $b (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $b) (i32.const 0) (local.get $v))
    (local.get $b))

  (func $Box_str__new (param $v ro_handle) (result rw_handle)
    (local $b rw_handle)
    (local.set $b (mem.alloc (i32.const 0) (i32.const 1)))
    (mem.write_ref (local.get $b) (i32.const 0) (local.get $v))
    (local.get $b))

  ;; --- Monomorphized Pair<i32, str> & Pair<f64, i32> ---
  (func $Pair_i32_str__new (param $a i32) (param $b ro_handle) (result rw_handle)
    (local $p rw_handle)
    (local.set $p (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $p) (i32.const 0) (local.get $a))
    (mem.write_ref (local.get $p) (i32.const 0) (local.get $b))
    (local.get $p))

  (func $Pair_f64_i32__new (param $a f64) (param $b i32) (result rw_handle)
    (local $p rw_handle)
    (local.set $p (mem.alloc (i32.const 12) (i32.const 0)))
    (mem.write_f64 (local.get $p) (i32.const 0) (local.get $a))
    (mem.write_i32 (local.get $p) (i32.const 8) (local.get $b))
    (local.get $p))

  ;; --- Concrete types & Scorable impls ---
  (func $Task__new (param $pts i32) (param $mul i32) (result rw_handle)
    (local $t rw_handle)
    (local.set $t (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $t) (i32.const 0) (local.get $pts))
    (mem.write_i32 (local.get $t) (i32.const 4) (local.get $mul))
    (local.get $t))

  (func $Task__score (param $self ro_handle) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0))
             (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $Bonus__new (param $flat i32) (result rw_handle)
    (local $b rw_handle)
    (local.set $b (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $b) (i32.const 0) (local.get $flat))
    (local.get $b))

  (func $Bonus__score (param $self ro_handle) (result i32)
    (mem.read_i32 (local.get $self) (i32.const 0)))

  ;; --- Monomorphized total_score<Task> and total_score<Bonus> ---
  (func $total_score__Task (param $items ro_handle) (result i32)
    (local $sum i32)
    (local $i i32)
    (local $end i32)
    (local.set $end (mem.ref_len (local.get $items)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $sum
          (i32.add (local.get $sum)
                   (call $Task__score (mem.read_ref_ro (local.get $items) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $sum))

  (func $total_score__Bonus (param $items ro_handle) (result i32)
    (local $sum i32)
    (local $i i32)
    (local $end i32)
    (local.set $end (mem.ref_len (local.get $items)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $sum
          (i32.add (local.get $sum)
                   (call $Bonus__score (mem.read_ref_ro (local.get $items) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $sum))

  (func (export "main") (result i32)
    (local $bi rw_handle)
    (local $bs rw_handle)
    (local $p1 rw_handle)
    (local $p2 rw_handle)
    (local $tasks rw_handle)
    (local $bonuses rw_handle)
    (local $ts i32)
    (local $bs_total i32)

    (local.set $bi (call $Box_i32__new (i32.const 42)))
    (local.set $bs (call $Box_str__new (mem.const_str $s_optic)))
    (call $trace_meta (mem.const_str $l1) (local.get $bi))
    (call $trace_meta (mem.const_str $l2) (local.get $bs))
    (call $trace_i32 (mem.const_str $l3) (mem.read_i32 (local.get $bi) (i32.const 0)))
    (call $trace_str (mem.const_str $l4) (mem.read_ref_ro (local.get $bs) (i32.const 0)))

    (local.set $p1 (call $Pair_i32_str__new (i32.const 7) (mem.const_str $s_lucky)))
    (local.set $p2 (call $Pair_f64_i32__new (f64.const 2.5) (i32.const 4)))
    (call $trace_meta (mem.const_str $l5) (local.get $p1))
    (call $trace_meta (mem.const_str $l6) (local.get $p2))
    (call $trace_f64 (mem.const_str $l7) (mem.read_f64 (local.get $p2) (i32.const 0)))

    (local.set $tasks (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $tasks) (i32.const 0) (call $Task__new (i32.const 5) (i32.const 3)))
    (mem.write_ref (local.get $tasks) (i32.const 1) (call $Task__new (i32.const 4) (i32.const 10)))
    (local.set $ts (call $total_score__Task (mem.readonly (local.get $tasks))))
    (call $trace_i32 (mem.const_str $l8) (local.get $ts))

    (local.set $bonuses (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $bonuses) (i32.const 0) (call $Bonus__new (i32.const 100)))
    (mem.write_ref (local.get $bonuses) (i32.const 1) (call $Bonus__new (i32.const 25)))
    (local.set $bs_total (call $total_score__Bonus (mem.readonly (local.get $bonuses))))
    (call $trace_i32 (mem.const_str $l9) (local.get $bs_total))

    (i32.add
      (i32.add (mem.read_i32 (local.get $bi) (i32.const 0))
               (mem.read_i32 (local.get $p1) (i32.const 0)))
      (i32.add (local.get $ts) (local.get $bs_total))))
)

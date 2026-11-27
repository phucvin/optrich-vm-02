;; Guessed WAT for examples/optic/12_capstone_kv_parser/lexer.opc
;; Token      : mem.alloc(4, 1) -> bytes[0..4)=kind, refs[0]=text (ro_handle span)
;; Vec<Token> : mem.alloc(8, 1) -> bytes[0..4)=len, bytes[4..8)=cap, refs[0]=mem.alloc(0, cap)
(module
  (func $is_alpha (param $b i32) (result i32)
    (i32.or
      (i32.or
        (i32.and (i32.ge_u (local.get $b) (i32.const 97)) (i32.le_u (local.get $b) (i32.const 122)))
        (i32.and (i32.ge_u (local.get $b) (i32.const 65)) (i32.le_u (local.get $b) (i32.const 90))))
      (i32.eq (local.get $b) (i32.const 95))))

  (func $is_digit (param $b i32) (result i32)
    (i32.and (i32.ge_u (local.get $b) (i32.const 48)) (i32.le_u (local.get $b) (i32.const 57))))

  (func $Token__new (param $kind i32) (param $text ro_handle) (result rw_handle)
    (local $t rw_handle)
    (local.set $t (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $t) (i32.const 0) (local.get $kind))
    (mem.write_ref (local.get $t) (i32.const 0) (local.get $text))
    (local.get $t))

  (func $Vec_Token__new (param $cap i32) (result rw_handle)
    (local $v rw_handle)
    (local.set $v (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.const 0))
    (mem.write_i32 (local.get $v) (i32.const 4) (local.get $cap))
    (mem.write_ref (local.get $v) (i32.const 0) (mem.alloc (i32.const 0) (local.get $cap)))
    (local.get $v))

  (func $Vec_Token__push (param $v rw_handle) (param $tok rw_handle)
    (local $len i32)
    (local $cap i32)
    (local $buf handle)
    (local $new_cap i32)
    (local $new_buf rw_handle)
    (local.set $len (mem.read_i32 (local.get $v) (i32.const 0)))
    (local.set $cap (mem.read_i32 (local.get $v) (i32.const 4)))
    (local.set $buf (mem.read_ref (local.get $v) (i32.const 0)))
    (if (i32.ge_s (local.get $len) (local.get $cap))
      (then
        (local.set $new_cap (i32.shl (local.get $cap) (i32.const 1)))
        (local.set $new_buf (mem.alloc (i32.const 0) (local.get $new_cap)))
        (mem.copy_refs (local.get $new_buf) (i32.const 0) (local.get $buf) (i32.const 0) (local.get $len))
        (mem.write_i32 (local.get $v) (i32.const 4) (local.get $new_cap))
        (mem.write_ref (local.get $v) (i32.const 0) (local.get $new_buf))
        (local.set $buf (local.get $new_buf))))
    (mem.write_ref (local.get $buf) (local.get $len) (local.get $tok))
    (mem.write_i32 (local.get $v) (i32.const 0) (i32.add (local.get $len) (i32.const 1))))

  (func (export "tokenize") (param $src ro_handle) (result rw_handle)
    (local $toks rw_handle)
    (local $i i32)
    (local $n i32)
    (local $b i32)
    (local $start i32)

    (local.set $toks (call $Vec_Token__new (i32.const 8)))
    (local.set $n (mem.byte_len (local.get $src)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $b (mem.read_u8 (local.get $src) (local.get $i)))
        (if (i32.or (i32.eq (local.get $b) (i32.const 32))
                    (i32.or (i32.eq (local.get $b) (i32.const 9)) (i32.eq (local.get $b) (i32.const 10))))
          (then
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (if (i32.eq (local.get $b) (i32.const 61)) ;; '='
          (then
            (call $Vec_Token__push (local.get $toks)
              (call $Token__new (i32.const 2) (mem.span (local.get $src) (local.get $i) (i32.const 1))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (if (i32.eq (local.get $b) (i32.const 59)) ;; ';'
          (then
            (call $Vec_Token__push (local.get $toks)
              (call $Token__new (i32.const 3) (mem.span (local.get $src) (local.get $i) (i32.const 1))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (if (call $is_alpha (local.get $b))
          (then
            (local.set $start (local.get $i))
            (block $end_id
              (loop $lp_id
                (br_if $end_id (i32.ge_s (local.get $i) (local.get $n)))
                (local.set $b (mem.read_u8 (local.get $src) (local.get $i)))
                (br_if $end_id (i32.eqz (i32.or (call $is_alpha (local.get $b)) (call $is_digit (local.get $b)))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (br $lp_id)))
            (call $Vec_Token__push (local.get $toks)
              (call $Token__new (i32.const 0)
                (mem.span (local.get $src) (local.get $start) (i32.sub (local.get $i) (local.get $start)))))
            (br $lp)))
        (if (call $is_digit (local.get $b))
          (then
            (local.set $start (local.get $i))
            (block $end_num
              (loop $lp_num
                (br_if $end_num (i32.ge_s (local.get $i) (local.get $n)))
                (br_if $end_num (i32.eqz (call $is_digit (mem.read_u8 (local.get $src) (local.get $i)))))
                (local.set $i (i32.add (local.get $i) (i32.const 1)))
                (br $lp_num)))
            (call $Vec_Token__push (local.get $toks)
              (call $Token__new (i32.const 1)
                (mem.span (local.get $src) (local.get $start) (i32.sub (local.get $i) (local.get $start)))))
            (br $lp)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $toks))
)

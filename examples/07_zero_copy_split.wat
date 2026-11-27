;; Example 07 — Zero-Copy String Tokenizer (`mem.span` + `mem.cmp`)
;; Splits "apple,banana,cherry" on ',' into zero-copy `ro_handle` slices
;; stored in a ref-slot array (zero bytes copied!).
;;
;; Run: ./build/optrich-vm examples/07_zero_copy_split.wat main
(module
  (string $csv    "apple,banana,cherry")
  (string $target "banana")

  ;; Split `s` by byte `delim` into a ref array of zero-copy `ro_handle` spans
  (func $split (param $s ro_handle) (param $delim i32) (param $max_parts i32) (result handle)
    (local $out rw_handle)
    (local $i i32)
    (local $start i32)
    (local $n i32)
    (local $count i32)

    (local.set $out (mem.alloc (i32.const 0) (local.get $max_parts)))
    (local.set $n (mem.byte_len (local.get $s)))

    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (if (i32.eq (mem.read_u8 (local.get $s) (local.get $i)) (local.get $delim))
          (then
            (mem.write_ref (local.get $out) (local.get $count)
              (mem.span (local.get $s)
                        (local.get $start)
                        (i32.sub (local.get $i) (local.get $start))))
            (local.set $count (i32.add (local.get $count) (i32.const 1)))
            (local.set $start (i32.add (local.get $i) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))

    ;; Emit final token after the last delimiter
    (mem.write_ref (local.get $out) (local.get $count)
      (mem.span (local.get $s)
                (local.get $start)
                (i32.sub (local.get $n) (local.get $start))))
    (local.set $count (i32.add (local.get $count) (i32.const 1)))

    ;; Return a narrowed ref_span containing only the `count` found tokens
    (mem.ref_span (local.get $out) (i32.const 0) (local.get $count)))

  ;; Entry point: split CSV -> 3 tokens; verify token[1] == "banana" (len 6)
  ;; Returns: token_count * 100 + len(token[1]) * 10 + equals_target => 361
  (func (export "main") (result i32)
    (local $parts handle)
    (local $tok1 handle)
    (local $eq i32)

    (local.set $parts (call $split (mem.const_str $csv) (i32.const 44) (i32.const 8))) ;; ',' = 44
    (local.set $tok1 (mem.read_ref (local.get $parts) (i32.const 1)))

    (local.set $eq
      (i32.and
        (i32.eq (mem.byte_len (local.get $tok1)) (mem.byte_len (mem.const_str $target)))
        (i32.eqz (mem.cmp (local.get $tok1) (i32.const 0)
                          (mem.const_str $target) (i32.const 0)
                          (mem.byte_len (mem.const_str $target))))))

    (i32.add
      (i32.mul (mem.ref_len (local.get $parts)) (i32.const 100))
      (i32.add
        (i32.mul (mem.byte_len (local.get $tok1)) (i32.const 10))
        (local.get $eq))))
)

;; Example 09 — Rust-Style Unboxed `&dyn Trait` Fat-Pointer Dispatch
;; Contrast with Example 06 (boxed Go-style trait object):
;;   Here, `&dyn Scorable` is passed directly on the stack as an UNBOXED fat pointer
;;   `(data: handle, vtable: ro_handle)` — ZERO heap allocation for a trait wrapper!
;;
;; Trait `Scorable`:
;;   vtable[0]: base_score(self: handle) -> i32
;;   vtable[1]: boosted_score(self: handle, bonus: i32) -> i32
;;
;; Run: ./build/optrich-vm --gc-stats examples/09_rust_trait_dispatch.wat main
(module
  (type $base_sig  (func (param handle) (result i32)))
  (type $boost_sig (func (param handle i32) (result i32)))

  ;; --- Generic function taking Rust-style `&dyn Scorable` as (data, vtable) ---
  (func $eval_dyn (param $data handle) (param $vt ro_handle) (param $bonus i32) (result i32)
    (i32.add
      (call_ref (type $base_sig)  (local.get $data) (mem.read_func (local.get $vt) (i32.const 0)))
      (call_ref (type $boost_sig) (local.get $data) (local.get $bonus)
                                  (mem.read_func (local.get $vt) (i32.const 1)))))

  ;; --- Concrete Type 1: `Article` (bytes[0..4) = words, bytes[4..8) = likes) ---
  (func $article_base (param $self handle) (result i32)
    (i32.add (mem.read_i32 (local.get $self) (i32.const 0))
             (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $article_boost (param $self handle) (param $bonus i32) (result i32)
    (i32.add (call $article_base (local.get $self)) (local.get $bonus)))

  (func $article_vtable (result ro_handle)
    (local $vt rw_handle)
    (local.set $vt (mem.alloc_vtable (i32.const 2)))
    (mem.write_func (local.get $vt) (i32.const 0) (ref.func $article_base))
    (mem.write_func (local.get $vt) (i32.const 1) (ref.func $article_boost))
    (mem.readonly (local.get $vt)))

  ;; --- Concrete Type 2: `Tweet` (bytes[0..4) = retweets) ---
  (func $tweet_base (param $self handle) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (i32.const 2)))

  (func $tweet_boost (param $self handle) (param $bonus i32) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $bonus)))

  (func $tweet_vtable (result ro_handle)
    (local $vt rw_handle)
    (local.set $vt (mem.alloc_vtable (i32.const 2)))
    (mem.write_func (local.get $vt) (i32.const 0) (ref.func $tweet_base))
    (mem.write_func (local.get $vt) (i32.const 1) (ref.func $tweet_boost))
    (mem.readonly (local.get $vt)))

  (func (export "main") (result i32)
    (local $art rw_handle)
    (local $tw  rw_handle)

    ;; Article(words=20, likes=5): base=25, boost(+10)=35 => 60
    (local.set $art (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $art) (i32.const 0) (i32.const 20))
    (mem.write_i32 (local.get $art) (i32.const 4) (i32.const 5))

    ;; Tweet(retweets=7): base=14, boost(*10)=70 => 84
    (local.set $tw (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $tw) (i32.const 0) (i32.const 7))

    ;; Pass unboxed fat pointers (data, vtable) directly — zero wrapper allocation!
    ;; Total = 60 + 84 = 144
    (i32.add
      (call $eval_dyn (local.get $art) (call $article_vtable) (i32.const 10))
      (call $eval_dyn (local.get $tw)  (call $tweet_vtable)   (i32.const 10))))
)

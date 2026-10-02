#import "../../typst-ko/lib.typ": *

= Appendix A --- Words and builtins

The text is where words are *explained*. This appendix gathers only tables to skim when you get stuck reading code. The word list is the same as the
specification's Appendix A, and anything not in that list is not a word.

== The forty-four words

#dtable(
  columns: 3,
  id: "a1-keywords",
  caption: [All the words],
  [*Kind*], [*Words*], [*Chapter*],
  [Declarations], [`module` `use` `def` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state`], [#chrefs("modules", "named-types")],
  [ops], [`fn` `proc` `export` `unsafe` `extern` `satisfies`], [#chref("ops")],
  [Locals], [`let` `var` `set` `be`], [#chref("locals")],
  [Flow], [`if` `else` `while` `for` `guard` `match` `case` `return` `break` `continue` `try`], [#chref("control")],
  [Expressions and values], [`expr` `lit` `true` `false` `none`], [#chref("expr")],
  [Concurrency], [`spawn` `send`], [#chref("actors")],
  [Ownership], [`drop`], [#chref("ownership")],
  [Tests], [`test` `expect`], [#chref("build-test")],
  [Blocks], [`do` `end`], [#chref("surface")],
)

`rem` and `note` are markers opening comments, and clause heads such as `input`, `output`, `effects` and `requires` and builtins such as `add` and `len`
are not words but cannot be used as names.

== Removed words

#dtable(
  columns: 2,
  id: "a1-removed",
  caption: [Removed words and what to use instead (`E-VOCAB-REMOVED`)],
  [*Removed*], [*Instead*],
  [`loop`], [`while true .`],
  [`give`], [`return`],
  [`unit`], [`void`],
  [`calcop` · `procop`], [`fn` · `proc`],
  [`is` · `as` (in declarations) · `local`], [Not written],
  [`to` · `in` (field access)], [`field a b` · `idx a i`],
  [`on`], [`proc` inside an actor],
  [`fail`], [`return error <variant>`],
  [`;` · `,`], [`.`],
  [`make`], [`lit`],
  [`index`], [`idx`],
  [a `struct`·`enum`·`type`·`newtype` declaration without `def`], [`def struct` · `def enum` · `def type` · `def newtype`],
)

`as` and `to` remain only as position markers in `use … as <alias>` and `case <low> to <high>`.

== Frequently used builtins

#dtable(
  columns: 2,
  id: "a1-builtins",
  caption: [Kinds of builtins],
  [*Kind*], [*Operations*],
  [Arithmetic (stopping)], [`add` `sub` `mul` `div` `mod` `neg` `abs` `min` `max`],
  [Arithmetic (choosing the outcome)], [`wrap_*` `sat_*` `chk_*` `div_nz` `nonzero_of`],
  [Changing width], [`widen` `narrow` `narrow_wrap` `narrow_sat` `narrow_try` `cast` `bit_cast`],
  [Comparison and logic], [`eq` `ne` `lt` `le` `gt` `ge` `and` `or` `not`],
  [Bits], [`bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap` `clmul_lo` `clmul_hi`],
  [Sequences], [`len` `idx` `subslice` `view` `view_array` `same_slice`],
  [Groupings], [`field` `method` `isa` `get`],
  [Answer-carrying types], [`some` `ok` `error` `is_some` `is_none` `is_ok` `is_error` `some_value` `ok_value` `error_value` `value_or`],
  [Translation time], [`size_of` `comptime` `config`],
  [Allocation], [`alloc_bytes`],
  [I/O leaves], [`write_out` `read_in` `arg` · file, connection, clock, random and terminal leaves are wrapped by the standard library],
  [Atomic], [`atomic_load` `atomic_store` `atomic_add` `atomic_sub` `atomic_swap` `atomic_cas` `atomic_fence`],
  [Devices], [`read_volatile` `write_volatile`],
  [Concurrency], [`await` `drain` `channel` `chsend` `chrecv` `yield` `spawn` `send`],
  [Atomic (bits)], [`atomic_and` `atomic_or` `atomic_xor`],
  [Floating-point maths --- #chref("numbers")], [`sqrt` `sin` `cos` `exp` `log` `pow` `floor` `ceil` `round` `fmod` `sum_neumaier` `sum_seq`],
  [Pipe stages --- #chref("pipe")], [`pipe` `map` `filter` `fold` `scan` `take` `skip` `zip` `enumerate` `collect` `count` `all` `any` `into`],
  [Lanes (SIMD) --- #chref("parallel-atomic")], [`splat` `load` `store` `load_masked` `store_masked` `lane_select` `lane_any` `lane_all` `reduce_add` `reduce_mul` `reduce_min` `reduce_max` `lane_avg` `native_lanes` `lane_reverse` `lane_rotate` `prefetch`],
  [Small-number sets --- #chref("named-types")], [`bitset_new` `bitset_union` `bitset_intersect` `bitset_difference` `bitset_complement` `bitset_contains` `bitset_is_subset` `bitset_is_empty` `bitset_remove`],
  [Layouts and views --- #chref("named-types")], [`encode` `try_view` `view_segments` `seg` `segs` `capacity`],
  [Borrows and regions --- #chref("references") · #chref("regions")], [`ref` `mut_ref` `deref` `borrow` `region` `stack_new` `push` `pop` `swap`],
  [Contracts and errors --- #chref("contracts")], [`range` `ret` `expect` `panic`],
  [Computation leaves --- stand only after `call_builtin`], [`clmul_lo` `clmul_hi` `aes_round` `aes_round_last` `aes_ctr` `ghash` `chacha20` `poly1305` `aes_gcm` `chacha_poly` `sha256` `sha384` `sha512` `crc32` `hash_bytes` `rng_next`],
  [C strings --- #chref("ffi")], [`cstr_of` `str_from_cstr`],
  [File and network leaves --- only after `call_builtin`], [`file_open` `file_read` `file_write` `file_seek` `file_close` `file_type` `link_type` `dir_open` `dir_read` `dir_close` `dir_make` `path_remove` `path_rename` `net_listen` `net_accept` `net_connect` `net_resolve` `net_send` `net_recv` `net_close` `net_pair` `net_port`],
  [Host leaves --- #chref("lib-map")], [`env_get` `reactor_new` `r_read` `r_write`],
)

★ *The computation leaves and the file and network leaves are not global words.* They stand only in that position --- `call_builtin sha256 msg out`. Calling one bare is `E-BUILTIN-BARE`; naming something else after `call_builtin` is `E-BUILTIN-NAME`. The reason is one: a word a program uses once should not cost every reader a name to remember. The same rule already governs the stage names inside `pipe` and the type slot of `cast u8 x`.

This table sorts every builtin of the canon into a group. The chapter next to each group name explains that group with examples. The
one-line meanings are authoritative in the repository's `docs/spec/BUILTIN-MEANINGS.tsv`.

== Effects and capabilities

#dtable(
  columns: 3,
  id: "a1-effects",
  caption: [Effect atoms and their paired capabilities],
  [*Effect*], [*Permitting capability*], [*Chapter*],
  [`io`], [`cap io` · `file_system` · `net` · `tty` · `clock` · `random`], [#chref("capabilities")],
  [`alloc` · `heap`], [`cap allocator` · `cap heap`], [#chrefs("regions", "fixed-memory")],
  [`atomic`], [`cap atomic`], [#chref("parallel-atomic")],
  [`device`], [`cap mmio`], [#chref("hardware")],
  [`unsafe`], [`cap c` · `cap machine` (depending on use)], [#chrefs("ffi", "hardware")],
  [`state` · `panic` · `wait` · `concurrent`], [Written without capabilities], [#chrefs("effects", "tasks-channels")],
)

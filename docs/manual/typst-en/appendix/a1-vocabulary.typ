#import "../../typst-ko/lib.typ": *

= Appendix A --- Words and builtins

The text is where words are *explained*. This appendix gathers only tables to skim when you get stuck reading code. The word list is the same as the
specification's Appendix A, and anything not in that list is not a word.

== The forty-three words

#dtable(
  columns: 3,
  id: "a1-keywords",
  caption: [All the words],
  [*Kind*], [*Words*], [*Chapter*],
  [Declarations], [`module` `use` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state`], [#chrefs("modules", "named-types")],
  [ops], [`fn` `proc` `export` `unsafe` `extern` `satisfies`], [#chref("ops")],
  [Locals], [`let` `var` `set` `be`], [#chref("locals")],
  [Flow], [`if` `else` `while` `for` `guard` `match` `case` `return` `break` `continue` `try`], [#chref("control")],
  [Expressions and values], [`expr` `make` `true` `false` `none`], [#chref("expr")],
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
  [`to` · `in` (field access)], [`field a b` · `index a i`],
  [`on`], [`proc` inside an actor],
  [`fail`], [`return error <variant>`],
  [`;` · `,`], [`.`],
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
  [Bits], [`bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap`],
  [Sequences], [`len` `index` `subslice` `view` `view_array` `same_slice`],
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
  [Pipe stages --- #chref("pipe")], [`pipe` `map` `filter` `fold` `scan` `take` `skip` `zip` `enumerate` `reverse` `collect` `count` `all` `any` `into`],
  [Lanes (SIMD) --- #chref("parallel-atomic")], [`splat` `load` `store` `load_masked` `store_masked` `select` `reduce_add` `reduce_mul` `reduce_min` `reduce_max` `avg` `native_lanes` `rotate` `prefetch`],
  [Small-number sets --- #chref("named-types")], [`bitset_new` `union` `intersect` `difference` `complement` `contains` `is_subset` `is_empty` `remove`],
  [Layouts and views --- #chref("named-types")], [`encode` `try_view` `view_segments` `seg` `segs` `capacity`],
  [Borrows and regions --- #chref("references") · #chref("regions")], [`ref` `mut_ref` `deref` `borrow` `region` `stack_new` `push` `pop` `swap`],
  [Contracts and errors --- #chref("contracts")], [`range` `ret` `expect` `panic`],
  [Hashes --- #chref("lib-text")], [`crc32` `hash_bytes` `sha256` `sha512`],
  [C strings --- #chref("ffi")], [`cstr_of` `str_from_cstr`],
  [Host leaves --- #chref("lib-map")], [`file_open` `file_read` `file_write` `file_seek` `file_close` `file_type` `link_type` `dir_open` `dir_read` `dir_close` `dir_make` `path_remove` `path_rename` `net_listen` `net_accept` `net_connect` `net_resolve` `net_send` `net_recv` `net_close` `net_pair` `net_port` `env_get` `rng_next` `reactor_new` `r_read` `r_write`],
)

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

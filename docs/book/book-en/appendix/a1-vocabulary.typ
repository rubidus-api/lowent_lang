#import "../../book/lib.typ": *

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
  [Concurrency], [`await` `drain` `channel` `chsend` `chrecv` `yield`],
)

The authoritative full list and meanings are the repository's `docs/spec/BUILTIN-MEANINGS.tsv`.

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

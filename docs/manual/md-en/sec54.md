# Appendix A — Words and builtins

The text is where words are **explained**. This appendix gathers only tables to skim when you get stuck reading code. The word list is the same as the specification’s Appendix A, and anything not in that list is not a word.

## <a id="sx1"></a>The forty-three words

| **Kind** | **Words** | **Chapter** |
|---|---|---|
| Declarations | `module` `use` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state` | chapters 21 and 13 |
| ops | `fn` `proc` `export` `unsafe` `extern` `satisfies` | chapter 5 |
| Locals | `let` `var` `set` `be` | chapter 6 |
| Flow | `if` `else` `while` `for` `guard` `match` `case` `return` `break` `continue` `try` | chapter 7 |
| Expressions and values | `expr` `make` `true` `false` `none` | chapter 8 |
| Concurrency | `spawn` `send` | chapter 25 |
| Ownership | `drop` | chapter 19 |
| Tests | `test` `expect` | chapter 31 |
| Blocks | `do` `end` | chapter 3 |

*Table 50.1 — All the words*

`rem` and `note` are markers opening comments, and clause heads such as `input`, `output`, `effects` and `requires` and builtins such as `add` and `len` are not words but cannot be used as names.

## <a id="sx2"></a>Removed words

| **Removed** | **Instead** |
|---|---|
| `loop` | `while true .` |
| `give` | `return` |
| `unit` | `void` |
| `calcop` · `procop` | `fn` · `proc` |
| `is` · `as` (in declarations) · `local` | Not written |
| `to` · `in` (field access) | `field a b` · `index a i` |
| `on` | `proc` inside an actor |
| `fail` | `return error <variant>` |
| `;` · `,` | `.` |

*Table 50.2 — Removed words and what to use instead (`E-VOCAB-REMOVED`)*

`as` and `to` remain only as position markers in `use … as <alias>` and `case <low> to <high>`.

## <a id="sx3"></a>Frequently used builtins

| **Kind** | **Operations** |
|---|---|
| Arithmetic (stopping) | `add` `sub` `mul` `div` `mod` `neg` `abs` `min` `max` |
| Arithmetic (choosing the outcome) | `wrap_*` `sat_*` `chk_*` `div_nz` `nonzero_of` |
| Changing width | `widen` `narrow` `narrow_wrap` `narrow_sat` `narrow_try` `cast` `bit_cast` |
| Comparison and logic | `eq` `ne` `lt` `le` `gt` `ge` `and` `or` `not` |
| Bits | `bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap` |
| Sequences | `len` `index` `subslice` `view` `view_array` `same_slice` |
| Groupings | `field` `method` `isa` `get` |
| Answer-carrying types | `some` `ok` `error` `is_some` `is_none` `is_ok` `is_error` `some_value` `ok_value` `error_value` `value_or` |
| Translation time | `size_of` `comptime` `config` |
| Allocation | `alloc_bytes` |
| I/O leaves | `write_out` `read_in` `arg` · file, connection, clock, random and terminal leaves are wrapped by the standard library |
| Atomic | `atomic_load` `atomic_store` `atomic_add` `atomic_sub` `atomic_swap` `atomic_cas` `atomic_fence` |
| Devices | `read_volatile` `write_volatile` |
| Concurrency | `await` `drain` `channel` `chsend` `chrecv` `yield` |

*Table 50.3 — Kinds of builtins*

The authoritative full list and meanings are the repository’s `docs/spec/BUILTIN-MEANINGS.tsv`.

## <a id="sx4"></a>Effects and capabilities

| **Effect** | **Permitting capability** | **Chapter** |
|---|---|---|
| `io` | `cap io` · `file_system` · `net` · `tty` · `clock` · `random` | chapter 16 |
| `alloc` · `heap` | `cap allocator` · `cap heap` | chapters 18 and 20 |
| `atomic` | `cap atomic` | chapter 27 |
| `device` | `cap mmio` | chapter 30 |
| `unsafe` | `cap c` · `cap machine` (depending on use) | chapters 29 and 30 |
| `state` · `panic` · `wait` · `concurrent` | Written without capabilities | chapters 15 and 26 |

*Table 50.4 — Effect atoms and their paired capabilities*

---

[← Prev](ch50.md) · [Contents](README.md) · [Next →](sec55.md)

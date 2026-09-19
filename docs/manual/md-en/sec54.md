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
| Concurrency | `await` `drain` `channel` `chsend` `chrecv` `yield` `spawn` `send` |
| Atomic (bits) | `atomic_and` `atomic_or` `atomic_xor` |
| Floating-point maths — chapter 4 | `sqrt` `sin` `cos` `exp` `log` `pow` `floor` `ceil` `round` `fmod` `sum_neumaier` `sum_seq` |
| Pipe stages — chapter 24 | `pipe` `map` `filter` `fold` `scan` `take` `skip` `zip` `enumerate` `reverse` `collect` `count` `all` `any` `into` |
| Lanes (SIMD) — chapter 27 | `splat` `load` `store` `load_masked` `store_masked` `select` `reduce_add` `reduce_mul` `reduce_min` `reduce_max` `avg` `native_lanes` `rotate` `prefetch` |
| Small-number sets — chapter 13 | `bitset_new` `union` `intersect` `difference` `complement` `contains` `is_subset` `is_empty` `remove` |
| Layouts and views — chapter 13 | `encode` `try_view` `view_segments` `seg` `segs` `capacity` |
| Borrows and regions — chapter 12 · chapter 18 | `ref` `mut_ref` `deref` `borrow` `region` `stack_new` `push` `pop` `swap` |
| Contracts and errors — chapter 14 | `range` `ret` `expect` `panic` |
| Hashes — chapter 33 | `crc32` `hash_bytes` `sha256` `sha384` `sha512` |
| C strings — chapter 29 | `cstr_of` `str_from_cstr` |
| Host leaves — chapter 32 | `file_open` `file_read` `file_write` `file_seek` `file_close` `file_type` `link_type` `dir_open` `dir_read` `dir_close` `dir_make` `path_remove` `path_rename` `net_listen` `net_accept` `net_connect` `net_resolve` `net_send` `net_recv` `net_close` `net_pair` `net_port` `env_get` `rng_next` `reactor_new` `r_read` `r_write` |

*Table 50.3 — Kinds of builtins*

This table sorts every builtin of the canon into a group. The chapter next to each group name explains that group with examples. The one-line meanings are authoritative in the repository’s `docs/spec/BUILTIN-MEANINGS.tsv`.

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

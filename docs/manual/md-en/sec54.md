# Appendix A — Words and builtins

The text is where words are **explained**. This appendix gathers only tables to skim when you get stuck reading code. The word list is the same as the specification’s Appendix A, and anything not in that list is not a word.

## <a id="sx1"></a>The forty-four words

| **Kind** | **Words** | **Chapter** |
|---|---|---|
| Declarations | `module` `use` `def` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state` | chapters 21 and 13 |
| ops | `fn` `proc` `export` `unsafe` `extern` `satisfies` | chapter 5 |
| Locals | `let` `var` `set` `be` | chapter 6 |
| Flow | `if` `else` `while` `for` `guard` `match` `case` `return` `break` `continue` `try` | chapter 7 |
| Expressions and values | `expr` `lit` `true` `false` `none` | chapter 8 |
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
| `to` · `in` (field access) | `field a b` · `idx a i` |
| `on` | `proc` inside an actor |
| `fail` | `return error <variant>` |
| `;` · `,` | `.` |
| `make` | `lit` |
| `index` | `idx` |
| a `struct`·`enum`·`type`·`newtype` declaration without `def` | `def struct` · `def enum` · `def type` · `def newtype` |

*Table 50.2 — Removed words and what to use instead (`E-VOCAB-REMOVED`)*

`as` and `to` remain only as position markers in `use … as <alias>` and `case <low> to <high>`.

## <a id="sx3"></a>Frequently used builtins

| **Kind** | **Operations** |
|---|---|
| Arithmetic (stopping) | `add` `sub` `mul` `div` `mod` `neg` `abs` `min` `max` |
| Arithmetic (choosing the outcome) | `wrap_*` `sat_*` `chk_*` `div_nz` `nonzero_of` |
| Changing width | `widen` `narrow` `narrow_wrap` `narrow_sat` `narrow_try` `cast` `bit_cast` |
| Comparison and logic | `eq` `ne` `lt` `le` `gt` `ge` `and` `or` `not` |
| Bits | `bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap` `clmul_lo` `clmul_hi` |
| Sequences | `len` `idx` `subslice` `view` `view_array` `same_slice` |
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
| Pipe stages — chapter 24 | `pipe` `map` `filter` `fold` `scan` `take` `skip` `zip` `enumerate` `collect` `count` `all` `any` `into` |
| Lanes (SIMD) — chapter 27 | `splat` `load` `store` `load_masked` `store_masked` `lane_select` `lane_any` `lane_all` `reduce_add` `reduce_mul` `reduce_min` `reduce_max` `lane_avg` `native_lanes` `lane_reverse` `lane_rotate` `prefetch` |
| Small-number sets — chapter 13 | `bitset_new` `bitset_union` `bitset_intersect` `bitset_difference` `bitset_complement` `bitset_contains` `bitset_is_subset` `bitset_is_empty` `bitset_remove` |
| Layouts and views — chapter 13 | `encode` `try_view` `view_segments` `seg` `segs` `capacity` |
| Borrows and regions — chapter 12 · chapter 18 | `ref` `mut_ref` `deref` `borrow` `region` `stack_new` `push` `pop` `swap` |
| Contracts and errors — chapter 14 | `range` `ret` `expect` `panic` |
| Computation leaves — stand only after `call_builtin` | `clmul_lo` `clmul_hi` `aes_round` `aes_round_last` `aes_ctr` `ghash` `chacha20` `poly1305` `aes_gcm` `chacha_poly` `sha256` `sha384` `sha512` `crc32` `hash_bytes` `rng_next` |
| C strings — chapter 29 | `cstr_of` `str_from_cstr` |
| Host leaves — chapter 32 | `file_open` `file_read` `file_write` `file_seek` `file_close` `file_type` `link_type` `dir_open` `dir_read` `dir_close` `dir_make` `path_remove` `path_rename` `net_listen` `net_accept` `net_connect` `net_resolve` `net_send` `net_recv` `net_close` `net_pair` `net_port` `env_get` `reactor_new` `r_read` `r_write` |

*Table 50.3 — Kinds of builtins*

★ **The fifteen computation leaves are not global words.** They stand only in that position — `call_builtin sha256 msg out`. Calling one bare is `E-BUILTIN-BARE`; naming something else after `call_builtin` is `E-BUILTIN-NAME`. The reason is one: a word a program uses once should not cost every reader a name to remember. The same rule already governs the stage names inside `pipe` and the type slot of `cast u8 x`.

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

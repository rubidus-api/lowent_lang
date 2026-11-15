# basic_type_system conformance runner

`run.py` runs the conformance vectors of [basic_type_system](https://github.com/rubidus-api) (BTS) on both Lowent
implementations — the VM (`lowentc --run-batch`) and the native build (`lowentc --emit-c` + `cc`) — and writes a
`bts-results/2` file (harness protocol §5, copied in `conformance/harness-protocol.md`).

```sh
python3 impl/tests/bts/run.py --suite integer_ops      # → impl/build/bts/results.jsonl
python3 -I impl/tests/bts/conformance/check_results.py impl/build/bts/results.jsonl
```

- `conformance/` is a copy of BTS `docs/conformance/` (vectors, `check_results.py`, `gen.py`, `refmodel/`, `targets/`, MIT licence).
  `conformance/SOURCES.tsv` records each file's sha256 and the BTS commit it came from. Do not edit these files; take a new copy.
- Each case becomes one Lowent function; each row is one call. A row that must trap runs on its own. A row judged by translation
  (`casting_rules` relations and common types) is its own module: `--check` stands for the VM, `--emit-c` for native.
- Row numbers follow harness protocol §5.2 and are counted against the copied `gen.py` before anything runs.
- Build mode `debug` (keeps every check). Target `x86_64` (`usize` and `idiff` are 64 bits; rows for other widths are
  `target-unavailable`).
- Skips: `language-policy` — Lowent has decided not to have the operation; `not-implemented` — Lowent has no spelling for it yet
  (the suite then makes no claim).

## Suites

| suite | rows | how it maps | what is not run |
|---|---|---|---|
| `integer_ops` | 114,991 | `names.lowent`, else a same-meaning spelling (`wrap_sub 0 a` for a wrapping negation, `intops.*`) | operations Lowent does not have (`language-policy`) |
| `float_ops` | 15,570 | the bit patterns go in and come out as `u32`/`u64` through `bit_cast`; `f32` `sqrt` goes through `math.sqrt` on `f64` (exact: 53 ≥ 2·24 + 2); `order_key` and `sum_tree4/8/16` are the library `fbits` (one function per `sum_tree` row, the array written into it) | — |
| `casting_rules` | 3,048 | `relations` and `common_type` by translation; `narrow` · `cast` · `narrow_wrap` · `narrow_sat` · `narrow_try` for the values | — (a common type is checked by accepting the result as that type, so a narrower choice would also pass) |
| `simd_ops` | 1,556 | `lit vec N T` from the lane arguments; `add` · `sub` · `mul` · `sat_*` · `lane_select` over `eq`/`lt` · `reduce_add/min/max` | lane `and` `or` `xor` `shl` `shr`, `to_int`, `lane_lookup16`, `load_masked`, `store_masked`, `load_full`, `kernels` (`not-implemented`) |
| `litmus` | 146 | one `proc` per test: each thread is a flow in a `task_group`, sharing a `mut slice u64` through `atomic_*` with the test's orders | — |
| `programs` | 104 | `programs.py` translates the shared program notation to one op per module (prelude: owned box · a file that needs completion · the externals); judged by translation, `--check` for the VM and `--emit-c` for native | `secret` · `declassify` and one-field borrows (`language-policy`); raw pointers · `uaddr` · arenas (`not-implemented`); the AVR · CHERI examples on this x86_64 run (`target-unavailable`) |
| `range_erasure` | 34 | `erasure.py` translates each example to one op; the compiler's remaining-check list (`--ir`, «남은 자리», with each check's kind) is read back — a `remove` check must be absent, a `keep` check present and stopping on its `fires_on` input (VM and native) | — |

★ `litmus`: Lowent's flows are green threads on one OS thread, so every outcome is a sequential interleaving. A pass shows that no
forbidden outcome appeared — not that a weak outcome cannot appear on a machine that runs the threads at once.

★ `simd_ops` on native: every case returns a `vec`, so the native rows run the tagged path; the fast path's lane saturation is checked by
the golden suite («bts runner finds 2», `vf` · `vx`).

★ `simd_ops` `reduce_sum`: Lowent's `reduce_add` stops when the lanes' sum does not fit the lane type (canon §6.3.7 (2)); BTS expects it to
wrap. Those rows fail and are listed, with that reason, in the private baseline of the gate.

★ `programs`: two reject examples are accepted on purpose for now — a write view or a `mut_ref` bound to another name and then used
through the old one. Lowent reads the binding as an alias of the same borrow; BTS reads it as a move. Which one Lowent means is the
owner's to decide; the rows are in the gate's baseline as a meaning difference.

## What the runner found

- 2026-11-13 (`integer_ops`): an unsigned 64-bit value inside an option or a struct printed as negative; `intops.mod_trunc` stopped on the
  smallest value divided by −1.
- 2026-11-14 (`float_ops` · `casting_rules` · `simd_ops`): an `f32` signalling NaN lost its bits through `bit_cast`; `narrow_sat u64` of a float
  at or above 2^64 gave 2^63 − 1; integer lane `sat_add`/`sat_sub`/`sat_mul` wrapped and signed lanes printed unsigned; `bit_cast` accepted a
  vector (the VM stopped at run time, native gave 0).

- 2026-11-15 (`programs`): an owned value that needs completion overwritten by `set`; two write views of one array; a write through a
  write view, or straight into the array, while a read view of it lives.

All were fixed the same day. In every case the two back ends had agreed on the wrong answer, so comparing them could not see it.

# basic_type_system conformance runner

`run.py` runs the conformance vectors of [basic_type_system](https://github.com/rubidus-api) (BTS) on both Lowent
implementations — the VM (`lowentc --run-batch`) and the native build (`lowentc --emit-c` + `cc`) — and writes a
`bts-results/2` file (harness protocol §5, copied in `conformance/harness-protocol.md`).

```sh
python3 impl/tests/bts/run.py                          # integer_ops → impl/build/bts/results.jsonl
python3 -I impl/tests/bts/conformance/check_results.py impl/build/bts/results.jsonl
```

- `conformance/` is a copy of BTS `docs/conformance/` (vectors, `check_results.py`, `gen.py`, `refmodel/`, `targets/`, MIT licence).
  `conformance/SOURCES.tsv` records each file's sha256 and the BTS commit it came from. Do not edit these files; take a new copy.
- Each case becomes one Lowent function; each row is one call. A row that must trap runs on its own.
- Operation names: the case's `names.lowent` when the vectors give it, otherwise a same-meaning spelling listed in
  `lowent_expr` (for example `wrap_sub 0 a` for a wrapping negation, `intops.cmp_lt` for the exact comparison).
  An operation Lowent does not have is skipped as `language-policy`.
- Build mode `debug` (keeps every check). Target `x86_64` (`usize` and `idiff` are 64 bits; rows for other widths are
  `target-unavailable`).

Suites so far: `integer_ops`. The first run (2026-11-13) found two Lowent defects, both fixed the same day: an unsigned
64-bit value inside an option or a struct printed as negative, and `intops.mod_trunc` stopped on the smallest value
divided by −1.

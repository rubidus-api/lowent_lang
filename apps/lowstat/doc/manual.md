# `lowstat` manual

## Usage

```
lowstat <file.csv | -> [--where COL=VALUE] (--count | --sum COL) [--group-by COL] [--top K]
```

- `-` reads standard input.
- `--count` or `--sum COL` — exactly one.
- `--where COL=VALUE` — keep rows whose decoded field equals `VALUE` byte for byte.
- `--group-by COL` — one result line per distinct value of `COL`.
- `--top K` — with `--group-by`, only the `K` largest results.

## Build

```sh
impl/build/lowentc --emit-c apps/lowstat/src/lowstat.low > /tmp/lowstat.c
cc -O2 -o /tmp/lowstat /tmp/lowstat.c -lm
```

The VM gives the same answer: `impl/build/lowentc --run main apps/lowstat/src/lowstat.low data.csv --count`.

## Input (the CSV dialect)

- UTF-8; invalid UTF-8 anywhere is an error. No encoding is guessed.
- Comma separator. A field may be wrapped in double quotes; inside quotes `""` is one quote
  and line breaks are part of the field.
- Records end with LF, CRLF or CR. The last record may lack a line break.
- A line with no bytes at all is skipped (as Python's `csv.DictReader` does).
- The first record is the header. Every data record must have as many fields as the header.
- A quote inside an unquoted field, or a character after a closing quote, is an error.
- A column named twice in the header is an error only when an option names it.

## Numbers

`--sum` reads a signed 64-bit integer: an optional `+` or `-` and one or more digits. Spaces,
an empty field and a decimal point are not numbers — the run stops with an error rather than
reading them as 0. A sum that leaves the signed 64-bit range is an error.

## Output

Without `--group-by`:

```
count
42
```

With `--group-by path --count`:

```
path,count
/a,3
/b,1
```

Keys are written as CSV fields (quoted when they contain a comma, a quote or a line break).
Groups are in byte order of the key. With `--top K`, they are in descending order of the value
and ties are in byte order of the key.

The result is written only after the whole input has been read and checked: a bad record
anywhere means no aggregate is printed.

## Exit codes

| code | meaning |
|---|---|
| 0 | done |
| 1 | data error: CSV syntax, field count, a non-integer or overflowing sum, an unknown or duplicated column, invalid UTF-8, no header |
| 2 | usage |
| 3 | a limit was reached |
| 4 | the input could not be opened or read, or the output could not be written |
| 70 | the buffers could not be allocated |

Errors go to standard error with the data record number (1 = the first record after the header).

## Limits

One record: 65,536 bytes · fields per record: 256 · groups: 65,536 · bytes of all group keys:
4 MiB · `--where` value: 4,096 bytes. The program allocates about 9.5 MiB once.

## Not in this version

SQL, regular expressions, several conditions, several files, floating-point sums and
external sorting for more groups than the limit.

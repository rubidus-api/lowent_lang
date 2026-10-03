# `lowindex` manual

## Usage

```
lowindex build  <index> <dir>
lowindex search <index> <word>...
```

## Build

```sh
impl/build/lowentc --emit-c apps/lowindex/src/lowindex.low > /tmp/lowindex.c
cc -O2 -o /tmp/lowindex /tmp/lowindex.c -lm
```

The VM gives the same answer: `impl/build/lowentc --run main apps/lowindex/src/lowindex.low search notes.lwx word`.

## What is indexed

- Regular files under `<dir>`, walked in byte order of name. Symbolic links are not followed.
- A file with a NUL byte in its first 64 KiB is treated as binary and skipped.
- Words: maximal runs of `0-9`, `A-Z`, `a-z` and bytes 0x80–0xFF. `A-Z` become `a-z`.
  Other bytes, including non-ASCII letters' case, are left as they are. A word longer than
  255 bytes is not indexed.
- Each document is listed once per word, however often the word appears.

`build` is the update command too: it rebuilds from the folder. The new index is written to
`<index>.tmp` and renamed over `<index>` only when it is complete. The rename is atomic on one
file system; it does not promise that the data survives a power failure.

## Searching

The query words are cut with the same rule (so `slice_u8` is one word and `RFC-0132` is two).
The result is every document that contains all of them, as paths relative to how `<dir>` was
written, in byte order. No hits is not an error.

## The index file

All numbers are little-endian 64-bit. Header (40 bytes): `LWIDX`, two zero bytes, version 1,
total length, document count, word count, FNV-1a checksum of everything after the header.
Then each document as [length · path], then each word in byte order as
[length · bytes · document count · document numbers]. `search` checks the magic, version, length
and checksum, and that the tables fit the file, before answering.

## Exit codes

| code | meaning |
|---|---|
| 0 | done (also when nothing matched) |
| 1 | the index is damaged or not an index |
| 2 | usage |
| 3 | a limit was reached (the message says which) |
| 4 | a file could not be opened, read, written or renamed |
| 70 | the buffers could not be allocated |

## Limits

Documents 65,536 · distinct words 524,288 · bytes of distinct words 16 MiB · word–document
pairs 4,194,304 · index read by `search` 64 MiB. `build` allocates about 160 MiB once and
`search` about 75 MiB; a folder whose files are full of unique strings (hashes, encoded data)
reaches the word limit long before the byte size suggests.

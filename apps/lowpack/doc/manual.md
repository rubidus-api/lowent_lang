# `lowpack` manual

## Usage

```
lowpack create  <archive.tar> <path>...
lowpack list    <archive.tar>
lowpack check   <archive.tar>
lowpack extract <archive.tar> <dir>
```

## Build

```sh
impl/build/lowentc --emit-c apps/lowpack/src/lowpack.low > /tmp/lowpack.c
cc -O2 -o /tmp/lowpack /tmp/lowpack.c -lm
```

The VM gives the same answer: `impl/build/lowentc --run main apps/lowpack/src/lowpack.low list site.tar`.

## What is stored

- Regular files and directories. Each path given to `create` is stored as written (relative
  paths only); a directory is stored with everything under it, entries in byte order of name.
- Mode 0644 for files and 0755 for directories, owner 0, group 0, time 0. The archive does
  not depend on who made it or when.
- Names up to 100 bytes, or up to 255 bytes when they can be split at a `/` into a prefix of at
  most 155 bytes and a name of at most 100 bytes. Longer names stop `create` with code 3.
- A symbolic link, a device or a pipe stops `create` with code 1 — it is not silently skipped.
- The archive ends with two zero blocks. If `create` fails, the partial archive is removed.

## Reading

`list` prints each entry name as stored (directories end with `/`), in archive order —
the same lines as `tar -tf`. `check` reads every header and every content block and prints
`ok N entries`.

A header is accepted when its checksum matches and its magic is `ustar` (POSIX, or the GNU
spelling). Type `0` (or NUL) is a file and `5` a directory. Every other type — symbolic and
hard links, devices, FIFOs, PAX headers (`x`, `g`) and GNU long names (`L`, `K`) — is an error.
The checksum catches accidental damage; it says nothing about who made the archive.

## Extracting safely

`extract` writes under `<dir>`, which must exist. It refuses, with code 1:

- an absolute name or a name with a `..` component,
- a path whose existing part passes through a symbolic link or a file,
- a file that already exists (it never overwrites; a second entry with the same name fails),
- a directory entry with a size, or an entry where a file would replace a directory.

Missing parent directories are created. A file that cannot be completely written is removed.
Entries before the failing one stay on disk — extraction is not transactional.
Modes, owners and times from the archive are not applied.

## Exit codes

| code | meaning |
|---|---|
| 0 | done |
| 1 | the archive is malformed or unsafe, or an input is not a file or directory |
| 2 | usage |
| 3 | a limit was reached (name length, entries per directory, pending directories) |
| 4 | a file could not be opened, read or written |
| 70 | the buffers could not be allocated |

## Limits

Entries per directory 4,096 · name bytes per directory 256 KiB · pending directories 4,096 ·
path 4,096 bytes. About 0.8 MiB is allocated once.

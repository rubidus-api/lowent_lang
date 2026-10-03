# `lowserve` manual

## Usage

```
lowserve <root> <port> [<max-requests>]
```

`port` 0 picks a free port. The first line on standard output is `listening on 127.0.0.1:<port>`.
With `<max-requests>`, the server stops after answering that many connections (used by tests);
without it, it runs until stopped.

## Build

```sh
impl/build/lowentc --emit-c apps/lowserve/src/lowserve.low > /tmp/lowserve.c
cc -O2 -o /tmp/lowserve /tmp/lowserve.c -lm
```

## Requests

- Methods `GET` and `HEAD`; others get `405` with `Allow: GET, HEAD`.
- Versions `HTTP/1.0` and `HTTP/1.1`; another `HTTP/x` gets `505`, anything else `400`.
- The request head (request line and headers) may be up to 8 KiB; more gets `431` and the
  connection is closed (a client still sending may see a reset instead of the response).
- Headers are read but not used. Request bodies are not read.
- Every response has `Content-Type`, `Content-Length` and `Connection: close`.

## Paths

1. The target is cut at `?` or `#`; `%xx` escapes are decoded. A malformed escape or a NUL
   byte is `400`.
2. The decoded path must start with `/` and must not contain a `..` segment (`400`).
3. Every component under the root is checked: if one is a symbolic link, the answer is `403`.
   This keeps links from leading outside the folder. (The check and the open are separate
   steps; a folder changed by someone else in between is not defended against.)
4. A directory is answered with its `index.html` (with or without a trailing `/` — there is no
   redirect). No `index.html` is `404`. Anything that is not a regular file is `404`.

Content types come from the extension: html/htm, txt/md, css, js, json, png, jpg/jpeg, svg,
pdf; everything else is `application/octet-stream`.

## Exit codes

| code | meaning |
|---|---|
| 0 | stopped after `<max-requests>` connections |
| 2 | usage, or the root is not a directory |
| 4 | the port could not be bound |
| 70 | the buffers could not be allocated |

## Limits and what is missing

Request head 8 KiB · path 4 KiB · about 86 KiB allocated once. No TLS, no keep-alive, no
ranges, no compression, no listings, no concurrency, no read timeout.

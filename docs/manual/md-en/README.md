# Lowent Manual

Systems Programming with Contracts and Effects

draft

v0.2.0 · last updated 2026-09-15

rubidus

- [rubidus@gmail.com](mailto:rubidus@gmail.com)
- [github.com/rubidus-api/lowent_lang](https://github.com/rubidus-api/lowent_lang)

An introduction to the Lowent language, and its user manual.

Written for readers who have programmed a little,  
up to those who already use a systems language such as C or Rust.

This edition is a **draft**. For the paginated index and exact typesetting, see the [PDF](../pdf-en/lowent-manual-en.pdf).

If anything in the book leaves you wondering, please ask on the [**Q&A board**](https://github.com/rubidus-api/lowent_lang/discussions/categories/q-a). I answer as time allows and as far as I know the answer. Korean or English, either is fine. For typos and mistakes, an [issue](https://github.com/rubidus-api/lowent_lang/issues) is the better place.

## Contents

- [A note on this translation](sec01.md)
- [Preface](sec02.md)
- [How to read this book](sec03.md)
- [Detailed contents →](toc.md)

### Part I — Getting started

- [1 What Lowent sets out to do](ch01.md)
- [2 A first program — build, run, get rejected](ch02.md)
- [3 The surface — full stops, blocks and clause order](ch03.md)

### Part II — Values and flow

- [4 Numbers — fixed-width integers and floating point](ch04.md)
- [5 Ops — fn and proc](ch05.md)
- [6 Locals — let and var](ch06.md)
- [7 Flow — branches, loops and leaving early](ch07.md)
- [8 Expressions — prefix notation and the expr island](ch08.md)

### Part III — Data

- [9 Sequences — arrays and slices](ch09.md)
- [10 Aggregates — struct and enum](ch10.md)
- [11 Types that hold answers — option and result](ch11.md)
- [12 Borrowing — ref and mut_ref](ch12.md)
- [13 Named types — type, newtype, range, cast](ch13.md)

### Part IV — Contracts and effects

- [14 Contracts — write them, have them checked, lose the checks](ch14.md)
- [15 Effects — the marks an op leaves on the world](ch15.md)
- [16 Capabilities — power that is handed over](ch16.md)
- [17 Designing failure](ch17.md)

### Part V — Memory

- [18 Regions — where values live, and memory reclaimed all at once](ch18.md)
- [19 Ownership — one party responsible for disposal](ch19.md)
- [20 Allocators and fixed memory](ch20.md)

### Part VI — Abstraction

- [21 Modules — hidden by default](ch21.md)
- [22 Generics — parameters fixed at translation time](ch22.md)
- [23 Traits — one promise kept by many types](ch23.md)
- [24 pipe — one line for each thing you mean to do](ch24.md)

### Part VII — Concurrency

- [25 Actors — living with state, by messages](ch25.md)
- [26 Tasks and channels — exchange between bound flows](ch26.md)
- [27 Parallel loops and atomic operations](ch27.md)

### Part VIII — The outside world

- [28 Input, output and files](ch28.md)
- [29 Meeting C](ch29.md)
- [30 Hardware — registers, interrupts, machine instructions](ch30.md)
- [31 Building and testing — packages, configuration, tests, cross-checks](ch31.md)

### Part IX — The standard library

- [32 A map of the standard library](ch32.md)
- [33 Text and encodings — strings, fmt, utf8, codec, hash](ch33.md)
- [34 Containers and sorting — sortlib, sortgen, hashmap, vecgen, spsc](ch34.md)
- [35 Storage and handles — pool, shard, budget, wire](ch35.md)
- [36 Input/output, networking, time, randomness, cryptography](ch36.md)
- [37 The terminal — term and tty](ch37.md)

### Part X — Grounds: what has been proven

- [38 Why prove](ch38.md)
- [39 The mathematical toolkit](ch39.md)
- [40 Proofs about numbers — widening, narrowing, division](ch40.md)
- [41 Proofs about bounds — intervals, relations, row-major addresses](ch41.md)
- [42 Proofs about ownership and borrowing](ch42.md)
- [43 Loops and fixed points — proving “however many times”](ch43.md)
- [44 Proofs about effects — writing “what it can do” into the type](ch44.md)
- [45 Proofs about races and parallelism — discipline instead of a memory model](ch45.md)
- [46 Proofs about weak memory — with the default, you may think sequentially](ch46.md)
- [47 Proofs about locks — where heavy tools are really needed](ch47.md)
- [48 Proofs about syntax — whichever closer closes it, the same tree](ch48.md)
- [49 Proofs about hashes — calling things by content, not name](ch49.md)
- [50 What is not proven](ch50.md)

### Front and back matter

- [Appendix A — Words and builtins](sec54.md)
- [Appendix B — Diagnostics index](sec55.md)
- [Appendix C — Common mistakes and how to fix them](sec56.md)
- [Appendix D — Grammar summary](sec57.md)
- [Appendix E — Standard library modules](sec58.md)
- [strings — string view operations](sec59.md)
- [strbuf — owned string buffer and null-terminated cstr](sec60.md)
- [fmt — formatting assembled into the caller’s buffer](sec61.md)
- [utf8 — UTF-8 code point iteration and validation](sec62.md)
- [utf16 — UTF-16 surrogate arithmetic](sec63.md)
- [unicode — Unicode property tables](sec64.md)
- [codec — hex and base64](sec65.md)
- [regex — Pike VM regular expressions without backtracking](sec66.md)
- [term — terminal renderer (the pure half)](sec67.md)
- [sortlib — in-place quicksort](sec68.md)
- [sortgen — generic sorting (the type brings the comparison)](sec69.md)
- [searchlib — binary search on sorted slices](sec70.md)
- [hashmap — u64 → u64 hash map](sec71.md)
- [strmap — string-keyed hash map](sec72.md)
- [vecs — growable byte vectors with caller-held buffers](sec73.md)
- [spsc — lock-free SPSC ring buffer](sec74.md)
- [hash — hashing (hash map slots · corruption detection · SHA-256)](sec75.md)
- [math — floating-point maths](sec76.md)
- [random — random numbers (reproducible sequences · OS entropy)](sec77.md)
- [hmac — HMAC-SHA256 and HKDF](sec78.md)
- [chacha — ChaCha20 stream cipher](sec79.md)
- [poly — Poly1305 one-time authenticator](sec80.md)
- [aead — ChaCha20-Poly1305 seal and open](sec81.md)
- [x25519 — key agreement on a curve](sec82.md)
- [aes — the AES-128 block cipher](sec83.md)
- [gcm — AES-128-GCM authenticated encryption](sec84.md)
- [bigint — big-number modular arithmetic](sec85.md)
- [rsa — RSASSA-PSS verification](sec86.md)
- [p256 — the NIST P-256 curve and ECDSA verification](sec87.md)
- [ecdsa — ECDSA P-256 signing](sec88.md)
- [ed25519 — Ed25519 signature verification](sec89.md)
- [der — minimal DER parser](sec90.md)
- [pem — unwrapping PEM envelopes](sec91.md)
- [tls13 — the computational parts of TLS 1.3](sec92.md)
- [tlssrv — the TLS 1.3 server handshake](sec93.md)
- [http — HTTP/1.1 request parser](sec94.md)
- [soa — an SoA layout trial: one array per field](sec95.md)
- [allocs — allocator traits, bumps and default allocators](sec96.md)
- [pool — generational-handle block pool](sec97.md)
- [shard — access units that split a store](sec98.md)
- [budget — handle bit budgets and generation wraparound](sec99.md)
- [wire — dividing one word into fields](sec100.md)
- [flags — named on/off settings in one word](sec101.md)
- [segarena — fixed-size segment arena](sec102.md)
- [pagecache — page ids and pinning cursors](sec103.md)
- [growvec — self-growing byte vector](sec104.md)
- [vecgen — generic self-growing vector vec t a](sec105.md)
- [mapgen — generic hash map table k v](sec106.md)
- [nodelist — fixed intrusive lists](sec107.md)
- [segview — cursors, total length and coalescing for segment views](sec108.md)
- [lifemode · lifeatom — when a value ends](sec109.md)
- [io — stream reading over slices](sec110.md)
- [outbuf — buffered output where forgetting to flush is a compile error](sec111.md)
- [files — file and directory streams where forgetting to close is a compile error](sec112.md)
- [tty — terminal input](sec113.md)
- [net — sockets (TCP loopback · in-process pairs)](sec114.md)
- [clock — time and deadlines](sec115.md)
- [Index](sec116.md)

## Copyright and contact

author

rubidus

contact

[rubidus@gmail.com](mailto:rubidus@gmail.com)

repository

[github.com/rubidus-api/lowent_lang](https://github.com/rubidus-api/lowent_lang)

edition

v0.2.0 — draft

last updated

2026-09-15

**The text** — Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0). You may share and adapt it freely with attribution; commercial use is not permitted, and adaptations must carry the same licence.  
[creativecommons.org/licenses/by-nc-sa/4.0/](https://creativecommons.org/licenses/by-nc-sa/4.0/)

**The example code** — MIT licence. Take it and use it. The Lowent compiler and standard library used by the examples follow the repository's MIT licence.

Every code demonstration in this book is output actually obtained by checking and running it with lowentc. The typesetting is done with Typst.

This book keeps being revised. What you are reading is the edition numbered above; corrections and additions follow it. Reports and suggestions are taken at the repository.

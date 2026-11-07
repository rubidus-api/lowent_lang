# Appendix E — Standard library modules

Part IX (chapters 32–37) toured the standard library by use. This appendix is a **reference**, one page per module, gathering what each does, which ops it offers and what it does not do. It is not a place to read from the start but a place to open when needed.

Module pages share one shape. The head gives the source file, layer and capabilities received, followed by what the module does, its ops at a glance, how to use it, design and boundaries, and what is not built. The maturity each module writes (`experimental`, `incubating`, `standard`) is authoritatively in the source head and the development repository’s ledger and moves between editions, so it is not copied here. The conventions every module follows — the caller’s buffer, failure as a value, all or nothing, ownership that must be settled — are in chapter 32.

Module names may differ from file names. What `use` looks for is the `module` declaration in the file (`lib/str.low` is `strings`, `lib/alloc.low` is `allocs`). Cross-module names are always called qualified (`strings.find`), and a frequently used one can get an alias with `use codec as c .`.

| **Module** | **In one line** |
|---|---|
| [`strings`](sec59.md#mod-strings) | String view operations — cut, find, compare |
| [`strbuf`](sec60.md#mod-strbuf) | Owned string buffer and null-terminated `cstr` |
| [`fmt`](sec61.md#mod-fmt) | Formatting assembled into the caller’s buffer |
| [`utf8`](sec62.md#mod-utf8) | UTF-8 code point iteration and validation |
| [`utf16`](sec63.md#mod-utf16) | UTF-16 surrogate arithmetic |
| [`unicode`](sec64.md#mod-unicode) | Unicode property tables — extracted mechanically |
| [`codec`](sec65.md#mod-codec) | Hex · base64 |
| [`regex`](sec66.md#mod-regex) | Pike VM regular expressions without backtracking |
| [`term`](sec67.md#mod-term) | The pure half of a terminal renderer — ANSI assembly · screen diff · width |
| [`sortlib`](sec68.md#mod-sortlib) | In-place quicksort for `u64` |
| [`sortgen`](sec69.md#mod-sortgen) | Generic sort — the type brings the comparison |
| [`collection`](sec70.md#mod-collection) | Map, filter and fold over a slice — the element type brings the work |
| [`searchlib`](sec71.md#mod-searchlib) | Binary search over sorted slices |
| [`hashmap`](sec72.md#mod-hashmap) | `u64 → u64` open addressing |
| [`strmap`](sec73.md#mod-strmap) | Byte string → `u64` hash map |
| [`vecs`](sec74.md#mod-vecs) | Growing byte vector |
| [`spsc`](sec75.md#mod-spsc) | Lock-free single-producer single-consumer ring buffer |
| [`hash`](sec76.md#mod-hash) | Hash-map slots (FNV-1a) · damage detection (CRC-32) · SHA-256 · SHA-384 · SHA-512 |
| [`hash_legacy`](sec77.md#mod-hash_legacy) | MD5 · SHA-1 — checksums and reading old files. Not for security |
| [`math`](sec78.md#mod-math) | Floating-point maths — `close` instead of `==` |
| [`random`](sec79.md#mod-random) | Separates reproducible sequences from OS entropy by name |
| [`hmac`](sec80.md#mod-hmac) | HMAC-SHA256 · HKDF — who sent it |
| [`chacha`](sec81.md#mod-chacha) | ChaCha20 stream — not safe alone |
| [`poly`](sec82.md#mod-poly) | Poly1305 — a fresh key per message |
| [`aead`](sec83.md#mod-aead) | ChaCha20-Poly1305 seal and open — cryptography starts here |
| [`x25519`](sec84.md#mod-x25519) | Key agreement |
| [`aes`](sec85.md#mod-aes) | AES-128 block cipher — usually wrong used alone |
| [`gcm`](sec86.md#mod-gcm) | AES-128-GCM — never repeat a nonce |
| [`bigwidth`](sec88.md#mod-bigwidth) | The trait of big-number widths — pick the width on each call |
| [`bigint16`](sec89.md#mod-bigint16) | Big-number modular arithmetic — 16-bit limbs (narrow multipliers) |
| [`bigint32`](sec90.md#mod-bigint32) | Big-number modular arithmetic — 32-bit limbs (32-bit multipliers) |
| [`bigint64`](sec91.md#mod-bigint64) | Big-number modular arithmetic — 64-bit limbs (64-bit machines) |
| [`bigint`](sec92.md#mod-bigint) | Big-number modular arithmetic — the machine picks the width (measured or multiplier width · `--limb-bits`) |
| [`rsa`](sec93.md#mod-rsa) | RSASSA-PSS verification |
| [`p256`](sec94.md#mod-p256) | NIST P-256 curve and ECDSA verification |
| [`p384`](sec95.md#mod-p384) | NIST P-384 curve and ECDSA verification — the public web’s intermediates |
| [`ecdsa`](sec96.md#mod-ecdsa) | ECDSA P-256 signing — nonce derived without randomness |
| [`ed25519`](sec97.md#mod-ed25519) | Ed25519 signature verification |
| [`der`](sec98.md#mod-der) | Minimal DER parser — extracts public keys only |
| [`pem`](sec99.md#mod-pem) | Unwrapping PEM envelopes |
| [`x509`](sec100.md#mod-x509) | Reading X.509 certificates — offsets and lengths only |
| [`verify`](sec101.md#mod-verify) | Certificate signatures and one link of a chain |
| [`tls13`](sec102.md#mod-tls13) | TLS 1.3 parts — key schedule · records · transcript · Finished |
| [`tlssrv`](sec103.md#mod-tlssrv) | Server TLS handshake and application data records |
| [`tlscli`](sec104.md#mod-tlscli) | Client TLS 1.3 handshake — the caller checks certificates |
| [`http`](sec105.md#mod-http) | HTTP/1.1 request parser — its heart is rejection |
| [`soa`](sec106.md#mod-soa) | A trial of laying structs out as per-field arrays |

*Table 50.1 — L0 — pure computation (`effects none` · the caller’s buffer)*

| **Module** | **In one line** |
|---|---|
| [`allocs`](sec107.md#mod-allocs) | Allocator trait · bump allocators · two default allocators |
| [`pool`](sec108.md#mod-pool) | Generational-handle block pool |
| [`shard`](sec109.md#mod-shard) | Tokens splitting storage into non-overlapping pieces |
| [`budget`](sec110.md#mod-budget) | Bit budgets for handles and retiring generations |
| [`wire`](sec111.md#mod-wire) | Fields in one word — a mask carries position and width |
| [`flags`](sec112.md#mod-flags) | Named on/off settings in one word |
| [`segarena`](sec113.md#mod-segarena) | Fixed-size segment arena |
| [`pagecache`](sec114.md#mod-pagecache) | Page ids and pin tokens |
| [`growvec`](sec115.md#mod-growvec) | Growing byte vector — short name for `vecgen.vec u8` |
| [`vecgen`](sec116.md#mod-vecgen) | Generic growing vector `vec t` |
| [`mapgen`](sec117.md#mod-mapgen) | Generic hash map `table k v` |
| [`nodelist`](sec118.md#mod-nodelist) | Fixed-size intrusive list |
| [`segview`](sec119.md#mod-segview) | Cursor, total length and flattening for scattered views |
| [`lifemode`](sec120.md#mod-lifemode) | `lifemode` · `lifeatom` — when a value ends |

*Table 50.2 — L1 — storage (needs the allocation capability or borrowed bytes)*

| **Module** | **In one line** |
|---|---|
| [`io`](sec124.md#mod-io) | Stream reading over slices |
| [`outbuf`](sec125.md#mod-outbuf) | Buffered output — forgetting to flush is rejected |
| [`files`](sec126.md#mod-files) | Files and directories — forgetting to close is rejected |
| [`tty`](sec127.md#mod-tty) | Terminal input — raw mode · reading keys · pure key parsing |
| [`net`](sec129.md#mod-net) | Sockets — TCP loopback · in-process connection pairs |
| [`clock`](sec130.md#mod-clock) | Time and deadlines — monotonic and wall clocks |
| [`trust`](sec131.md#mod-trust) | Finding a trusted root in the trust store — the bundle is streamed |

*Table 50.3 — L2 — host (needs capabilities)*

The cryptographic modules are stacked in layers towards TLS 1.3; that order and the top that does not exist yet are in chapter 36.

---

[← Prev](sec57.md) · [Contents](README.md) · [Next →](sec59.md)

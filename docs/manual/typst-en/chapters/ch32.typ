#import "../../typst-ko/lib.typ": *

= A map of the standard library

#chapter-toc()

#prereq(
  ([#chref("modules"), Modules], [standard library names are brought in with `use <name> .`]),
  ([#chref("capabilities"), Capabilities], [capabilities are handed over, not picked up]),
  ([#chref("errors-design"), Designing failure], [at the boundary, failure is returned as a value]),
)

#deepqa[
  In #chref("modules"), does `use` look for a file name or a module name? Why did it say the difference matters in the standard library?
][
  The `module` declaration inside the file. In the standard library a few file names differ from module names (`lib/alloc.low` is `allocs`, `lib/str.low` is
  `strings`), so trying to import by file name leads astray. This chapter's first table is that list.
]

#why[
  Part IX tours the standard library. Detailed descriptions of each module live in Appendix E, one page each, and this part does not repeat them. Instead it
  sets out *which module sits on which layer, which conventions they all follow, and where to look for a given job*. This first chapter is the map --- the boundary
  between language and library, the criteria for entering the library, and the layers and shared conventions of modules.
]

#organizer[
  You will learn the question that separates the three layers of language, leaf and library. You will pick up the criteria (the charter) and maturity ladder for
  entering the standard library, and a map dividing modules into a layer that does not allocate, a layer needing storage and a layer needing capabilities. You will
  also see the shared conventions --- the caller's buffer, failure returned as a value, all or nothing, ownership that must be settled --- and the places where file
  names and module names differ.
]

#chapter-questions()

== Three layers --- language, leaf, library

#dtable(
  columns: 3,
  id: "libmap-layers",
  caption: [Three layers],
  [*Layer*], [*Who makes it*], [*Qualification*],
  [Language], [The specification], [Cannot be expressed as a library, or expressing it would hide a cost],
  [Leaf], [The processor], [Reaches the operating system or hardware. Cannot be written in Lowent],
  [Library], [Anyone], [Everything else. Written in Lowent],
)

#idx("leaf")
There is one question deciding whether a piece is a leaf --- "can this be written in Lowent?" If it can, it is library. Opening a file and receiving an integer handle
reaches the kernel, so it is a leaf. Wrapping that handle in an owned value that cannot be forgotten can be written in Lowent, so it is library. A convenience op that
reads a whole file is also library. *Convenience does not qualify something as a leaf.*

The standard library has no privileges. It gets the same rules as code the author writes --- contracts, effects, capabilities. Here is the source of
`strings.starts_with`.

```lowent
export fn starts_with input s str . input prefix str . output bool . do
  guard le (len prefix) (len s) . else return false .
  return eq_str (subslice s 0 (len prefix)) prefix .
end .
```

It reads with only what this book has taught. If there were a hidden passage only the library could use, a program's guarantees would break the moment it passed
through.

#qa[
  Why leave as a library a feature that would be convenient in the language?
][
  Adding a word or builtin to the language means every program must learn it and the processor must treat it specially. As a library it is checked by the same rules,
  and programs that do not use it do not have it. So the language accepts only *what cannot be expressed*. The standard library's allocators, containers and file
  handles all passed this criterion and live in the library. Convenience is not a reason to enter the language.
]

== What gets in

Things enter the library not *because they exist* but *on evidence*. The specification's charter sets six measures.

#dtable(
  columns: 2,
  id: "libmap-charter",
  caption: [The library charter],
  [*Measure*], [*Meaning*],
  [If it can be written in this language, write it in this language], [Leaves only when using outside authority or when this language cannot express it],
  [The signature is the ledger of cost and authority], [Effects, capabilities, storage, allocation, ownership and completion are visible at the boundary],
  [Separate computation from authority], [Formatting vs. emitting, parsing vs. opening files, the next step of a random generator vs. getting entropy are different],
  [Place along several axes], [Authority, storage, execution tier and profile are not squeezed into one ladder],
  [Keep what is inside small], [Up to a portable base and a thin host adapter],
  [Every module has evidence], [Unit tests, rejection tests, cross-checks and failure tests],
)

#idx("maturity")
Every module writes its *maturity* --- `experimental` (merely written), `incubating` (has a decision document), `standard` (documented and named by regression tests)
and `deprecated` (says what to move to). The value of the ladder is in the lower rungs, not the upper. Writing `experimental` writes "do not trust this yet" *in the
source*. A rung is not a ranking of quality but the size of a promise.

The specification does not list the library's *contents*. What exists is authoritatively recounted from the source each time, and a document copying it diverges from
the moment it copies. Three APIs that did not actually exist were once read as fact because they were in a table. The tables in this part were also made by looking at
this edition's source, and detailed per-module pages are in Appendix E.

== The map by layer

#dtable(
  columns: 3,
  id: "libmap-tiers",
  caption: [Layers of the standard library],
  [*Layer*], [*Character*], [*Representative modules*],
  [L0 pure computation], [`effects none` · the caller's buffer], [`strings` `strbuf` `fmt` `utf8` `utf16` `unicode` `codec` `regex` `hash` `math` `random` `sortlib` `sortgen` `searchlib` `hashmap` `strmap` `spsc` `term` · cryptographic modules],
  [L1 storage], [Needs the allocation capability or borrowed bytes], [`allocs` `pool` `shard` `budget` `segarena` `pagecache` `growvec` `vecgen` `mapgen` `nodelist` `segview` `wire` `flags` `lifemode`],
  [L2 host], [Needs capabilities (`cap io`, `file_system`, `tty`, `net`, `clock`)], [`io` `outbuf` `files` `tty` `net` `clock`],
)

The remaining chapters of this part group by use: text and encodings (#chref("lib-text")), containers and sorting (#chref("lib-containers")), storage and handles
(#chref("lib-alloc")), input/output, networking and cryptography (#chref("lib-io-net")), and the terminal (#chref("lib-terminal")).

== File names and module names

What `use` looks for is the `module` declaration inside the file. In a few places it differs from the file name.

#dtable(
  columns: 2,
  id: "libmap-names",
  caption: [Module names that differ from file names],
  [*Source file*], [*Module name*],
  [`lib/alloc.low`], [`allocs`],
  [`lib/str.low`], [`strings`],
  [`lib/vec.low`], [`vecs`],
  [`lib/sort.low`], [`sortlib`],
  [`lib/search.low`], [`searchlib`],
  [`lib/out.low`], [`outbuf`],
  [`lib/file.low`], [`files`],
)

#demo("examples/ch32/names.low")

`use strings .` resolves from the standard module location without `from`. Imported names are always called qualified by module, as in `strings.starts_with`. There is
no glob import. Importing by file name finds no such module.

#demo("examples/ch32/wrong_name.low")

The diagnostic is a warning rather than an error because there is no search path, so looking at this translation unit alone cannot tell whether the name exists
somewhere (#chref("modules")). The moment the name is actually used, it is rejected.

== Conventions every module follows

#dtable(
  columns: 2,
  id: "libmap-conventions",
  caption: [Shared conventions of the standard library],
  [*Convention*], [*Meaning*],
  [Buffers belong to the caller], [L0 modules do not allocate. They take output buffers and workspace as parameters. So reentrancy is free and the VM/native cross-check runs as is],
  [Failure is a value], [`option` (could not) or `result` (what failed). No traps, no silent truncation, no replacement characters],
  [All or nothing], [If there is not enough room, not a single byte is written. A half-written buffer is silently wrong output],
  [Ownership must be settled], [Things needing completion (`outbuf`'s remaining bytes, `files`' handles) are owned values, so forgetting them is rejected at translation],
)

#misconception[It is the standard library, so every module can be trusted equally][
  Maturity differs per module, and the top of each module document states what it *does not do*. For example, `tlssrv` performs the whole handshake but not yet
  transport, and `der` is a minimal parser extracting only public keys, not a certificate infrastructure (PKI). Reading the boundaries a module writes for itself before
  importing it is how this library is meant to be used.
]

== The list of leaf ops --- what lies beneath the modules

Library modules ultimately call *leaf* builtin ops provided by the processor. Normally you use the module and do not call the leaves directly
--- the module dresses them in conventions such as ownership, three-place answers and all-or-nothing. Still, knowing which module stands on
what keeps you oriented when reading module documentation or building a new module.

#dtable(
  columns: 3,
  id: "libmap-leaves",
  caption: [Leaf builtin ops and the modules that wrap them],
  [*Leaf op*], [*What it does*], [*Wrapping module · capability*],
  [`file_open` · `file_read` · `file_write` · `file_seek` · `file_close`], [open, read, write, move the position of, and close a file], [`files` · `cap file_system`],
  [`dir_make` · `dir_read` · `dir_close`], [make a directory, read its entries, close it], [`files` · `cap file_system`],
  [`path_remove` · `path_rename` · `link_type`], [remove or rename a path, and ask what the path itself is without following symlinks], [`files` · `cap file_system`],
  [`net_listen` · `net_accept` · `net_connect` · `net_port`], [listening socket · accept · connect to an address and port · ask the port], [`net` · `cap net`],
  [`net_resolve`], [a name to an IPv4 address --- DNS. A *separate* leaf from connecting, so a program that already has an address never touches name resolution], [`net` · `cap net`],
  [`net_send` · `net_recv` · `net_close` · `net_pair`], [send · receive · close · a connected pair], [`net` · `cap net`],
  [`reactor_new` · `r_read` · `r_write`], [make a reactor · read through a reactor · write through a reactor], [no module --- `cap io`],
  [`env_get`], [read an environment variable], [no module --- `cap env`],
  [`rng_next`], [next random number from a state (splitmix64)], [`random` --- no capability (pure)],
  [`hash_bytes` · `crc32`], [FNV-1a 64 hash · CRC-32 checksum], [`hash` --- no capability],
  [`sha256` · `sha384` · `sha512`], [SHA-256 · SHA-384 · SHA-512 --- SHA-384 is SHA-512 with a different start value, keeping the first 48 bytes], [`hash`·`hmac`·`tls13` · `ed25519` --- no capability],
  [`aes_ctr` · `ghash`], [AES-128-CTR keystream · GHASH accumulation --- key, counter and accumulator are all 16 bytes. The counter and the accumulator are updated *in place*, so successive calls continue where the last one stopped], [`gcm` --- no capability],
  [`chacha_poly`], [one round of ChaCha20-Poly1305 --- keystream and accumulation in *one pass*. Its meaning is exactly `chacha20` followed by `poly1305` over the ciphertext it just made. The two computations use different execution resources (vector vs the integer multiplier), so interleaving overlaps them (measured 1,080 → 1,199 MB/s)], [`aead` --- no capability],
  [`aes_gcm`], [one round of AES-128-GCM --- keystream and accumulation in *one pass*. Its meaning is exactly `aes_ctr` followed by `ghash` over the ciphertext it just made; only the speed differs (measured 2848 → 3661 MB/s). Sealing only --- opening must attest the tag before it unseals, so it stays two passes], [`gcm` --- no capability],
  [`chacha20` · `poly1305`], [ChaCha20 keystream · Poly1305 accumulation --- key 32, counter block 16 (its first four bytes are the block number, little-endian), and the counter moves *in place*. `poly1305` accumulates into the state (five limbs of h, five of r) and zero-pads a tail that is not a multiple of sixteen. The width is chosen by the build (`lowentc --hw sse2` · `avx2`)], [`chacha` · `poly` · `aead` --- no capability],
  [`aes_round` · `aes_round_last`], [one AES round --- the 16-byte state in place. When the machine has the instruction the processor lowers to it (`lowentc --hw aes`); when it does not, to the same answer computed from tables], [`aes` --- no capability],
  [`str_from_cstr`], [scan a NUL-terminated C string into a `str`], [C boundary --- the VM says it cannot, with `E-VM-CSTR`],
)

*Computation leaves stand only after `call_builtin`* --- `call_builtin sha256 msg out`. The name lives in that position and never becomes a global word: a word a program uses once should not cost every reader a name to remember. The stage names inside `pipe` and the type slot of `cast u8 x` already work this way. Leaves that touch the operating system (`file_open`, `net_send`, …) are called plainly --- their first operand is a capability, so their specialness already shows.

The qualification for a leaf is one question: "can it be written in Lowent?" `rng_next` and the three hashes are pure computation yet leaves,
because the processor fixes their algorithms so that the VM and the native build give bit-identical answers. Every leaf that needs a
capability takes it as the *first operand* (#chref("capabilities")).

== Common mistakes

#antipattern[Chaining writes that return `option` without asking][
  #demo("examples/ch32/mistake_chainnone.low")

  The write ops of `fmt` return the next position as an `option u64`. With a twelve-slot buffer all three succeed and return 8, but with six
  slots `put_u64` runs out of room, returns `none`, and `some_value` stops. Failure in L0 modules is a *value*, not a trap, so the receiver
  asks at every step.

  #demo("examples/ch32/chainnone_fixed.low")

  The fixed version returns 0 with the six-slot buffer. Notice that `hello ` remains in the buffer. "All or nothing" is the promise of *a
  single op call*, not of a whole sequence of calls. If the whole sequence must be undone, treat only the positions that succeeded (`p`,
  `q`) as the valid length.
]

#misconception[When the buffer is short, as much as fits is written][
  #demo("examples/ch32/all_or_nothing.low")

  C's `snprintf` writes what fits and truncates. The L0 modules of the standard library are *all or nothing*. Asked to write six bytes into
  four slots, it writes no byte at all and returns `none`, leaving the buffer as `[0,0,0,0]`. A half-written buffer becomes silently wrong
  output, and the caller can rely on "none means the buffer is untouched" to continue down another path.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "lib-map-glance",
  caption: [Shapes for using the standard library --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`use strings .`], [import a standard module --- the name is the file's `module` declaration], [the module in `lib/str.low` is `strings`],
  [`strings.starts_with line "GET "`], [imported names are qualified by module], [no glob imports],
  [`fmt.put_str buf pos s` → `option u64`], [write into the caller's buffer and return the next position], [L0 never allocates --- reentrancy is free],
  [`none` (not enough room)], [not a single byte was written], [all or nothing --- the promise of one call],
  [`result t e`], [a failure that says what failed], [no traps, no silent truncation],
  [`owned` handles · pending bytes], [ownership that must be repaid], [forgetting it is rejected at translation],
  [`experimental` · `incubating` · `standard` · `deprecated`], [maturity each module declares], [a rung is the size of a promise, not a ranking],
)

#recap[
  The language takes only what cannot be expressed, leaves only thin pieces touching the outside, and the rest is library under the same rules. Libraries enter on the
  charter's evidence and write their maturity in the source. Modules divide into pure computation (L0), storage (L1) and host (L2) layers. Module names may differ from
  file names. Every module follows the caller's buffer, failure as a value, all or nothing, and ownership that must be settled.
]

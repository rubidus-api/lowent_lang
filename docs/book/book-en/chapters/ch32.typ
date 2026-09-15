#import "../../book/lib.typ": *

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
  Part IX tours the standard library. Detailed descriptions of each module live in the repository's `docs/manual/lib/`, and this part does not copy them. Instead it
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
this edition's source, and detailed per-module documents are in the repository's `docs/manual/lib/`.

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

#recap[
  The language takes only what cannot be expressed, leaves only thin pieces touching the outside, and the rest is library under the same rules. Libraries enter on the
  charter's evidence and write their maturity in the source. Modules divide into pure computation (L0), storage (L1) and host (L2) layers. Module names may differ from
  file names. Every module follows the caller's buffer, failure as a value, all or nothing, and ownership that must be settled.
]

#import "../../typst-ko/lib.typ": *

= Proofs about hashes --- calling things by content, not name

#chapter-toc()

#prereq(
  ([#chref("build-test"), Building and testing], [builds are cached by the content hash of emitted C, and dependencies are pinned by hash]),
  ([#chref("surface"), The surface], [the clause order of a head is fixed to one]),
  ([#chref("proofs-syntax"), Proofs about syntax], [two spellings with the same meaning become the same tree]),
)

#deepqa[
  In #chref("build-test"), what did `--lock` refuse to build over even when the version number is the same?
][
  Different bytes. Even with the same version number, changed content is a different dependency. This chapter covers how to write that "content" so that *the same
  program* is recognised by meaning rather than bytes, and what can be proven about how it is written.
]

#why[
  If #chref("proofs-syntax") was about two spellings building the same tree, this chapter is about whether two programs *are the same*. If two programs a cache judges
  "the same" differ in meaning, the cache lies. Conversely, judging programs with the same meaning "different" rebuilds needlessly. Reproducible builds, incremental
  builds and dependency pinning all rest on this judgement. The hash function itself is an unprovable cryptographic assumption, but what comes *before* it --- what goes
  into the hash and what is left out --- can be proven.
]

#organizer[
  You will learn the assumption of collision resistance, the Merkle DAG in which hashes contain hashes, and how ops calling each other are grouped into one bundle to
  remove cycles. You will check against real output how the two hashes of `--emit-db` (`iface` and `def`) react to comments, bodies, effect order and contracts, and
  understand what the injectivity theorem of the canonical encoding guarantees and why the imprecision of not normalising contract clauses is the safe direction.
]

#chapter-questions()

== Hash functions and Merkle DAGs

The hash function BLAKE3-256 reduces bytes of any length to 32 bytes. One property is needed --- *no pair of different inputs giving the same output can be found*
(collision resistance). So "the hashes are equal" may be used as "the contents are equal". This is not proven mathematically but an *assumption*, and it enters the
trusted base (#chref("proofs-limits")). Still, in practice it is sturdier than any other assumption.

== Called by content, not by name

Ordinary build systems judge caches by file names and modification times. So a file touched with unchanged content is rebuilt needlessly (a loss), and changed content with the
#idx("content addressing")
same time uses a stale result (a danger). *Content addressing* drops names and calls things by the hash of their content. `lowentc --emit-db` gives two hashes per op --- `iface`,
the surface observable from outside (signature and contracts), and `def`, the whole definition. The hash function is BLAKE3-256.

#idx("Merkle DAG")
A hash embeds its dependencies *by their hashes*. Then a definition's hash reflects the content of everything it depends on, and when one op deep down changes, the hashes above
change along with it. The hash itself is dependency tracking. This is a Merkle DAG.

Ops calling each other create cycles. If `hash(A)` contains `hash(B)` and `hash(B)` contains `hash(A)`, the definition goes round. So the cyclic ones are grouped
into *one bundle* (a strongly connected component, SCC). A nameless preliminary hash fixes the canonical order within the bundle, calls inside the bundle carry that
order's number instead of a name, the bundle is hashed once, and each member's hash is (bundle hash, its number). Contracting strongly connected components leaves an
acyclic graph --- the same idea as #chref("proofs-math")'s fixed points appears again.

```text
 A ⇄ B (they call each other)      C ──▶ A

 1  group {A, B} into one block (an SCC)
 2  order the block with nameless preliminary hashes        A = 0 · B = 1
 3  calls inside the block carry the number, not the name   A calls B → "number 1"
 4  hash the block once                                     → H
 5  hash each member as (H, its number)                     hash(A) = h(H, 0) · hash(B) = h(H, 1)
    C carries hash(A) — the graph that is left has no cycles
```

== What changes a hash and what does not

#demo("examples/ch49/h1_plain.low")

#demo("examples/ch49/h1_comment.low")

*H1 --- comments change nothing.* Both hashes are the same. Fixing a comment does not rebuild the whole project. And a machine keeps the definition of "the same program" as meaning,
not bytes.

#demo("examples/ch49/h2_body.low")

*H2 --- changing only the body changes only `def`.* Changing `add a. a. .` to `mul a. 2 .` left `iface` as it was and changed only `def`. This is the heart of incremental builds. When only
an op's body changes, its users are recompiled, but their `iface` stays the same too, so the next dependants hit the cache. Signature changes spread along the chain; body-only changes
stop after one step.

```text
 main ──calls──▶ util ──calls──▶ leaf        only leaf's body changed

 leaf   def changes · iface stays
 util   recompiled · its iface stays too
 main   cache hit — it stops here
```

#demo("examples/ch49/h3_effects_a.low")

#demo("examples/ch49/h3_effects_b.low")

*H3 --- effects are a set.* `effects io alloc` and `effects alloc io` give the same hash. They once differed, because the clause's source text was hashed. Then content addressing
addressed *spelling*, not meaning. The grammar says effects are a set, so they are hashed in normalised order.

#demo("examples/ch49/h4_contract.low")

*Contracts are surface too.* Adding `requires le a. 100 . .` changed `iface`. Callers trust and check the contracts of the ops they call, and as seen in #chref("proofs-bounds"),
contracts are the grounds for removing bounds checks. If a contract changed but the hash stayed the same, the cache would keep using results that trusted the old contract --- like
reusing a proof after its premises changed. The implementation once hashed only signatures and had this hole; it was fixed.

== What can and cannot be proven

`LowentHash.v` separates the two exactly. BLAKE3's collision resistance is a cryptographic assumption, not a subject of proof. What can be proven comes *before* it --- what the
canonical encoding erases and what it keeps.

#mathbox[Encoding theorems (`LowentHash.v`)][
  `H1_comments_are_irrelevant` · `H2_body_does_not_touch_iface` · `H3_effects_are_a_set`, and `encoding_is_injective` --- if encodings are equal, every piece of the interface is equal.
  The last theorem is the valuable one. If "same hash but different interface" ever occurs, the cause narrows to one thing --- a collision of the hash function. A machine guarantees it
  did not come from an ambiguous encoding.
]

Unlike effects, contract clauses are not normalised but hashed *in written order*. So `requires A . requires B .` and `requires B . requires A .` have the same meaning but different
hashes. It is imprecise. But it is the safe direction. Same meaning with different hashes only runs one more rebuild, while different meanings with the same hash make the cache lie.
That this direction is safe is proven too. The asymmetry seen in the interval analysis (#chref("proofs-bounds")) and borrow checking (#chref("proofs-ownership")) --- *when unsure,
go wide* --- decided the judgement here too.

#misconception[Leaving the order of head clauses free would make hashes unstable][
  That is why the order was fixed to one (#chref("surface")). Other orders are rejected at translation and fixed by `--fmt`, so the same program never has two hashes. The principle of
  one spelling per meaning is not a matter of surface taste but also a premise of content addressing.
]

== What hashes stop

#dtable(
  columns: 2,
  id: "phash-prevents",
  caption: [What content addressing stops],
  [*Situation*], [*Stopped by*],
  [Fixing a comment rebuilds everything], [H1],
  [Fixing a body rebuilds the dependants of dependants], [H2],
  [Only reordering effects misses the cache], [H3],
  [Changing a contract lets dependants use old results], [Contracts are in `iface`],
  [A dependency silently changes], [The Merkle DAG, since hashes contain content],
  [A lock file's dependency changes], [The lock file's content hash refuses it (#chref("build-test"))],
)

The last two lines are the grounds of reproducible builds. Making "the same source gives the same result" *comparable* is the value of content addressing --- it stops
being something believed and becomes something compared.

#qa[
  Are `--emit-db` hashes actually used for build decisions?
][
  Not fully yet. `--emit-db` is a side file, and this edition's incremental builds run on the content hash of emitted C (#chref("build-test")). The `iface` and `def`
  hashes are the layer that records exactly what is the same and what differs, and tests guard those same/different relations. Hashes taking over every build
  decision is future work.
]

== What is not proven

- *The theorems are about a model.* `LowentHash.v` proves H1, H2 and H3 for the canonical encoding. That the implementation uses that encoding is confirmed by tests on
  a few examples.
- *Normalisation is incomplete.* It is sensitive to contract clause order, and how far local variable names or numeric literal spellings (`0x10` vs `16`) are
  normalised was not exhaustively checked.
- *BLAKE3's collision resistance is an assumption.* If wrong, the cache sees different contents as the same.
- *That bundle (SCC) hashes come out the same across implementations is a specification-level promise* and not settled. There are several ways to handle cycles.
- *What tests guard is not hash values but the relations same and different.* If the encoding changes, values change, but the relations must stay.

#recap[
  Collision resistance of the hash function is an assumption, a Merkle DAG in which hashes contain dependencies' hashes replaces dependency tracking, and cyclic ops
  are hashed as one bundle. `--emit-db`'s `iface` and `def` hashes do not react to comments; body changes change only `def`, contract changes change `iface` too, and
  effects are normalised as a set. The injectivity of the encoding is proven, so if hashes match but interfaces differ the cause narrows to a collision. The
  imprecision of not normalising contract clauses is the safe direction.
]

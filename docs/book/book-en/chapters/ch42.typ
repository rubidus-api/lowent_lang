#import "../../book/lib.typ": *

= Proofs about syntax and hashes

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [a detached period closes, and a newline is whitespace]),
  ([#chref("build-test"), Building and testing], [builds are cached by the content hash of the emitted C]),
  ([#chref("proofs-why"), Why prove], [proving is choosing and writing down propositions]),
)

#deepqa[
  In #chref("surface"), `poly`'s `return` spanned two lines but was one form. What closed that form, and what did the newline do?
][
  The detached period at the end of the second line closed it. A newline is whitespace exactly like a space, so it closed nothing. That is why splitting the line anywhere kept the
  meaning. This chapter covers why such surface properties are subjects of proof, and hashes that recognise "the same program" by meaning rather than bytes.
]

#why[
  The proofs so far were about *meaning* --- what a program does. This chapter is about *surface* and *identity* --- what a program looks like, and whether two programs are the
  same. Syntax needs proofs because of something that actually happened. The parser and the stages after it believed different grammars, and the difference came out as defects.
  Hashes need proofs for the same reason. If two programs a cache judges "the same" differ in meaning, the cache lies.
]

#organizer[
  You will learn the theorem that one statement equals a one-statement block, the theorem that closers build the same tree, and how "the same" is defined by a meaning function and
  confirmed by computation. You will check against real output how the two hashes of content addressing (`iface` and `def`) react to comments, bodies, effect order and contracts,
  and understand what the injectivity theorem of the canonical encoding guarantees and why the imprecision of not normalising contract clauses is the safe direction.
]

#chapter-questions()

== One statement is a one-statement block

#demo("examples/ch42/oneform.low")

`bare` writes one statement directly as the body of `if`, and `blocked` wraps it in `do … end`. The answers are the same, and the canonical form printed by `--fmt` does not
differ by a single character.

#mathbox[Theorem G1 (`one_form_is_a_block` in `LowentBlock.v`)][
  `forall s, denote (wrap s) = denote s.` Wrapping one statement in a block does not change its meaning. Here "same meaning" means that the function `denote`, sending each tree to
  a flat sequence of statements, gives the same result. Defining "the same" by a function and confirming equations by computation is the only technique in this chapter. The
  corollary `body_is_one_thing` says that writing a statement or a block in a body position is the same.
]

This theorem justifies a design decision. C separates statements and compound statements into different kinds. `if (c) x = 1;` and `if (c) { x = 1; }` are grammatically different,
and the famous `goto fail;` trap came from that gap --- the indentation made two lines look as if they belonged to the `if`, but the grammar made only one belong. Lowent does not
separate them.

== Closers do the same job

#mathbox[Theorem G2 (`closers_agree`)][
  `forall c1 c2 h ops, close c1 h ops = close c2 h ops.` Closing with any of `.`, `end` or `)` gives the same tree. All three "close the innermost open form". Blocks are associative,
  so a block within a block unfolds in place (`block_assoc`, `nested_block_flattens`).
]

A newline could also be made a closer, and that is provable too. It still is not done. If newlines closed, line breaks would carry meaning, and merely splitting a long line would
change the program. *That a meaning can be proven the same does not mean the spelling must exist.* Theorems once attached to commas, line continuation and newline closing were all
`Qed` too, but when those rules left the language, the theorems left with them. Keeping theorems modelling dead rules would make a proven document speak of rules that do not exist.

Every proof in this file ends by computation. The value is not in the difficulty of the proofs but in *having chosen and written the propositions*. With "closers do the same job"
written down, the proof breaks when someone treats one specially. Syntax is a layer that quietly drifts with "it would be convenient to make an exception just here", and theorems
catch that drift. What they stop are not users' defects but the compiler's.

#qa[
  With this theorem, can the real parser be trusted to be correct?
][
  No. The model is a few small functions building trees, and the real parser is much larger. Their correspondence is backed by tests. And the back-end cross-check compares the VM
  and native code, but both pass through *the same front end*, so if the parser is wrong both are wrong in the same way. An attempt to cross-check the parser directly against this
  model did not get enough pairs to conclude anything, because the model is smaller than the real grammar. The front end is the weakest seam in this language's verification
  (#chref("proofs-limits")).
]

== Called by content, not by name

Ordinary build systems judge caches by file names and modification times. So a file touched with unchanged content is rebuilt needlessly (a loss), and changed content with the
#idx("content addressing")
same time uses a stale result (a danger). *Content addressing* drops names and calls things by the hash of their content. `lowentc --emit-db` gives two hashes per op --- `iface`,
the surface observable from outside (signature and contracts), and `def`, the whole definition. The hash function is BLAKE3-256.

A hash embeds its dependencies *by their hashes*. Then a definition's hash reflects the content of everything it depends on, and when one op deep down changes, the hashes above
change along with it. The hash itself is dependency tracking. Ops calling each other are grouped into one bundle (a strongly connected component), the order inside the bundle is
normalised, and it is hashed once.

== What changes a hash and what does not

#demo("examples/ch42/h1_plain.low")

#demo("examples/ch42/h1_comment.low")

*H1 --- comments change nothing.* Both hashes are the same. Fixing a comment does not rebuild the whole project. And a machine keeps the definition of "the same program" as meaning,
not bytes.

#demo("examples/ch42/h2_body.low")

*H2 --- changing only the body changes only `def`.* Changing `add a a` to `mul a 2` left `iface` as it was and changed only `def`. This is the heart of incremental builds. When only
an op's body changes, its users are recompiled, but their `iface` stays the same too, so the next dependants hit the cache. Signature changes spread along the chain; body-only changes
stop after one step.

#demo("examples/ch42/h3_effects_a.low")

#demo("examples/ch42/h3_effects_b.low")

*H3 --- effects are a set.* `effects io alloc` and `effects alloc io` give the same hash. They once differed, because the clause's source text was hashed. Then content addressing
addressed *spelling*, not meaning. The grammar says effects are a set, so they are hashed in normalised order.

#demo("examples/ch42/h4_contract.low")

*Contracts are surface too.* Adding `requires le a 100 .` changed `iface`. Callers trust and check the contracts of the ops they call, and as seen in #chref("proofs-numbers"),
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
That this direction is safe is proven too. The asymmetry seen in the interval analysis (#chref("proofs-numbers")) and borrow checking (#chref("proofs-ownership")) --- *when unsure,
go wide* --- decided the judgement here too.

#misconception[Leaving the order of head clauses free would make hashes unstable][
  That is why the order was fixed to one (#chref("surface")). Other orders are rejected at translation and fixed by `--fmt`, so the same program never has two hashes. The principle of
  one spelling per meaning is not a matter of surface taste but also a premise of content addressing.
]

== What is not proven

- *There is no guarantee that the real parser is this model.* The model is small and the parser large. Position information, comments and error recovery are absent from the model, so
  "the same tree" means the same structure, not the same diagnostic quality. The lexical (token) layer is separate too.
- *BLAKE3's collision resistance is an assumption.* If wrong, the cache sees different contents as the same.
- *That bundle (SCC) hashes come out the same across implementations is a specification-level promise* and not settled.
- *What tests guard is not hash values but the relations same and different.* If the encoding changes, values change, but the relations must stay.

#recap[
  One statement equals a one-statement block (G1), and the three closers build the same tree (G2). "The same" was defined by a meaning function and confirmed by computation, and the
  theorems catch syntax drifting quietly. `--emit-db`'s `iface` and `def` hashes do not react to comments; body changes change only `def`, contract changes change `iface` too, and
  effects are normalised as a set. The injectivity of the encoding is proven, and BLAKE3's collision resistance is an assumption. The imprecision of not normalising contract clauses is
  the safe direction.
]

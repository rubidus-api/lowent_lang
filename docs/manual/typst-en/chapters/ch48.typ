#import "../../typst-ko/lib.typ": *

= Proofs about syntax --- whichever closer closes it, the same tree

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [a detached period closes, and a newline is whitespace]),
  ([#chref("expr"), Expressions], [prefix notation has no precedence]),
  ([#chref("proofs-math"), The mathematical toolkit], [sets and relations --- what decides "the same"]),
)

#deepqa[
  In #chref("surface"), `poly`'s `return` spanned two lines but was one form. What closed that form, and what did the newline do?
][
  The detached period at the end of the second line closed it. A newline is whitespace exactly like a space, so it closed nothing. That is why splitting the line
  anywhere kept the meaning. This chapter covers why such surface properties are subjects of proof.
]

#why[
  The proofs so far were about *meaning* --- what a program does. This chapter is about *surface* --- what a program looks like. Syntax needs proofs because of
  something that actually happened. The parser was chopping forms at `.` and a later stage was silently gluing them back --- two layers believed different grammars,
  and the difference came out as defects. For the principle "one spelling per meaning" not to remain a matter of surface taste, a machine must guard that two
  spellings build the same tree.
]

#organizer[
  You will learn monoids, which have an identity and associativity, and denotations, which decide by a function whether two spellings are the same. You will confirm
  with examples the theorem that one statement equals a one-statement block, the theorem that the three closers build the same tree, and associativity of blocks.
  You will also see newline closing, left out even though its equivalence is provable, the theorems removed along with removed rules, and what these theorems do not
  say about the real parser.
]

#chapter-questions()

== Algebra and denotation

Joining statements has two properties. The way they join does not depend on grouping (associativity), and an empty block is nothing (identity --- what 0 does for
addition). A structure with both is a *monoid*. Concatenating lists is the typical monoid, and a program's sequence of statements has exactly that structure.

#idx("denotation")
To say whether two spellings are "the same" you need a criterion. Take a function `denote` sending each tree to *a flat sequence of statements*, and call them the same
if the results match. `denote(do S end) = [S] = denote(S)`, so the two are the same. This is the only technique in this chapter --- define "the same" by a function
and confirm equations by computation.

== One statement is a one-statement block

#demo("examples/ch48/oneform.low")

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

```text
 C       if (c)
             x = x + 10;
             y = 0;          ← indented as if inside the if, but the grammar puts it outside

 Lowent  if gt a 1 . set x (add x 10) .        one statement = a one-statement block (G1)
         if gt a 1 . do
           set x (add x 10) .
           set y 0 .
         end                                   a longer body is ended by end
```

== Closers do the same job

#mathbox[Theorem G2 (`closers_agree`)][
  `forall c1 c2 h ops, close c1 h ops = close c2 h ops.` Closing with `.` or `)` gives the same tree. Both "close the innermost open form". `end` is not on the list --- `do … end` is a pair of braces, so `end` closes only its own `do`; if a form is still open inside, it does not close it for you but refuses (`E-DOT-MISSING`). Blocks are associative,
  so a block within a block unfolds in place (`block_assoc`, `nested_block_flattens`).
]

Seen in an example:

#demo("examples/ch48/closers.low")

In `one_line` the two `)` close the inner forms and `.` closes the outer one. `spread` lays the same form over four lines with no marks needed, because a newline
is whitespace. Both ops give the same answer.

```text
 return add (mul x x) (add x 1) .      return add
                                           (mul x x)
                                           (add x
                                                1) .
 both become the same tree:
                 add
            ┌─────┴─────┐
           mul         add
          ┌─┴─┐       ┌─┴─┐
          x   x       x   1
```

A newline could also be made a closer, and that is provable too. It still is not done. If newlines closed, line breaks would carry meaning, and merely splitting a long line would
change the program. *That a meaning can be proven the same does not mean the spelling must exist.* Theorems once attached to commas, line continuation and newline closing were all
`Qed` too, but when those rules left the language, the theorems left with them. Today `,` and `;` are rejected with `E-VOCAB-REMOVED` --- a word that parses but has no
effect is a silent trap. Keeping theorems modelling dead rules would lit a proven document speak of rules that do not exist.

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

#qa[
  C has dozens of lines of precedence rules; why are this chapter's proofs so short?
][
  This language's expressions are prefix, so there is no operator precedence (the `expr` island is handled separately, #chref("expr")). With no precedence there are
  few cases of "what tree does this spelling become" to answer. It is a case of design making proofs easy. But short proofs are not small in value --- having written
  the propositions down is itself what prevents drift.
]

#misconception[A line break ends a statement][
  A line break is whitespace. `spread` in `closers.low` lays one form over four lines and, without any mark, becomes the same tree as
  `one_line`. Only closers (`.`·`)`) end a form. So a missing stop makes the next line join the previous form and produces an odd
  diagnostic, while splitting a long expression over several lines never changes its meaning. A rule making line breaks closers can also be
  proved, and this second property is exactly why it was not adopted.
]

== Common mistakes

#antipattern[Leaving out the period at the end of a line][
  #demo("examples/ch48/mistake_noperiod.low")

  The diagnostic points not at the line missing its period but at *the next line*. A newline closes nothing, so `set s (mul s 2)` was joined
  onto `add a b`, `add` ended up with four operands, and the tool says "extra operands". Look one line above the reported number first. Even
  if this diagnostic is confusing, the rule stays. If a line ended a statement, merely splitting a long expression would change its meaning.
]

#antipattern[Closing with `;` as in C][
  #demo("examples/ch48/mistake_semicolon.low")

  `;` used to be a third spelling of the period. Three spellings for one meaning make readers hunt for a difference, so it was removed and is now
  refused with `E-VOCAB-REMOVED`. It is not silently accepted, because a word that parses but means nothing is the hardest trap to find.
]

== What is not proven

- *There is no guarantee that the real parser is this model.* The model is a few small functions building trees, and the real parser is much larger. The
  correspondence is backed by unit and regression tests.
- *`denote` is a flat sequence.* Position information, comments and error recovery were not modelled. So "the same tree" means the same structure, not the same
  diagnostic quality.
- *The lexical (token) layer is separate.* Whether tokens split exactly into this model's three closers is the lexer tests' job.

#recap[
  A sequence of statements is a monoid with associativity and identity, and whether two spellings are the same is decided by a denotation into flat sequences,
  confirmed by computation. One statement equals a one-statement block (G1), `.` and `)` build the same tree (G2), and a block within a block unfolds in place.
  Newline closing was left out even though provable, and theorems of removed rules were removed with them. These theorems stop the compiler from drifting, but that
  the real parser is this model is backed by tests.
]

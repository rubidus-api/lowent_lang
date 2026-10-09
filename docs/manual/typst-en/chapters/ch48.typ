#import "../../typst-ko/lib.typ": *

= Proofs about syntax --- the text decides the tree

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [a name opens, a stop closes, and a newline is whitespace]),
  ([#chref("expr"), Expressions], [prefix notation has no precedence]),
  ([#chref("proofs-math"), The mathematical toolkit], [sets and relations --- what decides "the same"]),
)

#deepqa[
  In #chref("surface"), `poly`'s `return` spanned two lines but was one form. What closed that form, and what did the newline do?
][
  The stop at the end of the second line closed it. A newline is whitespace exactly like a space, so it closed nothing. That is why splitting the line
  anywhere kept the meaning. This chapter covers why such surface properties are subjects of proof.
]

#why[
  The proofs so far were about *meaning* --- what a program does. This chapter is about *surface* --- what a program looks like. Syntax needs proofs because of
  something that actually happened. The parser was chopping forms at `.` and a later stage was silently gluing them back --- two layers believed different grammars,
  and the difference came out as defects. For the principle "one spelling per meaning" not to remain a matter of surface taste, a machine must guard that two
  spellings build the same tree.
]

#organizer[
  You will meet a function that prints a tree as text and a small machine that reads text back into a tree. You will confirm with examples the theorem that
  reading printed text gives the tree back, that parentheses are decoration, that the number of stops is a checksum, that `end` does not close a form, and that
  blocks are associative. You will also see newline closing, left out even though its equivalence is provable, the theorems removed along with removed rules, and
  what these theorems do not say about the real compiler.
]

#chapter-questions()

== Text and trees

To say what is proven, two things must be fixed. A *tree* is the structure of a program. A form is one name and its arguments, and an argument is a leaf (a
literal), a form or a block. The variable `a.` is a form with no arguments. *Text* is a sequence of tokens --- leaves, opening names, stops, `do`, `end`,
parentheses. The function `print` writes a form as "name, arguments, stop" and a block as "`do`, forms, `end`".

#idx("reading machine")
Reading text back into a tree is the job of a small machine. It keeps one stack of the things that are open and does one thing per token. A name opens a
form. A stop closes *the form on top of the stack* --- and fails if the top is not a form. `do` opens a block, and `end` closes it only when the block is on
top. The machine has *no table of argument counts*. Every theorem in this chapter is about this machine.

== Reading is determined

#demo("examples/ch48/closers.low")

In `one_line` each of the two inner forms closes with its own stop, and the last stop closes the outer one. `spread` lays the same form over four lines with
no marks needed, because a newline is not a token. The two ops give the same answer.

```text
 return add mul x. x. . add x. 1 . . .      return add
                                                mul x. x. .
                                                add x.
                                                     1 . . .
 Both become the same tree:
                 add
            ┌─────┴─────┐
           mul         add
          ┌─┴─┐       ┌─┴─┐
          x   x       x   1
```

#mathbox[Theorem G1 (`parse_print` in `LowentBlock.v`)][
  `forall ts, parse (prints ts) = Some (map erase ts).` Reading the text printed from some trees gives exactly those trees back (`erase` removes the
  decorative parentheses). The corollary `form_ends_at_its_stop` says the same of a single form --- however many arguments it has, it is read the same way.
  What ends a form is its stop, not an argument count.
]

This theorem backs the design decision of edition 1.8. Up to edition 1.7 the number of arguments each op takes decided where a form ended, so reading a text
required knowing that op's declaration. Now the text alone decides the tree. An editor or a formatter gets the same tree without reading any declaration.

#mathbox[Theorem G2 (`parens_are_decoration`)][
  `forall t rest st, run (print (Group t) ++ rest) st = run (print t ++ rest) st.` Wrap a form in parentheses and, wherever it stands, the machine reaches
  the same state. Parentheses do not change the meaning.
]

#mathbox[Theorem G3 (`one_text_one_tree`)][
  `prints ts = prints us -> map erase ts = map erase us.` If two trees print as the same text, the two trees are the same once parentheses are removed. One
  text never has two meanings.
]

== The number of stops is a checksum

#mathbox[Theorem G4 (`stops_match_opens`, `missing_stop_is_refused`, `extra_stop_is_refused`)][
  In printed text the number of opening names equals the number of stops. A text whose form lacks its closing stop fails to read (`E-DOT-MISSING`), and so
  does a text with one stop more after something already closed (`E-CLOSER-EXTRA`).
]

#mathbox[Theorem G5 (`end_does_not_close_a_form`)][
  If `end` arrives while a form inside the block is still open, reading fails. `do … end` is a pair of braces, so `end` closes only its own `do` and does not
  close an open form for you. A form that owns a block is closed by the stop after `end` (`while c. do … end .`).
]

G5 is why one missing stop cannot silently swallow the rest of a file. An open form cannot cross the `end` of a block, so the mistake stops inside that block.

== Blocks are associative

Joining statements has two properties. The way of joining does not depend on grouping (associativity), and an empty block is nothing (an identity --- what 0
does for addition). A structure with both is a *monoid*. Appending lists is the standard monoid, and a program's sequence of statements has exactly that
structure.

#idx("denotation")
Saying two spellings are "the same" needs a yardstick. A function `denote` sends each tree to a *flat sequence of statements*, and two trees are the same when
the results are equal.

#mathbox[Theorem G6 (`block_assoc`, `nested_block_flattens`, `empty_block_is_nothing`)][
  A block within a block may be unfolded in place without changing the meaning, and an empty block adds nothing.
]


A newline is not in this model's text. Newlines were closers once, and the theorem for that rule was `Qed`. It was still dropped. If newlines closed, line
breaks would carry meaning, and merely splitting a long line would change the program. *That a meaning can be proven the same does not mean the spelling must
exist.* The theorems once attached to commas and line continuation, and edition 1.7's "one statement is a one-statement block" and "the two closers do the same
job", were all `Qed` too, but when those rules left the language the theorems left with them. Today `,` and `;` are rejected with `E-VOCAB-REMOVED`. Keeping
theorems that model dead rules would let a proven document speak of rules that do not exist.

The proofs in this file are not long. Their backbone is one induction that follows the shape of the tree (the lemma behind G1). The value is not in the
difficulty of the proofs but in *having chosen and written the propositions*. With "the reading machine has no table of argument counts" written down, the proof
breaks when someone treats one word specially. Syntax is a layer that quietly drifts with "it would be convenient to make an exception just here", and theorems
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
  `one_line`. Only the closer (`.`) ends a form. So a missing stop makes the next line join the previous form and produces an odd
  diagnostic, while splitting a long expression over several lines never changes its meaning. A rule making line breaks closers can also be
  proved, and this second property is exactly why it was not adopted.
]

== Common mistakes

#antipattern[Leaving out the period at the end of a line][
  #demo("examples/ch48/mistake_noperiod.low")

  The diagnostic points not at the line missing its period but at *the next line*. A newline closes nothing, so the `var` statement was
  still open when the `set` on the next line arrived, and the tool says "a stop must close the binding". Look one line above the reported number first. Even
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
- *The lexical (token) layer is separate.* Whether the glued stop `a.` and the spaced stop `a .` become the same token sequence is the lexer tests' job.
- *The model holds only the skeleton of the grammar.* Names in defining positions (`let x`, `input n`), types and patterns are leaves in the model, and the
  clauses of an op header, `else` chains and the infix operators of an `expr` island are not in it. Annex A.10 of the specification defines those shapes.

#recap[
  Reading the text printed from a tree gives the tree back (G1) --- the reading machine has no table of argument counts, so the text alone decides where a form
  ends. Parentheses are decoration (G2) and one text never has two meanings (G3). The number of stops is a checksum: one too few or one too many and reading
  fails (G4), and `end` does not close a form (G5). A block within a block unfolds in place (G6). Newline closing was left out even though it is provable, and
  the theorems of rules removed from the language were removed with them. That the real compiler is this model is backed by tests, not by these theorems.
]

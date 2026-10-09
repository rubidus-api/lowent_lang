#import "../../typst-ko/lib.typ": *

= Appendix C --- Common mistakes and how to fix them

These are the places people often trip when writing Lowent with habits from other languages. The meanings of diagnostic codes are in Appendix B, and
explanations are in the chapters named in the tables.

== The surface

#dtable(
  columns: 3,
  id: "a3-surface",
  caption: [Mistakes on the surface],
  [*Mistake*], [*Diagnostic*], [*Fix*],
  [Writing `output` before `input`], [`E-CLAUSE-ORDER`], [Follow the clause order table. Clauses other than inputs are moved by `--fmt` (#chref("surface"))],
  [Opening a block with a period or line break, as in `def struct p .`], [`E-STMT-NODO`], [`def struct p do … end .`],
  [Closing with `;` or `,`], [`E-VOCAB-REMOVED`], [The full stop `.`],
  [Reading a field with `p.x`], [`E-FIELD-GLUED`], [`field p. x .`],
  [`for x in xs`], [`E-VOCAB-REMOVED`], [`for x xs. do … end .`],
  [`let x f64 .5 .`], [`E-LET-NOVALUE`], [`0.5`],
  [Naming a local the same as a parameter], [`E-NAME-SHADOW`], [Pick a new name],
  [Using names like `count`, `text` or `len`], [`E-NAME-BUILTIN` and others], [Builtins and the word that opens a text literal cannot be names],
  [A module name equal to an op name], [`E-NAME-DUP`], [Rename the module],
  [Leaving the stop off a variable, as in `add a b .`], [`E-DOT-MISSING`], [`add a. b. .` --- a variable is written `a.` (#chref("surface"))],
  [`let x be u8 4 .`], [`E-LET-BE`], [`let x u8 4 .`],
  [One stop short, as in `return add a. b. .`, or no stop after `end`], [`E-DOT-MISSING`], [`return add a. b. . .` · `end .` --- close as many as you opened],
  [One stop too many, as in `if gt a. 3 . . do … end .`], [`E-CLOSER-EXTRA`], [`if gt a. 3 . do … end .`],
  [`for i count u64 n . do`], [`E-FOR-OLD`], [`repeat i u64 n. do`],
)

== Values and flow

#dtable(
  columns: 3,
  id: "a3-values",
  caption: [Mistakes with values and flow],
  [*Mistake*], [*Diagnostic*], [*Fix*],
  [`set` on a `let`], [`E-IMMUTABLE`], [Make it with `var`],
  [Using an integer as a condition], [`E-TYPE-COND`], [Write the question, as in `gt n. 0 .`],
  [Expecting overflow to wrap as in C], [`E-VM-OVERFLOW` (at run time)], [Choose the outcome with `wrap_add`, `sat_add` or `chk_add`],
  [Expecting `narrow u8 x. .` to truncate], [`E-VM-CAST` (at run time)], [`narrow_wrap`, `narrow_sat`, `narrow_try`],
  [`guard`'s `else` does not leave], [`E-GUARD-FALLTHROUGH`], [`return`, `break`, `continue`, `panic`, or use `if`],
  [No `return` on some path], [`E-RETURN-PARTIAL`], [Return on every path],
  [Using `if` as a value], [`E-IF-VALUE`], [Make a `var` and `set` it in each branch],
  [Missing cases in `match`], [`E-MATCH-INEXHAUSTIVE`], [The missing `case` or `case _`],
  [Writing enum variants one per line without periods], [`E-ENUM-DOT`], [`red .` for each variant],
  [Extracting with `some_value` without checking], [`E-VM-NONE` (at run time)], [`guard is_some` · `value_or` · `match`],
  [`array 4 u8`], [`E-TYPE-ARRAY`], [`array u8 4`],
)

== Effects, capabilities and memory

#dtable(
  columns: 3,
  id: "a3-effects",
  caption: [Mistakes with effects, capabilities and memory],
  [*Mistake*], [*Diagnostic*], [*Fix*],
  [Printing from a `fn`], [`E-EFFECT-CALC`], [`proc` with `effects io`],
  [Writing `effects none` on a `fn`], [`E-EFFECT-REDUNDANT`], [Delete the clause],
  [A `fn` writing to the caller through a `mut` parameter], [`E-EFFECT-PURITY`], [Make it a `proc`],
  [`effects io` without receiving a capability], [`E-EFFECT-NO-CAP`], [`input out cap io .`],
  [Receiving a capability but not passing it to the core op], [`E-CAP-MISSING`], [As the first operand, as in `write_out out. 1 …`],
  [The entry point receiving data], [`E-ENTRY-PARAMS`], [The entry point receives only capabilities. Arguments via `cap args`],
  [Writing to a slice without `mut`], [`E-TYPE-MUT`], [`mut slice`],
  [Writing through `ref`], [`E-TYPE-REF`], [`mut_ref`],
  [Borrowing the same value for writing twice], [`E-EXCL`], [Split the borrows or split the order],
  [Returning a reference to a local], [`E-ESCAPE`], [Return a value or store it in the caller's storage],
  [`mut ref slice`], [`E-MREF-SLICE`], [Return a new slice],
  [Carrying a region's bytes outside], [`E-REGION-ESCAPE`], [Widen the region or carry out only values],
  [Not closing a file or buffer], [`E-OWN-INCOMPLETE`], [Call `close` or `finish`. To discard, `drop`],
  [Using a moved value again], [`E-OWN-MOVED`], [Use it before handing over, or get it back],
  [Using the heap on a target without an operating system], [`E-HEAP-NOHOST`], [A fixed window (`cap allocator`) and `alloc`],
)

== Abstraction and concurrency

#dtable(
  columns: 3,
  id: "a3-abstraction",
  caption: [Mistakes with abstraction and concurrency],
  [*Mistake*], [*Diagnostic*], [*Fix*],
  [Writing `fn` or `proc` in a trait signature], [`E-TRAIT-SIG`], [`area input s self . output u64 .`],
  [Leaving the effects line off a `proc`], [`E-EFFECT-MISSING`], [Write `effects <atoms> .` --- `effects none .` when there are none (`--doc` prints `inferred:`)],
  [A type argument not satisfying the trait], [`E-BOUND-UNSAT`], [Add `satisfies` and the op],
  [Giving a run-time value where `comptime` is required], [`E-COMPTIME-ARG`], [A literal or module constant],
  [`spawn <op>` outside a `task_group`], [`E-SPAWN-SCOPE`], [Wrap it in `task_group do … end .`],
  [Writing an accumulator in a split loop], [`E-PAR-CARRY`], [`reduce <place> <op> .`],
  [Using a stage like sorting in `pipe`], [`E-PIPE-STAGE`], [Use only the seven stages and five terminals],
  [Calling an effectful attached op with `method` inside a `fn`], [(not rejected in this edition)], [Call it directly as `<type>.<name>` (#chref("traits"))],
)

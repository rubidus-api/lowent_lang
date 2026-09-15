# Appendix B — Diagnostics index

Only the diagnostics that this book’s examples actually trigger are listed. A code keeps its meaning across releases; the compiler explains the details and the repair at the site.

## <a id="sx1"></a>At translation time

| **Code** | **Meaning** | **Shown in** |
|---|---|---|
| `E-ACCESS-MODE` | the body breaks the read or write promise made with `access` | chapter 27 |
| `E-ALLOC-AMBIGUOUS` | more than one fitting allocator and none chosen with `using` | chapter 20 |
| `E-ALLOC-NESTED` | allocated from an outer source while an inner region of the same root is open | chapter 18 |
| `E-ALLOC-NOCAP` | declares `alloc` but receives no allocation capability | chapter 16 |
| `E-ALLOC-NOSOURCE` | a call draws from an allocator but no fitting one is visible in this op | chapter 20 |
| `E-ALLOC-OUTLIVES` | region bytes handed to an actor born outside the region | chapter 18 |
| `E-ALLOC-SHARED` | an allocator whose cursor is not atomic is handed to a task | chapter 26 |
| `E-ALLOC-TASK` | an op that carves from a root is spawned as a task | chapter 26 |
| `E-ALLOC-USING-UNUSED` | `using` on a call that does not draw from an allocator | chapter 20, chapter 34 |
| `E-ASM-TARGET-UNKNOWN` | the machine name in an `asm` clause is not a known target | chapter 30 |
| `E-ASM-UNBOUND` | the assembly template names an undeclared operand | chapter 30 |
| `E-ATOMIC-NOCAP` | declares `atomic` but receives no `cap atomic` | chapter 27 |
| `E-ATOMIC-ORDER` | a memory ordering that has no meaning for that atomic operation | chapter 27 |
| `E-BLOCK-UNCLOSED` | a block opened with `do` is never closed with `end` | chapter 2 |
| `E-BOUND-UNSAT` | the type argument does not satisfy the required trait | chapter 22, chapter 23 |
| `E-BRAND-REUSED` | a brand opens a second storage | chapter 35 |
| `E-CAP-FORGE` | spawns an actor holding a capability without holding that capability | chapter 20, chapter 25 |
| `E-CAP-KIND` | a capability of the wrong kind was handed over | chapter 16 |
| `E-CAP-MISSING` | a capability-requiring builtin was not given its capability as the first operand | chapter 16 |
| `E-CHAR` | a character (symbol) this language does not have | chapter 6, chapter 7, chapter 8, chapter 9 |
| `E-CHAR-WIDTH` | a character literal is not a single character | chapter 3 |
| `E-CLAUSE-ORDER` | the clauses of an op header are out of the fixed order | chapter 2, chapter 3, chapter 22 |
| `E-COMPTIME-ARG` | a run-time value in a `comptime` position | chapter 22 |
| `E-CONC-ALONE` | a task group spawns one task that waits for a peer | chapter 26 |
| `E-CONC-DEADLOCK` | every task in the group receives and none sends | chapter 26 |
| `E-CONFIG-TYPE` | the configuration gives a value the option does not offer | chapter 31 |
| `E-CONFIG-UNDEF` | a configuration or `config` names an undeclared build option | chapter 31 |
| `E-CONTRACT-DEAD` | declares an error that `requires` already excludes | chapter 14 |
| `E-CONTRACT-IMPOSSIBLE` | a call with constant arguments breaks the callee’s `requires` | chapter 14, chapter 35 |
| `E-DEP-MISSING` | the file at a `use … from` place cannot be read | chapter 21 |
| `E-EFFECT` | performs an effect its `effects` clause does not declare | chapter 15, chapter 20, chapter 26 |
| `E-EFFECT-CALC` | a pure `fn` performs an effect | chapter 1, chapter 5, chapter 15, chapter 16, chapter 18, chapter 24 |
| `E-EFFECT-DUP` | the same atom twice in an `effects` clause | chapter 15, chapter 44 |
| `E-EFFECT-NO-CAP` | declares `io` (or similar) without a capability that authorises it | chapter 2, chapter 16 |
| `E-EFFECT-NONE-MIX` | `none` written together with a real effect | chapter 15 |
| `E-EFFECT-PURITY` | a `fn` writes state visible to its caller or actor | chapter 5, chapter 15, chapter 25 |
| `E-EFFECT-REDUNDANT` | `effects none` written on a `fn` | chapter 5 |
| `E-EFFECT-UNDEF` | an effect word outside the closed vocabulary | chapter 15, chapter 44 |
| `E-ENS-UNDEF` | `ensures` names something undefined — the returned value is `ret` | chapter 14 |
| `E-ENTRY-PARAMS` | the entry point takes a non-capability input | chapter 16 |
| `E-ENUM-DOT` | an enum variant not closed with `.` | chapter 10 |
| `E-ENUM-INFINITE` | a variant embeds its own type by value | chapter 10 |
| `E-ERR-UNDECLARED` | returns an error not in its `errors` clause | chapter 11 |
| `E-ESCAPE` | a reference to a local escapes the op | chapter 12 |
| `E-EXCL` | overlapping borrows or owner access to the same value | chapter 12, chapter 26, chapter 43 |
| `E-EXPR-APP` | a call inside an `expr` island is not parenthesised | chapter 8 |
| `E-EXPR-CHAIN` | comparisons chained in an `expr` island | chapter 3 |
| `E-EXPR-UNARY` | a unary operator in an `expr` island | chapter 8 |
| `E-FFI-NOCAP` | calls C without receiving `cap c` | chapter 29 |
| `E-FFI-NOEFFECT` | an op calling C declares no effect | chapter 29 |
| `E-FFI-NOUNSAFE` | calls C without the `unsafe` mark | chapter 29 |
| `E-FFI-TYPE` | a type the C ABI cannot express crosses the boundary | chapter 29 |
| `E-FIELD-GLUED` | tried to read a field by gluing a dot to a value — write `field` | chapter 3 |
| `E-FN-CAP` | an op to be passed as a callback requires a capability | chapter 29 |
| `E-FN-NOTEXPORT` | `unsafe_fn` names an op that is not `export extern` | chapter 29 |
| `E-FOLD-OP` | a `pipe` stage names an op that does not exist | chapter 24 |
| `E-GROUP-UNCLOSED` | an unclosed parenthesis | chapter 3 |
| `E-GUARD-FALLTHROUGH` | the `else` of a `guard` does not leave | chapter 7 |
| `E-HEAP-NOHOST` | asks for the growing root on a freestanding target | chapter 18 |
| `E-IF-VALUE` | `if` used as a value | chapter 6 |
| `E-IMMUTABLE` | `set` on a name bound with `let` | chapter 6 |
| `E-IR-ARITY` | an op is called with the wrong number of arguments | chapter 5, chapter 48 |
| `E-IR-UNDEF` | a name unknown at that place | chapter 6, chapter 7, chapter 21, chapter 22, chapter 25 |
| `E-ISR-CALLED` | an interrupt handler called from code | chapter 30 |
| `E-ISR-EFFECT` | an interrupt handler does not declare `effects device` | chapter 30 |
| `E-ISR-PARAMS` | an interrupt handler takes parameters | chapter 30 |
| `E-LET-NOVALUE` | nothing after `be` | chapter 6 |
| `E-LOCK-NOTYET` | shared lock types are not built yet | chapter 26 |
| `E-MATCH-INEXHAUSTIVE` | a `match` does not cover every case | chapter 7, chapter 10, chapter 11 |
| `E-MATCH-REDUNDANT` | a `match` arm can never run (an arm after `_`, overlapping ranges) | chapter 11 |
| `E-METHOD-UNDEF` | no op of that name is attached to the receiver’s type | chapter 22, chapter 23 |
| `E-MMIO-BYVALUE` | an mmio register block taken by value | chapter 30 |
| `E-MMIO-PERM` | writes a read-only register | chapter 30 |
| `E-MREF-SLICE` | `mut ref slice` used | chapter 12 |
| `E-NAME-BUILTIN` | a builtin op’s name is used for a declaration or local | chapter 5, chapter 6 |
| `E-NAME-DUP` | the same name declared twice in one module | chapter 21 |
| `E-NAME-SHADOW` | re-binds a live name (shadowing) | chapter 3, chapter 6 |
| `E-OPT-UNUSED` | a declared build option that no code reads | chapter 31 |
| `E-OWN-INCOMPLETE` | a value that needs completion is dropped implicitly | chapter 19, chapter 28, chapter 36 |
| `E-OWN-JOIN` | ownership state differs between branches | chapter 19 |
| `E-OWN-MOVED` | uses a value after it was moved | chapter 19, chapter 25, chapter 28, chapter 35, chapter 36 |
| `E-PAR-ASSOC` | the reduction operator is not associative | chapter 27 |
| `E-PAR-CARRY` | a split loop writes a local that lives across iterations | chapter 27 |
| `E-PAR-FLOAT` | a floating-point accumulation declared splittable | chapter 27 |
| `E-PAR-NOLOOP` | the `parallel` clause finds no loop to split | chapter 27 |
| `E-PAR-READ` | a split loop reads another iteration’s element | chapter 27 |
| `E-PAR-WRITE` | a split loop writes another iteration’s element | chapter 27, chapter 45 |
| `E-PIPE-NO-TERMINAL` | a stage after the terminal | chapter 24 |
| `E-PIPE-STAGE` | a word that is not a `pipe` stage | chapter 24 |
| `E-PROFILE-LEVEL` | uses concurrency the build profile does not provide | chapter 25 |
| `E-REGION-ESCAPE` | carries region bytes out of the region | chapter 18 |
| `E-RETURN-PARTIAL` | some path does not return a value | chapter 3, chapter 5, chapter 7 |
| `E-SPAWN-SCOPE` | spawns a task outside a `task_group` | chapter 26 |
| `E-TIER-EFFECT` | an effect the declared machine tier cannot carry | chapter 30 |
| `E-TOPLEVEL` | something that cannot appear at top level is at top level | chapter 3 |
| `E-TRAIT-EFFECT` | the implementing op has more effects than the signature | chapter 23 |
| `E-TRAIT-MISSING` | an op required by the trait is missing | chapter 23 |
| `E-TRAIT-SIG` | `fn`/`proc` in a signature, or a parameter-count mismatch | chapter 23 |
| `E-TYPE-ARRAY` | `array` length written after the type | chapter 9 |
| `E-TYPE-BITCAST` | `bit_cast` target is not a plain scalar | chapter 20 |
| `E-TYPE-COND` | a non-boolean in a condition | chapter 4 |
| `E-TYPE-DECL` | `be` in a type declaration | chapter 13 |
| `E-TYPE-FIELD` | a field is missing or unknown when building a struct | chapter 10 |
| `E-TYPE-INSTANCE` | mixes different instances of the same generic | chapter 35 |
| `E-TYPE-KIND` | a type of the wrong kind for that operation (e.g. `cast` of `bool`) | chapter 13 |
| `E-TYPE-LOGICAL` | a non-boolean value given to a logical operation | chapter 8 |
| `E-TYPE-MUT` | writes an element of a non-`mut` slice | chapter 9 |
| `E-TYPE-NOMINAL` | mixes nominally distinct types | chapter 13 |
| `E-TYPE-REF` | writes through a shared `ref` | chapter 12 |
| `E-TYPE-RETURN` | the returned value does not match the op’s output type | chapter 11, chapter 31 |
| `E-TYPE-SIGN` | mixes signs with no value-preserving widening | chapter 4, chapter 39, chapter 40 |
| `E-TYPE-WIDTH` | the value does not fit the declared type | chapter 3, chapter 13 |
| `E-UNSAFE-UNDECLARED` | declares the `unsafe` effect but the op is not marked `unsafe` | chapter 29 |
| `E-VISIBILITY` | reaches a non-exported name of another module | chapter 21 |
| `E-VOCAB-REMOVED` | a removed word or spelling | chapter 3, chapter 5, chapter 7, chapter 15, chapter 48 |
| `E-WIDEN-SIGN` | `widen` from a signed to an unsigned type | chapter 13 |
| `W-EFFECT-OVER` | an effect declared but never performed (warning) | chapter 15, chapter 16 |
| `W-NOT-YET` | a word accepted by name that has no meaning yet | chapter 13 |
| `W-USE-EXTERNAL` | imports a module not in the compilation unit (warning) | chapter 32 |

*Table 50.1 — Diagnostics shown by the examples*

## <a id="sx2"></a>When execution stops

| **Code** | **Meaning** | **Shown in** |
|---|---|---|
| `E-VM-OVERFLOW` | integer overflow at the declared width | chapter 4 |
| `E-VM-DIV0` | division by zero | chapter 4 |
| `E-VM-CAST` | a narrowing or `cast` value does not fit | chapter 4 |
| `E-VM-SHIFT` | shift amount not smaller than the width | chapter 4 |
| `E-VM-BOUNDS` | index out of bounds | chapter 9 |
| `E-VM-NONE` | took the value out of `none` | chapter 11 |
| `E-VM-CONTRACT` | a contract was broken | chapter 14 |
| `E-VM-PANIC` | the program called `panic` | chapter 7 |
| `E-VM-ANALYSIS` | an eliminated check was actually needed (compiler bug) | chapter 40 |
| `E-TEST-FAIL` | an `expect` in a test is false | chapter 31 |

*Table 50.2 — Run-time diagnostics*

---

[← Prev](sec54.md) · [Contents](README.md) · [Next →](sec56.md)

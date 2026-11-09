#import "../lib.typ": *

#sub("6.4", "Declarations and contracts")[
  #sub("6.4.1", "Declaring an op")[
    #para("1")[
      An #t("op", "op") is a unit of execution with a name and a contract. Ops are divided into two kinds.
    ]
    #term("fn", "pure op")[
      An op that produces no effect at all. It always gives the same result for the same input. Since it does not touch the outside
      world, executing it in a different order gives the same result.
    ]
    #term("proc", "effectful op")[
      An op that produces effects. It shall write in its own contract which effects it produces (#cref("7.1")), and if it produces more
      effects than written, translation is refused.
    ]
    #para("2")[
      The kind #strong[is always written.]; There is no default — whether it is `fn` or `proc` is written in front of the name.
    ]
    #para("3")[
      The declaration of an op consists of, in order, the name, inputs, output, effects, contract and body. Inputs are written with `input`,
      the output with `output`. The order is laid out #strong[so that what comes first is used by what comes after];:
      - A named promise (`satisfies`) comes directly after the name — it says first #strong[what]; this op is.
      - Translation-time inputs (`comptime`) come next — the types of the following inputs and output use those names.
      - Capability inputs come before data inputs — who permitted it is seen before what it receives.
      - The output comes after the inputs — the output type may use the inputs' type parameters.
      - Effects come after the output and before the contract — they write #strong[what is done]; with the capabilities received earlier.
      - The contract is in the order input conditions (`requires`) · output promises (`ensures`) · failures (`errors`) · tests (`tests`).
    ]
    #para("3a")[
      The clauses of the head are written #strong[in one order only];. From front to back:
      `satisfies` · `lowdoc` · `vector` · `priority` · `comptime` inputs · capability and region inputs (whose type is `cap …` · `region …`) ·
      `use` · data inputs · `output` · `effects` · `link` · `variadic` · `asm` · `absorbs` · `reference` · `why` · `access` · `inplace` · `invalidates` · `parallel` · `reduce` ·
      `requires` · `ensures` · `errors` · `tests` · `schedule`. Clauses in the same position keep the order in which they are written.
      The method signatures of a trait (#cref("6.11.2")) and `proc`s inside an actor follow the same order.
      Violating this order is refused at translation (`E-CLAUSE-ORDER`). The order of inputs is also the order of the caller's arguments —
      so capabilities are always passed #strong[before]; data.
    ]
    #para("3b")[
      The formatter (`--fmt`) moves clauses that are not inputs, and `use` clauses, into this order. It does not move inputs among themselves —
      moving inputs would change the meaning of calls.
    ]
    #para("3c")[
      Each clause of the head #strong[is closed by its own dot]; — the same as statements (#cref("6.1.6") (2)). A clause word does not close the previous clause,
      and neither does a newline. Meeting the next clause word or `do` without a dot is refused at translation (`E-DOT-MISSING`).
      The list of clauses is the head, so clauses begin with the first word after the name — even if the name has the same spelling as a clause word
      (`proc link …`), it is read as the name.
    ]
    #para("3d")[
      The meanings of the clause words that appear in the order of (3a), and where they are defined, are as follows.
      - `satisfies` — a named promise this op keeps (#cref("6.4.5") · #cref("6.11.2")). `lowdoc` — a description for people to read (#cref("6.4.3")).
      - `vector` · `priority` — make this op an interrupt handler: the number of the interrupt it receives and its urgency (#cref("7.7")).
      - `comptime` inputs — parameters fixed at translation time (#cref("6.8")). Capability inputs — the right to produce effects (#cref("7.2")).
      - `use` — the allocator this op uses (#cref("8.14")).
      - `link` · `variadic` — the name on the C side · arguments whose number is not fixed (#cref("7.4")).
      - `asm` — the place that writes machine instructions directly (#cref("6.9.1")).
      - `absorbs` · `reference` · `why` — the declaration that the spread of `unsafe` stops here, and the pure version and assumptions that ground it (#cref("7.2.1")).
      - `access` · `inplace` · `invalidates` — what it touches and in which mode · writing in place · what it invalidates (#cref("6.4.3") · #cref("8.12")).
      - `parallel` · `reduce` — a loop that runs divided, and the place where the steps gather (#cref("6.5.10")).
      - `requires` · `ensures` · `errors` · `tests` — the clauses of the contract (#cref("6.4.3")).
      - `schedule` — comes only in the head of a test (`test`). `schedule explore_interleavings .` runs #strong[every order]; in which messages are delivered
      and checks that the result is the same for each order. With `limit <number>` it runs only up to that number. If the result differs by order,
      the test fails (`E-SCHED-NONDET`). The meaning is specified by #cref("6.4.13") (4).
    ]
    #para("3e")[
      #strong[The output can be named]; — `output <name> <type> .`. That name is a local of the body and #strong[starts at 0];:
      `0` · `0.0` for numbers, `false` for `bool`, `none` for `option`, and for a struct the 0 of each field. The body writes to that name and
      returns it with `return <name> .` (as with other ops that return a value, every path shall end with `return` — #cref("6.5.5") (3)).
      - Distinguishing: there is one namespace (#cref("6.10")), so if the first word after `output` #strong[is a type name];, the whole output is a type;
      if it is not a type name and what follows is one type, that word is the name of the result.
      - A type without a fixed zero (slices · `result` · `owned` · enums · newtypes · generic parameters, and structs with such fields)
      cannot be named (`E-RESULT-NOZERO`). Such an output is written without a name and returns a value.
      - An op without a body (`extern` · the method signatures of a trait) cannot name its result (`E-RESULT-NAMED`).
      - An output that returns an array (`output [<name>] array <length> <type> .`) #strong[has its place provided by the caller]; — #cref("6.2.6") (1c).
      - `ensures` still refers to the result as `ret` (#cref("6.4.12")). The name of the result is visible only inside the body.
    ]
    #ex("a named result starts at 0", "module ex_named_result .

struct span do
  lo u64 .
  hi u64 .
  seen bool .
end .

export fn bounds input xs slice u64 . output s span .
do
  for x xs. do
    if or not field s. seen . . lt x. field s. lo . . . do set field s. lo . x. . end .
    if gt x. field s. hi . . do set field s. hi . x. . end .
    set field s. seen . true .
  end .
  return s. .
end .")
    #rejected("a name given to a type without a fixed zero", "module ex_named_nozero .

fn first input xs slice u8 . output r slice u8 .
do
  return xs. .
end .", "E-RESULT-NOZERO")
    #ex("declaring an op", "module ex_op .

export fn twice input n u32 . output u32 .
  requires le n. 2147483647 . .
do
  return mul n. 2 . .
end .")
    #rejected("the output written before the input", "module ex_clause_order .

fn twice output u32 .
  input n u32 .
  requires le n. 2147483647 . .
do
  return mul n. 2 . .
end .", "E-CLAUSE-ORDER")
    #rejected("a head clause without a dot", "module ex_clause_dot .

fn twice input n u32 output u32 .
do
  return mul n. 2 . .
end .", "E-DOT-MISSING")
    #plain[
      `export` is the mark that lets other modules use this op. Without it, it is used only inside its
      module.
    ]
  ]
  #sub("6.4.2", "What a contract is")[
    #para("1")[
      A #t("계약", "contract") is the #strong[promise an op writes for itself]; about its input and output.
      A contract is not a comment but #strong[a statement that is checked];.
    ]
    #para("2")[
      Contracts are used in three places.
    ]
    #para("3")[
      a) #strong[The processor uses them as facts]; — on the premise that the contract is true, it removes checks (#cref("6.4.5")).
    ]
    #para("4")[
      b) #strong[They are enforced during execution]; — a contract the processor could not prove is confirmed during execution,
      and if broken it traps. But whether the remaining checks are actually kept is decided by #strong[the build mode]; (#cref("6.4.8")).
    ]
    #para("5")[
      c) #strong[They speak to readers]; — what must be kept to call this op is visible without opening
      the body.
    ]
    #para("6")[
      There are two places where a contract breaks, and the diagnostic separates #strong[whose fault it is];.
    ]
    #tbl("When a contract is broken")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Clause*], [*When it is checked*], [*Whose fault if broken*],
      [`requires`], [on entry], [#strong[the caller's]; — it did not keep the condition it had to keep],
      [`ensures`], [on exit], [#strong[this op's]; — it broke its own promise (the caller was told a lie)],
      )
    ]
    #plain[
      This distinction really pays off. When a program stops, the diagnostic says at once #emph["did I call it wrongly, or was that function
      built wrongly"];.
    ]
    #caution("A contract is an asset, not a cost")[
      It is easy to think #emph["writing checks makes it slower"];. In Lowent it is the opposite. When a contract is written,
      the processor #strong[uses it as a fact]; and removes checks during execution. If it is not written, the processor does not know, so
      the check remains — that is, #strong[writing honestly is faster];.
    ]
  ]
  #sub("6.4.3", "The six clauses of a contract")[
    #para("1")[
      A contract consists of the following six clauses. Not every clause is required.
    ]
    #term("requires", "precondition")[
      A condition that shall be true at the moment of entering the op. The caller is responsible for keeping it.
      It is checked on entry, and if it is broken it traps.
    ]
    #term("ensures", "postcondition")[
      A condition that is true when the op ends normally. The maker is responsible for keeping it.
      The value returned can be referred to by the name `ret`.
    ]
    #term("errors", "error condition")[
      When the op fails, and the error value then. It states which error comes under which condition.
      The condition is read with #strong[the values on entry]; to the op — ⟦(1c)⟧.
    ]
    #term("effects", "effect declaration")[
      The list of effects this op produces (#cref("7.1")). If it produces no effect at all it is `effects none`.
    ]
    #term("access", "access declaration")[
      What this op touches, and how — written `access <name> <mode> .`. The modes are the seven in the table below
      and no more; any other word is refused (`E-CONTRACT-MODE`).
    ]
    #tbl("The modes of `access` — a closed set of seven")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Mode*], [*Enforced now*], [*Meaning*],
      [`shared_read`], [#strong[yes];], [#strong[does not write]; to this place. Writing is refused],
      [`write_only`], [#strong[yes];], [#strong[does not read]; this place. Reading is refused],
      [`sequential`], [no], [goes through in order],
      [`random`], [no], [touches any place],
      [`streaming`], [no], [passes once and does not look again],
      [`tiled`], [no], [goes through in blocks],
      [`read_mostly`], [no], [mostly reads and rarely writes],
      )
    ]
    #term("tests", "examples")[
      Examples that show how this op is used. The processor can run them as tests.
    ]
    #para("1a")[
      The clause `tests <name>…` writes one or more #strong[names of ops]; that test this op. The names written shall be ops visible
      at that place; if not, it is refused (`E-CONTRACT-UNDEF`). The name of a test block (#cref("6.4.13")) is not an op,
      so it cannot be written here. The `tests` clause #strong[neither defines nor runs]; tests — it #strong[only points]; at
      what already exists. If an op pointed at disappears, the head reports it.
    ]
    #para("1b")[
      The `lowdoc` clause writes the description of this op. One line is `lowdoc "<text>" .`; several lines are opened with
      `lowdoc text <end mark>`, and the text runs up to the line on which that mark stands alone. This text is read by the document output
      (`--doc`) and does not affect the meaning of the translated program.
    ]
    #para("1c")[
      The condition of the `errors` clause is read with #strong[the values on entry]; to the op.
    ]
    #para("1c1")[
      If the body #strong[never]; raises that error, the processor reports it
      (`W-ERRORS-UNRAISED`). The clause is read as the promise «this op can fail this way»,
      and the caller builds branches by looking at it; a branch that cannot be taken becomes #strong[dead code that the reader cannot tell apart
      from live branches];. An op that passes on someone else's `result` as it is is regarded as raising that
      error (`try` · `return <call>`).
    ]
    #para("1d")[
      So the only names that can stand in that condition are #strong[those that are the same on entry and on exit]; — namely
      inputs that do not change and module constants. Writing in the condition a name the body can change (an actor's state field · a module `var` ·
      an argument in a `mut` position) is refused (`E-ERRORS-STATE`).
    ]
    #plain[
      Why on entry? In the table of ⟦(6)⟧ `errors` stands on the same side as `requires` — it is a clause that says #strong[what the caller
      did wrong];. What the caller did is only #strong[what it handed over];, so the only values that can measure that
      mistake are what was handed over. Read with the values on exit, a successful execution would accuse
      itself — an op that subtracted 30 from a balance of 50 and succeeded would, because the condition «more than the balance» is read again on exit,
      become #emph["it did not raise the error it should have"];.
    ]
    #caution("")[
      (1d) refuses generously. A slice in a `mut` position does not change length, so a condition that reads only `len`
      is in fact the same on entry and on exit, but the processor does not distinguish #strong[which part]; of the value a condition
      reads. Rather than pretending to distinguish what cannot be distinguished, it is better to draw the line thickly and write
      that down — write it with `guard` inside the body.
    ]
    #para("2")[
      The conditions used in a contract shall be #strong[pure expressions]; — something that produces effects cannot be written in a contract.
    ]
    #para("3")[
      `shared_read` and `write_only` are #strong[disciplines of reading and writing];, so the processor enforces them now. That is,
      writing to a place declared `shared_read` is refused, and reading a place declared `write_only` is refused
      (`E-ACCESS-MODE`).
      The other five are #strong[hints]; about usage, and the processor #strong[says]; that it does not use them yet
      (`W-NOT-YET`). It does not silently ignore them.
    ]
    #para("4")[
      Not being enforced does not mean they may be written carelessly. A mode is a #strong[claim]; about what this op does,
      and the processor may someday question that claim.
    ]
  ]
  #sub("6.4.4", "How to write contracts")[
    #para("1")[
      Conditions are written in prefix notation. Comparison and logical operations, arithmetic, `len` and `idx` can be used.
    ]
    #para("2")[
      If there are several `requires`, #strong[all]; shall be true.
    ]
    #para("3")[
      A condition about #strong[every element]; of a slice is written with the following four. `elem_lt <slice> <value>`
      means #emph["every element is less than that value"];, and `elem_le` · `elem_gt` · `elem_ge` likewise.
    ]
    #para("4")[
      These four exist because a contract #strong[cannot write going through one by one];. A condition is a pure
      expression and a loop is a statement, so to speak about all the elements there has to be a word that says it.
    ]
    #ex("a contract", "module ex_contract .

rem This op reads two bytes from a slice and makes one larger number.
export fn read_pair input data slice u8 . output u32 .
  requires ge len data. . 2 . .
  ensures le ret. 65535 . .
do
  let hi u32 widen u32 idx data. 0 . . .
  let lo u32 widen u32 idx data. 1 . . .
  return add mul hi. 256 . lo. . .
end .")
    #plain[
      How to read the example above: #emph["this function shall receive a row of bytes of length 2 or more (requires),
      promises that the value it returns is at most 65535 (ensures), and touches nothing in the outside
      world (effects none)."]; — this much can be known without opening the body.
    ]
  ]
  #sub("6.4.5", "Giving a contract a name")[
    #para("1")[
      If several ops require the same condition, that condition can be given a name. It is written with `contract`.
    ]
    #para("2")[
      An op writes that name with `satisfies` and so takes on that contract.
    ]
    #ex("a named contract", "module ex_named .

rem If several ops require the same condition, that condition can be given a name.
contract nonneg do
  requires ge a. 1 . .
end .

fn half satisfies nonneg . input a u8 . output u8 .
do
  return div a. 2 . .
end .")
    #plain[
      If the same condition is repeated by hand in several places, one gets fixed and another forgotten.
      Giving it a name makes one place to fix.
    ]
  ]
  #sub("6.4.6", "Contracts remove checks")[
    #para("1")[
      The processor #strong[takes contracts as facts]; and narrows the range a value can have. If that range shows safety,
      it removes that check.
    ]
    #para("2")[
      The checks that can be removed are these — overflow, division by 0, narrowing, slice bounds.
    ]
    #para("3")[
      If a contract #strong[is not enforced, it is not used as a fact.]; That is, nothing is believed without confirmation.
      Believing without confirmation and removing checks is not being faster but being wrong.
    ]
    #para("4")[
      The processor can report where checks it could not remove remain. A performance model that does not report
      is itself knowledge outside the source (#cref("1.3")).
    ]
    #ex("contracts remove checks", "module ex_elim .

rem Without a contract, the overflow check remains.
export fn bare input a u8 . output u8 .
do
  return add a. 1 . .
end .

rem With a contract, the processor proves it and removes the check.
export fn proven input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .")
    #note[
      The two ops have the same body. The only difference is one line of contract, and that one line removes one check during
      execution. This is the largest thing a contract does in this language.
    ]
    #diagram("a contract becomes a fact at entry and removes the overflow check", " proven 250  ──▶ requires le a 200   entry check fails → E-VM-CONTRACT (caller's fault)
 proven 150  ──▶ requires le a 200   passes → from here on a ∈ [0, 200] is a fact
                 add a 1             a + 1 ∈ [1, 201] ⊂ u8 [0, 255] → overflow check removed
 bare 255    ──▶ add a 1             no fact about a → the check stays → E-VM-OVERFLOW")
  ]
  #sub("6.4.7", "The build mode decides the remaining checks")[
    #para("1")[
      The #t("빌드 모드", "build mode") is written in the source as `build <mode> .`. A proved contract leaves no check in any
      mode. What the mode decides is the disposition of #strong[contracts that could not be proved];.
    ]
    #para("1a")[
      The modes are #strong[a closed four]; — `debug` · `test` · `release_safe` · `release_fast`.
      Any other word is refused (`E-BUILD-MODE`).
    ]
    #para("2")[
      `debug` keeps the remaining checks and, if one breaks, stops saying what broke.
    ]
    #para("2a")[
      `test` keeps checks like `debug`. What runs tests (#cref("6.4.13")) is not this mode but #strong[a request to run
      tests]; — that request runs tests in any mode, and even in `test` mode an ordinary execution does not run tests.
      If an `expect` inside a test is false, that test fails (`E-TEST-FAIL`) — it is not the processor's
      error but #strong[the claim of the person who wrote it being wrong];.
    ]
    #para("3")[
      `release_safe` keeps the remaining checks and stops if one breaks. What it reports when it stops is #strong[a short fixed message]; naming the kind of check that broke — the contract's text (the source
      expression) is not put in the executable. What differs from `debug` is that, as in (4), it removes contracts of `debug` grade.
    ]
    #para("4")[
      `release_fast` #strong[removes]; the checks of `requires` that could not be proved — both those written on the op and those of named contracts
      taken in with `satisfies`. `release_safe` removes the `requires` checks of `debug` grade. No mode removes the exit checks of
      `ensures`.
    ]
    #para("4a")[
      A contract whose checks the mode removed #strong[is not a fact.]; The processor does not remove other checks on the ground of that contract — the op
      runs #strong[as if that contract had not been written];. So if a call that broke the contract overflows or goes out of range inside that op,
      the check of that computation stops it. This is for the same reason that `assume` is not a fact (#cref("6.4.9")).
    ]
    #para("4b")[
      The checks the processor puts at each computation — overflow · division by 0 · `MIN / -1` · indices · shift counts · narrowing · tags — are not removed
      in any mode. Those checks disappear only when they are proved.
    ]
    #para("5")[
      That is, `release_fast` is the same as opening one more #strong[trust boundary]; (#cref("4.4")) — a person promises that the contracts
      are true. If the promise is wrong, the program runs the same as a program written without the contracts.
      It does not go into undefined behaviour.
    ]
    #rejected("the build modes are a closed four", "module ex_build_mode .

build nosuchmode .

fn f output u8 . do return 1 . end .", "E-BUILD-MODE")
  ]
  #sub("6.4.8", "Names and scopes")[
    #para("1")[
      Names nest in four layers — the module, the op, the block, and blocks inside it.
    ]
    #para("2")[
      In one scope a name points at exactly one entity. A name already alive #strong[cannot be hidden]; by declaring it
      again. Doing so is refused at translation.
    ]
    #para("3")[
      The things that cannot be hidden are these four — names in the module, the names of core ops (#cref("6.1.3")),
      the parameter names of the same op, and local names still alive in an outer block.
    ]
    #para("4")[
      Declaring the same name twice in the same block does not conform either. The second declaration #strong[does not replace];
      the first — the same characters would merely point at two entities.
    ]
    #para("5")[
      On leaving a block, the names declared in that block disappear. So two side-by-side blocks each declaring the same name
      conforms — the two names are not alive at the same time.
    ]
    #rejected("a module name cannot be hidden", "module ex_shadow .

let g u32 7 .

proc p output u32 . effects none .
do
  let g u32 1 .     rem hides the module's `g`
  return g. .
end .", "E-NAME-SHADOW")
    #rejected("a parameter cannot be hidden", "module ex_shadow_param .

proc p input n u32 . output u32 . effects none .
do
  let n u32 1 .     rem hides the parameter `n`
  return n. .
end .", "E-NAME-SHADOW")
    #rejected("the same name cannot be declared twice in the same block", "module ex_shadow_twice .

proc p output u32 . effects none .
do
  let a u32 1 .
  let a u32 2 .     rem does not replace the first
  return a. .
end .", "E-NAME-SHADOW")
    #rejected("an inner block cannot hide an outer name", "module ex_shadow_inner .

proc p input n u32 . output u32 . effects none .
do
  let a u32 1 .
  guard gt n. 0 . else do
    let a u32 2 .   rem the outer `a` is still alive
    return a. .
  end .
  return a. .
end .", "E-NAME-SHADOW")
    #para("6")[
      The order of looking up names is local → module → imported modules. If imported names collide,
      which one is meant shall be written out.
    ]
  ]
  #sub("6.4.9", "Grades of contracts")[
    #para("1")[
      A #t("등급", "grade") can be attached to one contract clause to fix #strong[when and with what that condition is handled];
      — it comes directly after the clause word, as in `requires <grade> <condition> .`. The grades are the following three
      and no more.
    ]
    #term("등급", "grade")[
      A mark attached to one contract clause that fixes #strong[when, and who, is responsible for]; that condition. At translation
      the processor proves it (`static`), at run time it is checked (`debug`), or a person
      promises that it is true (`assume`).
    ]
    #tbl("Grades of contracts — a closed set of three")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Grade*], [*When it is looked at*], [*Meaning*],
      [`static`], [at translation], [the processor #strong[shall prove it];. If it cannot, translation fails],
      [`debug`], [at run time], [looked at only while the build mode keeps checks],
      [`assume`], [#strong[not looked at];], [only written down. The processor #strong[does not use it as a fact];],
      )
    ]
    #para("2")[
      If no grade is written, the processor decides — it proves what it can prove, and what it cannot it keeps as the build
      mode (#cref("6.4.7")) decides. That is, a grade is #strong[a mark that overturns the default disposition];, not
      something needed to write a contract.
    ]
    #para("3")[
      A condition that is `assume`d #strong[is not used as a fact.]; The processor neither keeps it nor relies on it
      to remove other checks — that is, that condition is #strong[something said to the reader];.
    ]
    #para("4")[
      This is normative because the opposite does not hold. If a condition that is not kept were taken as a fact,
      then when that condition is false the processor would believe it is right #strong[while having removed checks it should not have removed];.
      That is, to use a contract as a fact #strong[it shall be enforced];, and what is not enforced is not a fact.
    ]
    #tbl("The same condition, different dispositions")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Written*], [*At entry*], [*Following checks*],
      [`requires le a 200`], [checked], [#strong[removed]; — because it became a fact],
      [`requires assume le a 200`], [not checked], [remain — because it is not a fact],
      )
    ]
    #plain[
      Then what is `assume` for? It is the place to #strong[write down]; what the processor cannot prove yet but a person
      knows. Written down, the reader knows it, and when the processor grows later that place can be raised to
      `static`. Not written down, that knowledge stays only in a person's head.
    ]
    #ex("the same program ends differently by mode — `debug`", "module ex_mode_debug .

build debug .

fn bump input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .",
      out: "bump(10) = 11 · bump(250) → E-VM-CONTRACT (trap)")
    #ex("the same program — `release_fast`", "module ex_mode_fast .

build release_fast .

fn bump input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .",
      out: "bump(10) = 11 · bump(250) = 251 (does not stop)")
    #ex("the same mode — where the contract kept the overflow away", "module ex_mode_fact .

build release_fast .

fn bump input a u8 . output u8 .
  requires le a. 254 . .
do
  return add a. 1 . .
end .",
      out: "bump(10) = 11 · bump(255) → E-VM-OVERFLOW (trap)")
    #caution("`release_fast` is outside the safe subset")[
      The same program ends differently by mode. Breaking the contract `requires le a. 200 . .` by calling
      with `a` as 250 stops in `debug` (`E-VM-CONTRACT`), and in `release_fast`
      #strong[does not stop and gives 251.]; In the third example the contract was the ground for proving that `add a. 1 .` does not overflow. When the mode
      removes the contract, that ground goes too, so the overflow check comes back, and `bump(255)` stops instead of giving 256 (#cref("6.4.7") (4a)).
      This is one of the reasons this document limits its claim of having removed undefined behaviour to
      #strong[the safe subset]; (#cref("4.4")). Removing checks for speed is something that may be chosen, but
      #strong[the fact of choosing shall remain in the source]; — that is why the mode is not a command-line flag but a `build`
      statement.
    ]
  ]
  #sub("6.4.10", "Violations that can be decided now")[
    #para("1")[
      If a call site breaks the callee's `requires` and #strong[both sides are constants];, that violation is decided
      #strong[at translation];, without waiting for execution. It is then refused
      (`E-CONTRACT-IMPOSSIBLE`).
    ]
    #para("2")[
      This does not make contracts stricter; it brings forward #strong[when the answer comes];.
      Before, it was translated green and caught a trap during execution.
    ]
    #plain[
      If a contract whose answer comes at translation is postponed until execution, that program becomes a program that #strong[is known
      to be wrong only by running it];. Programs carrying a bit budget that did not fit really were
      shipped. If the answer comes now, it is said now.
    ]
    #rejected("both sides are constants, so it is decided now", "module ex_impossible .

fn g input a u8 . output u8 . requires lt a. 10 . .
do
  return a. .
end .

fn f output u8 .
do
  return g 200 . .       rem 200 is never less than 10
end .", "E-CONTRACT-IMPOSSIBLE")
  ]
  #sub("6.4.11", "What cannot be a name")[
    #para("1")[
      Apart from what cannot be hidden (#cref("6.4.8")), there are #strong[characters that cannot be a name in the first place];.
    ]
    #tbl("What cannot be used as a name")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*What*], [*Diagnostic*], [*Why*],
      [keywords], [`E-NAME-KEYWORD`], [a keyword is a mark by which the reader #strong[tells construct from value at a glance];],
      [names of core ops], [`E-NAME-BUILTIN`], [the namespace #strong[is flat]; (no hiding), so that name would mean two things],
      [a lone `_`], [`E-NAME-WILDCARD`], [a `_` standing alone is a mark that means #strong[anything];, not a name],
      [names beginning with `__`], [`E-NAME-VENDOR-RESERVED`], [a place #strong[reserved]; for those who make processors and libraries],
      [characters outside ASCII], [`E-NAME-ASCII`], [text · strings · `rem` comments hold any character, but #strong[names are ASCII];],
      )
    ]
    #para("1a")[
      Here «core op» is not only those named in #cref("6.3"). Anything #strong[called by its bare name]; belongs to that set —
      the table of core ops in annex D is all of it. The names of builtin ops that stand after `call_builtin` (files · the network · processes · standard input and output ·
      clocks · random numbers · the environment · terminals · floating-point functions …) are not included — they are in their own namespace (#cref("6.3.3") (1f)).
      #strong[That the names of core ops are taken is normative]; — using one as a local name is refused.
    ]
    #plain[
      It may look strange to rule «names in that list cannot be used» without writing the list.
      But the two questions are different — #strong[what exists]; is answered by the counting side (the ledger), and
      #strong[whether it occupies the namespace]; is answered by the norm. The former grows and shrinks, but the latter is
      always true, and #strong[only what is always true is written in this document.];
    ]
    #para("2")[
      Two imported names #strong[being bound to the same name]; is refused too (`E-NAME-COLLISION`). Importing
      #strong[does not overwrite]; a name that already exists.
    ]
    #para("3")[
      In a dotted name `<front>.<back>`, the front shall be #strong[a declared type]; (`E-NAME-QUALIFIER`).
      That shape puts the op in the namespace of that type; it does not point at a module.
    ]
    #plain[
      Why ASCII? This language lets text and comments hold any character while keeping #strong[only names]; narrow.
      Names are not only read by people but moved, attached and compared by tools, and characters that cannot be told apart by eye
      becoming different names makes #strong[mistakes that cannot be found];.
    ]
    #caution("")[
      What reserving `__` protects is not the processor but #strong[the person writing];. Once it is fixed that that place always
      means «something internal», when reading someone else's code you know from the name alone that it is not yours.
    ]
  ]
  #sub("6.4.12", "What contracts refer to")[
    #para("1")[
      The names a contract clause calls shall be #strong[visible]; at that place. Calling a name that does not exist
      is refused (`E-REQ-UNDEF` · `E-ENS-UNDEF`).
    ]
    #para("2")[
      A contract that calls something the processor #strong[cannot bring]; to that place is refused too
      (`E-REQ-UNSUP` · `E-ENS-UNSUP`). It does not pretend to judge what it cannot judge.
    ]
    #para("3")[
      If a named contract (#cref("6.4.5")) points at something not declared, it is refused
      (`E-CONTRACT-UNDEF`).
    ]
    #para("4")[
      Declaring an error that #strong[cannot]; happen is refused (`E-CONTRACT-DEAD`) — if `requires`
      already filters out that case, that error branch never occurs.
    ]
    #para("4a")[
      If two `requires` on the same inputs #strong[cannot be true together];, it is refused
      (`E-CONTRACT-UNSAT`) — whatever arguments it is called with, it stops at entry, so the body of that op never
      runs. The same place as (4): code that does not run is written in the signature.
    ]
    #plain[
      What (4) prevents is not a wrong program but #strong[a wrong picture];. If a failure that never occurs is written in
      the signature, the caller writes code to handle it, and that code never
      runs. Code that does not run is not tested, and untested code is wrong someday.
    ]
    #para("4b")[
      A contract clause the processor #strong[cannot enforce]; reports that fact (`W-CONTRACT-IGNORED`).
      The entry check (`requires`) and the exit check (`ensures`) each know certain shapes; other shapes are
      stopped by nobody during execution, and interval analysis does not learn from them either. Such a clause stands only in documentation.
    ]
    #plain[
      (4b) pairs with (2). (2) refuses #strong[what cannot be brought];, and (4b) reports #strong[shapes that can be brought
      but cannot be set up];. Both keep the same one thing — a promise that is not checked is not left looking as if it were
      checked. On the `ensures` side this report did not exist until 2026-09-16, so such clauses were
      #strong[silently discarded]; (defect note #11).
    ]
    #para("5")[
      If the type passed to a translation-time parameter #strong[does not provide what that place requires];,
      it is refused (`E-BOUND-UNSAT`, #cref("6.11.2")).
    ]
  ]
  #sub("6.4.13", "Test blocks")[
    #term("시험 블록", "test block")[
      A body without inputs or outputs, written `test <name> do … end .`. It is executed only when a request to run tests is received.
    ]
    #term("단언", "assertion")[
      `expect <condition> .` — a condition a test claims to be true. If it is false, that test fails.
    ]
    #para("1")[
      Test blocks come at the top level of a module. The head has the name and, if present, a `schedule` clause (#cref("6.4.1") (3d)); the body has
      the forms that come in the body of an op. The body is checked by #strong[the same type rules]; as the body of an op — being inside a test
      does not let `let x u8 300 .` pass.
    ]
    #para("1a")[
      The name of a test is in #strong[the same namespace]; as the names of ops · types · modules. If tests in one module, or a test and
      another declaration, use the same name, it is refused (`E-NAME-DUP`, #cref("6.4.8") (2)). Tests are reported by name and called
      by name, so if names overlap it cannot be said which is meant.
    ]
    #para("2")[
      The condition of `expect <condition> .` shall be `bool`. Giving a number is refused (`E-TYPE-COND`) — this language has no rule
      that reads non-zero numbers as true (#cref("6.2.16") (4)).
    ]
    #para("2a")[
      If, when tests are run, the condition of an `expect` is false, that test fails (`E-TEST-FAIL`). This is not a contract violation
      — a contract violation is code breaking #strong[its own promise];; a test failure is #strong[the claim of the person who wrote it]; differing
      from the code. The place to fix is different.
    ]
    #para("2b")[
      The processor does not remove `expect` in any build mode (#cref("6.4.7")). A vanished assertion is a test that did not run.
    ]
    #para("2c")[
      `expect` comes only inside test blocks. Writing it in the body of an op or anywhere else is refused (`E-EXPECT-PLACE`).
      Promises about inputs or results are written with `requires` · `ensures` (#cref("6.4.3")), and stopping deliberately with `panic` (#cref("7.1")).
    ]
    #para("3")[
      Tests are executed only when a request to run tests (`lowentc --test`) is received. Ordinary execution and checking (`--check`)
      do not run tests, and checking reports that fact (`W-TEST-NOT-RUN`) — so that passing the check is not
      read as passing the tests.
    ]
    #para("3a")[
      The tests of a unit that translation refuses do not run. A request to run tests also goes through the same checks as `--check` first.
      If a refused program reports «pass», that green is false.
    ]
    #para("3b")[
      The result of running tests is the number of tests run · the number passed · the number failed. If there are no tests at all, it says so.
      If even one test failed, that request ends in failure.
    ]
    #para("4")[
      A test with the clause `schedule explore_interleavings [limit <number>] .` is run repeatedly while changing #strong[the order]; in which messages are
      delivered. Without `limit` it runs up to a bound the processor decides. If it runs through every order before the bound, the result is reported
      as «exhaustive»; if it stops at the bound, as «up to the bound». Stopping at the bound is not reported as exhaustive.
    ]
    #para("4a")[
      If it passes in every order, the test passes. If it passes in some orders and fails in others, the
      test fails (`E-SCHED-NONDET`) — the result depends on the order of delivery, and the answer from running it once is only the answer for that one
      order. If it fails in every order, it is an ordinary test failure (`E-TEST-FAIL`).
    ]
    #ex("test blocks and assertions", "module ex_test .

fn clamp8 input v u64 . output u8 .
do
  return narrow_sat u8 v. . .
end .

test clamp_keeps_small do
  expect eq clamp8 7 . 7 . .
end .

test clamp_saturates do
  expect eq clamp8 1000 . 255 . .
end .")
    #rejected("the condition of an assertion is bool — a number is not true", "module ex_test_num .

test counts do
  expect 1 .
end .", "E-TYPE-COND")
    #rejected("two tests in one module shall have different names", "module ex_test_dup .

test same do
  expect eq 1 1 . .
end .

test same do
  expect eq 2 2 . .
end .", "E-NAME-DUP")
    #rejected("tests and ops share one namespace", "module ex_test_ns .

fn check output bool . do return true . end .

test check do
  expect check. .
end .", "E-NAME-DUP")
    #rejected("expect comes only inside test blocks", "module ex_expect_place .

fn half input n u8 . output u8 .
do
  expect lt n. 200 . .
  return div n. 2 . .
end .", "E-EXPECT-PLACE")
  ]
]

#import "../lib.typ": *

#sub("6.6", "Telling variants apart — `match`")[
  #para("1")[
    `match` looks at which variant an `enum` value is and executes the matching block.
  ]
  #para("2")[
    `match` #strong[shall cover every variant.]; If even one is missing, translation is refused.
  ]
  #para("3")[
    `match` can divide not only `enum`s but integers too. Then one branch is a single value or a #strong[range];,
      and a range is written `<first> to <last>` and includes both ends.
  ]
  #para("4")[
    When dividing integers too, #strong[every value shall be covered];. Either the ranges cover the values the type can have without gaps,
      or there is a branch that takes the rest (`_`).
  ]
  #para("5")[
    For one branch to take several values, join them with `or`.
  ]
  #para("6")[
    A later branch writing again what an earlier branch already covered is refused
      (`E-MATCH-REDUNDANT`). That branch #strong[never runs];, and code that does not run being there
      is a sign that the writer's meaning and the program's meaning have parted.
  ]
  #para("7")[
    #strong[A bare name written in `case` is always a variant.]; If it is not a declared variant, it is refused
      (`E-MATCH-UNDEF`). To catch anything, use `_`.
  ]
  #plain[
    Before 2026-09-03, a bare name that was not a variant was read as #strong[a name that binds the value];, so
    a typo in a variant name, far from being caught, became #strong[a branch that catches everything]; and even satisfied
    the missing-branch check of (2). Adding one more variant said nothing either — that typo branch swallowed
    the new variant too. #strong[A place where the processor fails to catch the easiest mistake to catch is not a rule
    but a hole.];
  ]
  #para("7a")[
    If more names follow a name, it is #strong[destructuring]; — unpacking a variant that carries values
      (#cref("6.2.19")) or binding the fields of an aggregate. The first name in that position need not be a variant
      (it may be the name of an aggregate).
  ]
  #para("7b")[
    If the name in `case error <name>` is a #strong[declared variant]; of that error enum, it takes only that variant.
      If it is not a declared variant, the whole error value is bound to that name (that branch takes every error).
      This applies to this place, unchanged, the discipline (7) fixes for the whole of `match` — a bare name that is a variant is a
      variant.
  ]
  #plain[
    Before 2026-09-16, the name in this place was #strong[always read as a binding];. So `case error not_digit .`
    also took the `empty` error, a `match` that wrote only one of two variants passed the missing-branch check,
    and writing one per variant had the second refused as «already handled» (defect note #52). Writing a variant name
    and having #strong[another variant come into that branch]; is exactly the mistake (7) set out to prevent.
  ]
  #para("8")[
    When dividing a variant that carries values (#cref("6.2.19")), the number of binding names shall #strong[equal]; the number of things
      that variant carries (`E-MATCH-ARITY`).
  ]
  #para("8a")[
    Putting `else do … end` in the last place of a `match` makes it #strong[the branch that takes the rest]; —
      it does the same as `case _ .` (the grammar of annex A.7 already writes that place). The missing-branch check
      counts it as covering, and writing more branches after it is caught by (6).
  ]
  #para("8b")[
    When the pattern of a branch is #strong[nested]; (`case ok (some x)` · `case ok none`), its outer tag
      counts as covered #strong[when the inner pattern is itself exhaustive];. The inner being exhaustive means that the inner
      has `_` or a binding name, or the inner wrote both `some` and `none`, or the inner branches wrote every variant of that
      enum.
  ]
  #plain[
    Before (8b), nested branches were #strong[not counted at all];, so a `match` that divided exhaustively was
    required to have a `_`. A useless `_` says nothing even when variants grow later, so forcing it
    was #strong[making people switch off]; the missing-branch check of (2).
  ]
  #para("9")[
    For branches joined with `or` to bind values, #strong[every branch shall have the same name in the same place];
      (`E-MATCH-ORBIND`). Otherwise that name would point at different things depending on which branch it came
      from.
  ]
  #plain[
    If (2) · (4) prevent #strong[gaps];, (6) prevents #strong[overlaps];. The two are the front and back of the same discipline —
    dividing branches is dividing the space of values #strong[without gaps and without overlaps];, and keeping only one side
    is not dividing but merely laying things out.
  ]
  #ex("dividing by ranges", "module ex_range_match .

fn band input a u8 . output u8 .
do
  match a. do
    case 0 to 9 do return 1 . end .
    case 10 to 255 do return 2 . end .
  end .
end .",
    out: "band(5) = 1 · band(200) = 2")
  #rejected("if there are values not covered, it is refused", "module ex_range_gap .

fn band input a u8 . output u8 .
do
  match a. do
    case 0 to 9 do return 1 . end .
  end .
end .", "E-MATCH-INEXHAUSTIVE")
  #plain[
    Leaving a gap is refused at translation, so the author always has to say #emph["what is the rest"];.
    In other languages this place silently does nothing or becomes an exception, and neither can be known
    by reading the source.
  ]
  #ex("match covers every variant", "module ex_match .

enum color do
  red .
  green .
end .

export fn code input c color . output u32 .
do
  match c. do
    case red do return 1 . end .
    case green do return 2 . end .
  end .
end .")
  #rejected("a match with a variant missing", "module ex_match_bad .

enum color do
  red .
  green .
end .

export fn code input c color . output u32 .
do
  match c. do
    case red do return 1 . end .
    rem `green` was not handled
  end .
end .", "E-MATCH-INEXHAUSTIVE")
  #plain[
    The same can be done by chaining conditionals. The one advantage of `match` is #strong[completeness];
    — when one more variant is made, the processor finds every place that does not handle it.
    A chain of conditionals does not find those places.
  ]
  #note[
    Adding a new variant is common. If a person has to remember #emph["where all must this variant be handled"];,
    they will certainly miss something. `match` hands that memory over to the processor.
  ]
  #sub("6.7", "Tests — `test` and `expect`")[
    #para("1")[
      A `test` block is the place to write the tests of that module. The processor can run it.
    ]
    #para("2")[
      `expect` is the assertion of a test. If the condition is false, that test fails.
    ]
    #para("3")[
      The processor #strong[does not remove `expect` by optimisation.]; A vanished test is a test that did not run.
    ]
    #para("4")[
      Only translating does not run tests. Translation having passed does not mean #emph["the tests passed"];.
    ]
    #ex("a test", "module ex_test .

export fn twice input n u32 . output u32 .
  requires le n. 100 . .
do
  return mul n. 2 . .
end .

test twice_works
do
  expect eq twice 5 . 10 . .
end .")
    #caution("A contract violation and a test failure are different")[
      A contract violation is #strong[the code breaking its own promise];, and a test failure is #strong[a test saying that the code is
      wrong];. The two come out as different diagnostics — because what needs fixing differs.
    ]
  ]
  #sub("6.8", "Translation-time parameters — `comptime`")[
    #para("1")[
      A parameter marked `comptime` #strong[shall have its value fixed at translation time];. Types can be received that way too.
    ]
    #para("2")[
      This is the way to use the same op for several types. The processor makes a concrete instance for each combination used.
    ]
    #para("3")[
      That making #strong[is visible in the source.]; There are no angle brackets and no inference — which combinations were made is written
      at the call sites.
    ]
    #para("4")[
      Giving a `comptime` position a value not known at translation time does not conform.
    ]
    #para("4-1")[
      Values known at translation time are integer literals, module constants, `comptime <expr>` (the computation of #cref("6.8.1")), and local `let`s in the same op whose value
      is computed as in #cref("6.8.1") (1a). `var`s and run-time values are not. A parameter used as a lane count (`vec <t> <n>`) is given only integer literals and module constants — because
      a concrete instance is made for each such place.
    ]
    #para("4a")[
      #strong[Leaving empty]; a `comptime` position that takes a type does not conform either (`E-MONO-NOTYPE`).
      It is not traced back from the types of the arguments — as (3) says, what is made shall be written
      at the call site.
    ]
    #para("5")[
      A concrete instance is made for each combination used, so the number of combinations is the amount of code made. This cost
      can be counted at the call sites.
    ]
    #ex("receiving a translation-time value", "module ex_comptime .

fn twice input comptime n u8 . input a u8 . output u8 .
do
  return add a. n. . .
end .

fn call_it output u8 .
do
  return twice 3 4 . .
end .",
      out: "call_it() = 7")
    #rejected("a value not known at translation time cannot be given", "module ex_comptime_rt .

fn twice input comptime n u8 . input a u8 . output u8 .
do
  return add a. n. . .
end .

fn call_it input k u8 . output u8 .
do
  return twice k. 4 . .
end .", "E-COMPTIME-ARG")
    #note[
      It overlaps with what generics do in other languages, but here #strong[nothing is implicit];. What is made
      shall be knowable from the source alone, and that is why this language does not use inference.
    ]
  ]
  #sub("6.8.1", "It shall be computable at translation time")[
    #para("1")[
      An expression in a `comptime` position #strong[shall produce its value at translation];. If the current processor cannot compute it,
      it is refused (`E-COMPTIME-NONCONST`).
    ]
    #para("1a")[
      What the current processor computes is integer literals, module constants, local `let`s whose value came out at translation time,
      `machine.*`, `native_lanes <type>`, `config <name>`, and pure integer builtins
      (`add` · `sub` · `mul` · `div` · `mod` · `neg` · comparisons · `min` · `max` · bit operations · `shl` · `shr` · logic).
      A local `let`'s value coming out at translation time means that its value is a literal · one such name · `native_lanes <type>` ·
      `comptime <expr>`. `var`s and inputs (`comptime` inputs too — the concrete instance of #cref("6.8") is one, and the value is handed over at
      execution) are not computed. If a computation goes beyond the range of values (overflow of signed 64-bit · unsigned 64-bit, division by 0, mixing unsigned values and
      negative numbers), that expression is not computed — an expression that would stop at execution is not turned into a different number at translation.
    ]
    #para("1c")[
      Calling a pure `fn` is computed too. The processor actually runs that `fn` at translation and puts the answer in that place.
      A `proc`, which can produce effects, is not run. The inputs are one integer or `bool`, slices of numbers · `bool`s, structs, and slices of structs.
      Integer arguments shall be computable as in (1a), and slice arguments shall have their bytes fixed at translation time — quoted strings · `text`
      literals (as written) · files read at translation (1g) · `lit array` · `lit slice` whose elements are all constants · array or slice module constants (1d)(1e).
      Struct arguments shall be structs with a byte layout (#cref("6.2.6") (9)), and either a `lit s do … end` in which each field has a value computable as in (1a)
      (inner structs written the same way) or a struct answer (1f) — a module constant or a call in that place. The `fn` receives that struct as it would at
      the boundary of the program. In a 128-bit field, write one number literal. A slice-of-structs argument is a `lit slice s …` of such literals or
      an array · slice answer of structs (1d)(1e) — if it is the answer of a struct with slice fields, the processor hands over the bytes those fields point at as well.
      `mut` inputs are not accepted. If that `fn` overflows, breaks a contract or does not end within the fixed number of steps, or
      an argument cannot be known at translation time, it is refused with `E-COMPTIME-NONCONST`, and the message says why.
    ]
    #para("1c-1")[
      The fixed number of steps is a number the processor states, and the build settings change it — `build comptime_steps <n> .` in the manifest, and above that
      the command line `--comptime-steps <n>`. An answer run at translation can be kept and used again in the next translation; a kept answer is used only when that `fn`'s definition ·
      input bytes · processor edition · target · step budget are all the same. With or without keeping, the answers and refusals are the same.
    ]
    #para("1d")[
      An `fn` whose answer is an array (#cref("6.2.6") (1c)) is run too. The elements are sized numbers · `bool` · rows of such elements (arrays inside arrays) · structs (#cref("6.2.6") (9) · (9-1)). The processor provides the place of the result,
      runs that `fn`, and the bytes after running become #strong[read-only]; bytes fixed in the program — the same place as a list whose elements are all constants
      (#cref("6.2.6") (7) ⓑ). That answer is given a name with the same length and element type as the result of `f`, as in `let x array n t comptime f … .`.
      It may be a `let` inside a body or a module constant. Elements cannot be written through that name — `set` · `copy` · `swap` are refused (`E-TYPE-MUT` ·
      `E-IMMUTABLE` for `set` on a module constant), and passing it to a `mut` position is refused (`E-TYPE-ARGMUT`). Binding it with `var`, binding it to a name of
      a different length or element type, or putting it in the middle of an expression is refused (`E-RESULT-PLACE`). Whether two answers with the same contents are the same place is, as in ⓑ,
      a #t("자유 선택", "free choice") (FC-0003).
    ]
    #para("1d-1")[
      Only pure `fn`s run at translation, so such an `fn` builds no new storage — the place of the answer is provided from outside (by the processor), and slice fields inside the answer
      point at the `fn`'s inputs or at constant bytes of the program. The processor puts the bytes pointed at into the read-only place of (1d) and rewrites the addresses of the fields
      to that place. If a field points elsewhere (at bytes in that `fn`'s frame), it is refused. If the number of answers cannot be known in advance, write a generous length and count
      how many were filled separately (the example below).
    ]
    #para("1e")[
      An `fn` whose answer is a slice of numbers · `bool`s · structs (`output slice t .`) is run too. The bytes of the answer are copied and kept as the same #strong[read-only]; bytes as (1d)
      — whether that `fn` gave an answer pointing at a string or a piece of its input. If the elements are structs with slice fields, the addresses of the fields
      are rewritten as in (1d-1). The name is given with `let x slice t comptime f … .`; it may be a `let` inside a body or a module constant, and writing is
      refused as in (1d). A slice of rows (`output slice (array m t) .`) is not run (`E-COMPTIME-NONCONST`) — answers of rows are given as arrays (1d).
    ]
    #para("1f")[
      An `fn` whose answer is a struct is run too. The answer is a #strong[value]; of that struct — the same as a `lit s do … end` that writes each field's run value as a constant (not a window `view s`).
      The fields are numbers · `bool` · arrays and slices of numbers (their bytes are put in the read-only place of (1d)) · structs with only such fields.
      A 128-bit field goes in as it is, up to its upper half. If there is a field holding `option` · `result` · a reference, it is not run (`E-COMPTIME-NONCONST`).
    ]
    #para("1g")[
      `include_text "<path>"` is the bytes (`slice u8`, read-only) of the file at that path, read at translation. The path is looked up from the folder of the source file containing
      this text and stays inside that folder — absolute paths and `..` are refused, and so is reaching outside that folder by following links. This shape stands only in the input position of an `fn` run at translation (1c). In other places
      it is refused (`E-INCLUDE-PLACE`) — because it would read as if the file were read at execution. If the file cannot be read, points outside the folder, or exceeds the size the
      processor states, that `comptime` place is refused saying why. Those bytes are the input bytes of (1c-1) — if the file changes, a kept answer
      is not used. In a build where the processor pins dependencies with a lock file, files read are also pinned #strong[by content];, like dependencies: the entry's name is `include:<the path seen
      from the manifest folder>`, and a different content is refused with `E-DEP-HASH`, a missing entry with `E-DEP-UNLOCKED`. The pinning entries are written by `--lock-write`.
    ]
    #para("1b")[
      A computed answer is #strong[a constant];. The receiving place measures that answer like a literal — if it does not fit the type of the place,
      it is refused like a literal (`E-TYPE-WIDTH`). The types of the names used in the answer do not become the type of the answer.
    ]
    #para("2")[
      A `let` at module level is #strong[a named constant];. Its value too shall come out at translation time
      (`E-CONST-NOTCOMPTIME`). The value is an integer literal or `comptime <expr>`. That expression can read other module constants,
      and it is the same whether the constant read is before or after it. If they read each other and no value comes out, it is refused.
    ]
    #caution("")[
      What (1) says is not «this expression is not a constant» but «#strong[today's processor]; cannot
      compute this». The two are different, and this document does not hide the difference — if what cannot be done
      is written as if it were impossible in principle, nobody notices when it becomes possible later.
    ]
    #rejected("no value comes out at translation", "module ex_comptime .

fn f input a u8 . output u8 .
do
  let b u8 comptime a. . .    rem `a` is fixed only at execution
  return b. .
end .", "E-COMPTIME-NONCONST")
    #ex("computing a constant from constants", "module ex_comptime_names .

let KIB u64 1024 .
let PAGE u64 comptime mul 4 KIB. . . .    rem reads another module constant

fn f output u64 .
do
  let extra u64 4 .
  return comptime add PAGE. extra. . . .
end .",
      out: "f() = 4100")
    #ex("running a pure function at translation", "module ex_comptime_fn .

fn fact input n u64 . output u64 .
do
  var r u64 1 .
  var i u64 1 .
  while le i. n. . do
    set r. mul r. i. . .
    set i. add i. 1 . .
  end .
  return r. .
end .

fn f output u64 .
do
  return comptime fact 10 . . .    rem at execution it is one number
end .",
      out: "f() = 3628800")
    #ex("building a table at translation", "module ex_comptime_table .

fn crc_table output r array 256 u32 .
do
  var n u32 0 .
  while lt n. 256 . do
    var c u32 n. .
    var k u32 0 .
    while lt k. 8 . do
      if eq bit_and c. 1 . 1 . do set c. bit_xor 3988292384 shr c. 1 . . .
      end else do set c. shr c. 1 . . end .
      set k. add k. 1 . .
    end .
    set idx r. cast usize n. . . c. .
    set n. add n. 1 . .
  end .
  return r. .
end .

let CRC array 256 u32 comptime crc_table . . .    rem at execution it is one read-only table

fn f output u32 .
do
  return idx CRC. 255 . .
end .",
      out: "f() = 755167117")
    #ex("trimming text at translation", "module ex_comptime_slice .

fn trim input s slice u8 . output slice u8 .
do
  var a usize 0 .
  var b usize len s. . .
  while and lt a. b. . eq idx s. a. . 32 . . do set a. add a. 1 . . end .
  while and gt b. a. . eq idx s. sub b. 1 . . 32 . . do set b. sub b. 1 . . end .
  return subslice s. a. b. . .
end .

let NAME slice u8 comptime trim \"  hi  \" . . .    rem read-only bytes \"hi\"

fn f output usize .
do
  return len NAME. . .
end .",
      out: "f() = 2")
    #ex("building a message table at translation — without allocation", "module ex_comptime_messages .

struct msg do
  ko slice u8 .
  en slice u8 .
end .

fn parse input txt slice u8 . output r array 4 msg .
do
  var row usize 0 .
  var start usize 0 .
  var comma usize 0 .
  var i usize 0 .
  while le i. len txt. . . do
    if or eq i. len txt. . . eq idx txt. i. . 10 . . do
      if gt i. start. . do
        set idx r. row. . lit msg do ko subslice txt. start. comma. . . en subslice txt. add comma. 1 . i. . . end . .
        set row. add row. 1 . .
      end .
      set start. add i. 1 . .
    end else do
      if eq idx txt. i. . 44 . do set comma. i. . end .
    end .
    set i. add i. 1 . .
  end .
  return r. .
end .

let MSGS array 4 msg comptime parse \"hi,hello\\nthx,thanks\" . . .    rem the fields point at pieces of the input text

fn f output usize .
do
  return len field idx MSGS. 1 . en . . .
end .",
      out: "f() = 6")
    #ex("passing a struct and receiving a slice of structs", "module ex_comptime_structs .

struct pt do
  x u16 .
  y u16 .
end .

fn scale input p pt . input k u16 . output pt .
do
  return lit pt do x mul field p. x . k. . . y mul field p. y . k. . . end . .
end .

fn from input ps slice pt . input k usize . output slice pt .
do
  return subslice ps. k. len ps. . . .    rem a piece of the input — builds no new storage
end .

let P pt comptime scale lit pt do x 3 . y 4 . end . 10 . . .
let REST slice pt comptime from lit slice pt lit pt do x 1 . y 2 . end . lit pt do x 5 . y 6 . end . . 1 . . .

fn f output u16 .
do
  return add add field P. x . field P. y . . field idx REST. 0 . x . . .
end .",
      out: "f() = 75")
    #rejected("a file read at translation is an input of a function run at translation", "module ex_include_place .

fn f output usize .
do
  return len include_text \"words.txt\" . . .    rem it would read as if read at execution
end .", "E-INCLUDE-PLACE")
    #rejected("the computed answer does not fit the receiving place", "module ex_comptime_width .

let K u64 6 .

fn f output u8 .
do
  return comptime mul K. 100 . . .    rem 600 does not fit `u8`
end .", "E-TYPE-WIDTH")
  ]
  #sub("6.9", "Places that cannot be checked — `unsafe` and `extern`")[
    #para("1")[
      `unsafe` marks a place that does what the language cannot check. It is #strong[an effect];, so
      it shall be written in the contract (#cref("7.1")), and it spreads upward to the callers.
    ]
    #para("2")[
      `extern` marks a place that meets something written in another language (#cref("7.4")).
    ]
    #para("3")[
      These two places #strong[shall be narrow];. Widening them widens by as much the area the language's guarantees do not reach.
    ]
    #plain[
      The word «unsafe» does not mean #emph["anything goes"];. It is a mark that says #strong[from here on, not the language
      but a person is responsible];, and that mark remains in the source so it can be found later.
    ]
  ]
  #sub("6.9.1", "Writing machine instructions directly — `asm`")[
    #para("1")[
      The body of an op can be written as #strong[machine instructions];. Write an `asm <machine> .` clause in the declaration, and in the body write
      that machine's assembly.
    ]
    #para("2")[
      The processor #strong[cannot read]; what is inside. The assembler reads it. That is, every invariant this language keeps
      can be broken inside, and the processor cannot see that. What cannot be seen #strong[is confined]; —
      #strong[all]; of the following four shall be present.
    ]
    #tbl("What an op that uses `asm` shall have")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*To have*], [*Why*],
      [the `unsafe` mark], [it marks honestly what cannot be seen],
      [`cap machine`], [it #strong[receives the right]; to issue machine instructions — not an authority floating around],
      [`effects unsafe`], [carried in the signature, so #strong[the caller knows];],
      [the machine name], [assembly is not portable. The processor does not pretend it is],
      )
    ]
    #para("3")[
      The machine name comes from the closed set of #cref("5.6"). Other names are refused (`E-ASM-TARGET`),
      and assembly for a machine #strong[different]; from the one being built for is refused too — the processor neither silently
      drops that op nor hands another machine's instructions to the assembler.
    ]
    #para("4")[
      Operands are `<register> <name>` for inputs and `out <register> <name>` for outputs.
      Writing only a register without what to carry is refused (`E-ASM-OPERAND`).
    ]
    #para("5")[
      The `clobber` clause writes #strong[what]; these instructions #strong[destroy];, and the `options` clause the promises given to the processor.
    ]
    #caution("")[
      `asm` is a clause word, so it cannot come in a value position. That is why the name of the capability is not `cap asm` but
      `cap machine` — a name collision is not discovered and fixed but #strong[removed by design];.
    ]
    #para("6")[
      The body is #strong[the assembly itself]; — one text literal, and nothing else can come there
      (`E-ASM-BODY`). Machine instructions and lowered code cannot be mixed.
    ]
    #para("7")[
      The processor cannot read inside the template, but #strong[the operand list and the `{name}`s of the template are two expressions
      that say the same thing];, so whether they match is checked #strong[both ways];.
    ]
    #tbl("Places where `asm` is refused")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Diagnostic*], [*What went wrong*],
      [`E-ASM-UNBOUND`], [the `asm` clause #strong[never declared]; a name the template calls. The assembler refuses it, or worse, #strong[simply reads whatever register was there];],
      [`E-ASM-UNUSED`], [the template #strong[does not use]; a declared operand. It burns a register and says something the assembly does not — an unchecked duplicate rots into a lie (⟦§1.1⟧)],
      [`E-ASM-BIND`], [an input operand points at something that is #strong[not a value]; (a name that is neither a parameter nor a local). If it is not a value, whatever was in that place is carried],
      [`E-ASM-BODY`], [the body is not one text literal],
      [`E-ASM-OPERAND`], [only a register was written, without what to carry],
      [`E-ASM-NOUNSAFE` · `E-ASM-NOCAP` · `E-ASM-NOEFFECT`], [one of the three things to have in the table above is missing],
      [`E-ASM-TARGET` · `E-ASM-TARGET-UNKNOWN`], [the machine name differs from the machine being built for, or is outside the closed set of ⟦§5.6⟧],
      [`E-ASM-OPTLIE`], [`options pure` #strong[disagrees with the effects line];],
      )
    ]
    #para("8")[
      `options pure` says #emph["this assembly has no side effects"];, and trusting that, the processor may
      #strong[remove, hoist, or run once instead of twice]; these instructions. But if the effects
      line says it touches input/output · state · devices, #strong[both cannot be true.]; The processor then
      trusts `options`, so the disagreement is refused (`E-ASM-OPTLIE`).
    ]
    #caution("")[
      When what the processor #strong[trusts]; and what the processor #strong[checks]; differ, the dangerous side is always what it trusts.
      `options` is a promise that cannot be checked, so if that promise disagrees with a declaration elsewhere,
      it is stopped on the spot.
    ]
  ]
  #sub("6.9.2", "Where C is met — the boundary of `extern`")[
    #para("1")[
      An op marked `extern` #strong[has its body in C.]; So writing a body on this side is refused
      (`E-FFI-BODY`) — two bodies are not a program but a question nobody can answer.
    ]
    #para("1a")[
      An `extern` op whose body is in C is #strong[a block declaration];. As a `struct` holds its fields in `do … end`, it holds its clauses
      in `do … end`: `extern proc <name> do <clause>* end .`. Each clause is closed by its own dot
      (#cref("6.4.1") (3c)). Only clauses come in the block — if a statement comes, there are two bodies (`E-FFI-BODY`). The shape that lists clauses after the name
      without `do` and closes with `end` is refused (`E-STMT-NODO`) — `end` closes only its own
      `do` (#cref("6.1.6") (2a)).
    ]
    #ex("an op whose body is in C — its clauses go in do … end", "module ex_extern_block .

unsafe extern proc c_abs do
  input k cap c .
  input a i64 .
  output i64 .
  effects unsafe .
  link \"llabs\" .
end .")
    #rejected("clauses listed without do and closed with end", "module ex_extern_nodo .

unsafe extern proc c_abs input k cap c . input a i64 . output i64 .
  effects unsafe .
  link \"llabs\" .
end", "E-STMT-NODO")
    #para("2")[
      `export extern` is #strong[the opposite direction];. C calls us, so the body is #strong[on this side];, and
      without a body it is refused (`E-FFI-BODY`).
    ]
    #para("2a")[
      The name seen from C is `lw` followed, for each step of the path, by `_<length><step>`. The length is decimal and
      may have several digits. The path of an op in a module is [module, name], of an op belonging to a type [module, type, name], and of an op in a file without a module
      declaration [name] — `inside` of `module listlit` is `lw_7listlit_6inside`,
      and `rect.area` of `module shapes` is `lw_6shapes_4rect_4area`. A name in a module is unique whatever its kind
      (#cref("6.10.3")), so the path fixes the name to one, and since the length is written, underscores inside names are not read as step boundaries.
      Names with a digit after `lw_` belong to this rule; names with a letter after `lw_` belong to the processor itself.
    ]
    #para("2b")[
      If `link "<C name>" .` is written in the head, that name becomes the C name #strong[as it is];. That name shall be acceptable to C
      — ASCII letters · digits · underscores, not starting with a digit, not a reserved word of C or C++, and not starting with `lw_`, `__` or
      `_` + a capital letter. Otherwise it is refused (`E-LINK-NAME`). If two exports in one translation unit
      have the same C name, it is refused (`E-ABI-NAME-DUP`).
    ]
    #rejected("a C reserved word written as the C name", "module ex_link_keyword .

export fn inside input a u64 . output u64 .
  link \"inline\" .
do
  return add a. 1 . .
end .", "E-LINK-NAME")
    #para("3")[
      An op that calls C shall have #strong[all]; of the following three. If even one is missing, it is refused.
    ]
    #tbl("What an op that calls C shall have")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*To have*], [*If missing*], [*Why*],
      [the `unsafe` mark], [`E-FFI-NOUNSAFE`], [inside that function everything this language keeps can be broken, and the processor cannot see it. What cannot be checked #strong[shall at least be marked];],
      [`input k cap c .`], [`E-FFI-NOCAP`], [entering C is #strong[a right that is handed over]; — the same as the heap (⟦§8.4⟧) · devices · machine instructions (⟦§6.9.1⟧). A door floating around is not a door],
      [an effects line (at least `unsafe`)], [`E-FFI-NOEFFECT`], [the effects line is #strong[how the caller learns what it takes on];. C hidden behind a clean signature is exactly the hidden cost this language sets out to remove],
      [`link "<symbol>" .`], [`E-FFI-LINK`], [a C name is #strong[a promise made to another language];, and the promise is the author's. If it were made up from the op name, the moment the op is renamed it would call another function, and that fact would be nowhere in the source],
      )
    ]
    #para("3a")[
      The right is judged #strong[by kind];. Receiving a right other than `cap c` (allocators · file systems …) does not open this place
      (`E-FFI-CAPKIND`). Not having one but #strong[what it is]; makes the authority.
    ]
    #para("4")[
      The types that cross the boundary are limited to #strong[what C's ABI can express];. Anything else
      (⟦option⟧ · ⟦result⟧ · vectors · containers) is refused (`E-FFI-TYPE`). C has no such things, and
      the processor #strong[does not pretend it does.];
    ]
    #para("4a")[
      #strong[Only pieces (slices)]; are mapped automatically. `slice <type>` becomes #strong[two arguments];: «a pointer to `<type>`» and
      «a count». The width is known — `slice u32` is `uint32_t*`, not a byte
      pointer. Everything else is written by hand.
    ]
    #para("4a-1")[
      Integer arguments cross #strong[at their declared width]; — 8 · 16 · 32-bit integers are the C integer types of that width and sign (`int8_t` · `uint8_t` · `int16_t` ·
      `uint16_t` · `int32_t` · `uint32_t`), and other integers are `long long`. The result of an `extern` is at its declared width too. The header the processor emits writes those
      prototypes, so if the C side includes that header, a mismatched prototype becomes a translation error.
    ]
    #para("4b")[
      #strong[Rights do not cross.]; A `cap` is not a value but an authority, so there is nothing to hand to
      C.
    ]
    #para("4c")[
      A struct with a byte layout (#cref("6.2.7") — all fields sized numbers · arrays · such structs) crosses #strong[by value];. What is passed when calling C,
      and what is received and returned when C calls us, is a C struct with the same field order and widths. Whether a large struct is returned through a hidden pointer
      is decided by the C compiler according to the target's conventions — this language does not expose that in the signature. A struct with a 128-bit integer field does not
      cross (`E-INT128-PLACE`, #cref("6.2.2") (3b-2)).
    ]
    #para("4d")[
      If an input of an op that gives a name for C to call (`export`) is an ⟦option⟧ · a vector · a bitset or a struct without a layout, or its result is one of those · a piece, the processor
      #strong[does not emit]; the C name and says so (`W-EXPORT-NOSYM`) — the header does not promise that name either. It never writes another type in the C signature
      and discards the value.
    ]
    #para("5")[
      There is #strong[one]; ABI name, `c`. `sysv` · `win64` · `aapcs64` are not made into words — what they
      ask is #emph["what is C on this machine"];, and the machine being built for (#cref("5.6")) already
      fixes that answer. Adding words would make two ways of saying the same thing.
    ]
    #para("6")[
      Variadic arguments go #strong[one way only];. Variadic C functions can be called, but C cannot call our
      variadic ones — that would be work done without knowing the number and types of arguments at run time,
      with no place for a contract to stand.
    ]
    #para("7")[
      There is no new convention for callbacks. An `export extern` op can already be called by C, so
      a callback is passing #strong[the address of that op]; (`unsafe_fn <op>`) as a value. The boundary contract is checked at the place where
      the door is entered.
    ]
    #para("7a")[
      So what `unsafe_fn` can point at is #strong[only an `export extern` op];. Pointing at any other op
      is refused (`E-FN-NOTEXPORT`) — handing over the address of a door C cannot call is not an address but
      a question.
    ]
    #para("7b")[
      An op used as a callback #strong[cannot require capabilities]; (`E-FN-CAP`). What enters a callback is
      C, and #strong[C has no capabilities to hand over.]; Work that needs capabilities is done #strong[outside]; the callback,
      and inside the callback there is only computation.
    ]
    #para("7c")[
      After `unsafe_fn` there shall come #strong[the name of an op]; (`E-FN-VALUE`), and an op of that name shall actually
      exist (`E-FN-UNDEF`).
    ]
    #caution("")[
      A capability is handed over by the caller (#cref("7.2")), and the caller of a callback is C. So if a callback
      requires a capability, that place becomes #strong[a place nobody can fill];. Rather than discovering it empty at run time,
      that shape is refused at translation.
    ]
    #para("8")[
      Passing `owned <type>` to an `extern` #strong[moves the responsibility to C.]; This side's duty ends at that point,
      and giving it back afterwards is C's job. Whether that is kept #strong[is not verified]; —
      inside the door we are responsible, outside the door C is.
    ]
    #para("9")[
      If a raw pointer (#cref("8.5")) mismatches at the boundary, it is refused (`E-UPTR-TYPE`).
    ]
    #para("10")[
      #strong[An exported type cannot take its alignment from the machine]; (`E-ABI-TARGET-ALIGN`).
      Writing something like `align machine.cache_line` makes the layout differ by machine, while the header the C side reads
      assumes there is #strong[one]; layout — the two part without a word. Give exported
      types alignment with #strong[a written number]; (`align N .`).
    ]
    #plain[
      What this clause does is not make C convenient to call, but make #strong[the places that call C narrow and
      visible];. The mark (`unsafe`) · the right (`cap c`) · the effects line each tell the same fact to people, to the processor and
      to the caller. With only one of the three it «can still be called» — but
      then this language can no longer say what it has lost.
    ]
  ]
  #sub("6.9.3", "Places that use instructions that differ by machine — `unsafe target`")[
    #para("1")[
      Some ops have meaning #strong[only when a particular instruction set is present];. Such ops write that fact with `unsafe target <instruction set>`.
    ]
    #para("2")[
      An instruction-set name the processor does not know is refused (`E-TARGET-ISET`). The names form a closed set.
    ]
    #para("3")[
      If the name is known but #strong[the machine being built for cannot provide it];, it is refused (`E-TARGET-INTRIN`).
    ]
    #para("3a")[
      What this clause blocks is #strong[the place where that op stands];, not each op individually. Which leaves must be inside this
      fence is decided only when the meaning of that leaf #strong[depends on the machine]; ---
      an op written to give the same value on every machine is not inside this fence.
    ]
    #note[
      There was a clause in this place that tied `avg` down as «machine-bound». It was deleted (2026-09-17) ---
      #cref("6.3.7") wrote it as #strong[an ordinary lane op];, and both back ends of this edition emitted it not as a machine
      instruction but as widened integer arithmetic. That is, what it was blocking was #strong[a machine dependency that did not even
      exist];. Where the text and the tool part, follow the text.
    ]
    #plain[
      This place is `unsafe` because the meaning of that op #strong[depends on the machine];. What this language
      keeps shall be the same whatever the machine, and here that promise is let go — so the fact of letting go
      shall be written in the source.
    ]
    #caution("")[
      Among the library leaves that reach the host, some today go down #strong[only to POSIX];. Trying to build such
      a leaf for a machine without it is refused (`E-TARGET-LEAF`) — what is not there is not sent down
      pretending it is.
    ]
  ]
  #sub("6.9.4", "Decided but not built — `raw` · `addr` · `adopt` · `clone`")[
    #para("1")[
      The four words of this clause have a #strong[decided meaning, but the implementation has not built them yet];. The implementation recognizes
      them, and using one is refused at translation (`E-UNSAFE-UNBUILT`) — it is not taken with another meaning or quietly dropped to the
      interpreter. `adopt` · `clone` · `addr` are not reserved words: if the author builds an op with that name, that op stands.
    ]
    #para("2")[
      `raw <T>` is #strong[a location whose storage is not known];. It is made, read, written and moved (a number added to the location) only inside
      an op with the `unsafe` effect. It can be copied. It can be obtained from a `ref` · `mut_ref` · slice, and then the exclusivity (#cref("8.12"))
      and lifetime guarantees that reference kept are given up. Writing `raw` in a type position is refused for now.
    ]
    #para("3")[
      `addr` is #strong[a location seen as a number]; — it can only be added and compared; no value can be read or written through it. Going between it
      and `raw` happens only inside `unsafe`. `addr <local>` takes the location of that local. Writing `addr` in a type position, or using
      `addr <local>`, is refused for now.
    ]
    #para("4")[
      `adopt` turns a `raw <T>` back into a safe slice. It requires #strong[all]; three: being inside `unsafe` · taking as a witness the live region the
      memory lives in (the new slice gets that region's lifetime) · stating alignment and range as a contract (`requires`). To turn it back into
      a writable slice, the assumption «no other access to that range» is written with `assume` — a mark that a person carries what the
      implementation cannot see.
    ]
    #para("5")[
      `clone <value> <region>` #strong[copies a value deeply]; into that region. By the principle that there is no hidden copy (#cref("8")), a deep copy happens
      only through this word. It has the allocation effect (`alloc`).
    ]
    #para("6")[
      #strong[Deferred.]; The four are built when a use appears. If the other paths — windows on bytes (#cref("6.2.13")) · `extern` (#cref("6.9.2")) · allocators
      (#cref("8.13")) — fill those uses and the four #strong[prove unnecessary, they are dropped.]; The owner decides whether to build or drop them. Until then
      this clause stays and the implementation keeps recognizing the words.
    ]
    #rejected("a location type that is not built yet", "module ex_unbuilt .

fn f input p raw . output u64 . do
  return 0 .
end .", "E-UNSAFE-UNBUILT")
    #note[
      The source is RFC-0005 §6.1 · §6.8 (the kinds of reference and locations → turning back into a safe reference) and RFC-0049 (`clone`). Both RFCs were
      adopted and then archived, and these four were not carried into the canon (sweep of 2026-11-11). The owner decided to «put them in formally, state that
      they are dropped if they later prove unnecessary, and keep tracking them» (2026-11-12).
    ]
  ]
]

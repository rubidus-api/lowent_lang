#import "../lib.typ": *

#sub("6.5", "Statements and control flow")[
  #para("1")[
    A #t("문장", "statement") is something that is executed. An expression makes a value; a statement does work.
  ]
  #para("1a")[
    So using as a statement a form that #strong[only gives a value and changes nothing]; — a pure computation (`add s 100 .` · `eq s 7 .`) or an `fn` call —
      does not conform (`E-VALUE-DISCARDED`). That value goes nowhere. What was usually meant is
      `set s. add s. 100 . .`. A `proc` call is a statement, since its effect is its work.
  ]
  #rejected("a form that only gives a value cannot be used as a statement", "module ex_discard .

fn bump input a u64 . output u64 .
  requires lt a. 100 . .
do
  var s u64 a. .
  add s. 100 .   rem what was meant is set s (add s 100) .
  return s. .
end .", "E-VALUE-DISCARDED")
  #sub("6.5.1", "Statements that make names")[
    #para("1")[
      `let` makes a name that #strong[does not change];. A value once fixed does not change.
    ]
    #para("2")[
      `var` makes a name that #strong[can change];. Its value is changed with `set`.
    ]
    #para("2a")[
      #strong[Parameters can be `set` too.]; A parameter received by value is a #strong[local copy]; within this op, so
      changing it does not change the caller's value. Writing a field of a struct parameter (array fields included) is the same —
      it writes to the local copy. The only writes that reach the caller are `mut` · `mut_ref` positions (#cref("8.4")).
    ]
    #para("2b")[
      So what `requires` · `ensures` say through a parameter name is #strong[the value on entry];.
      Even after the body `set`s that name, what the contract says is the value on entry, unchanged.
    ]
    #syntax("binding", "binding      ::= ( \"let\" | \"var\" ) name [ alloc-clause ] [ type ] term [ fail-clause ] \".\"
alloc-clause ::= ( \"use\" | \"keep\" ) name \".\"
fail-clause  ::= \"else\" leave \".\"
               | \"else\" block
               | \"else\" \"error\" name block
leave        ::= \"return\" [ term ] | \"break\" [ number ] | \"continue\" [ number ] | \"panic\" term")
    #para("3")[
      The statement that makes a name is `let <name> <type> <value> .` (`var` the same) — #strong[the type stands after the name and before the value.];
      If the value does not fit that type, translation is refused. The type is not guessed: it is `let x u64 300 .`, not
      `let x 300 .` (`E-LET-NOTYPE`). Guessed types let bare literals slip past the width check.
      If the value begins with `lit`, the type is written after that `lit`, so it may be left out in front (`let p lit pt do … end . .`).
    ]
    #para("3a")[
      No word is inserted between the name and the type. The `be` of the old shape `let <name> be <type> <value> .` is not a word of this
      language — writing it is refused at translation (`E-LET-BE`).
    ]
    #para("3b")[
      A type ends by its characters alone: it is a constructor of the closed vocabulary (`u64` · `slice u8` · `result u64 perr` · `array 4 u8`),
      one name, or parentheses. User types that take arguments, and forms whose number of arguments is not fixed (such as `result u64` without
      an error type), are wrapped in parentheses — `let b (box u64) mk. .` · `let r (result u64) ok k. . .`.
    ]
    #para("3c")[
      #strong[The allocator clause]; stands directly after the name, before the type — `use <allocator>.` gives back at the end of the block, and `keep <allocator>.`
      does not give back even on leaving the block (#cref("6.2.6") (7e)(7f) · #cref("8.14")). An allocator is a variable, so it is closed with a dot:
      `var v use al. option (vec u32 allocs.bump_bytes) vecgen.new al. 8 . .`.
      The old shapes `using <allocator> be` · `using <allocator> keep be` are refused (`E-USING-OLD`). A binding whose type contains `owned`
      (`let n use al. option (owned node) lit node … .`) does not give back at the end of the block even when written with `use` — #cref("8.5") (10)(11).
    ]
    #para("4")[
      After the type #strong[there shall be a value.]; Closing without a value is refused at translation (`E-LET-NOVALUE`).
      This language has no way of making a name and giving it a value later.
    ]
    #para("4a")[
      The value is one form and #strong[is closed by its own dot]; (#cref("6.1.5")). After it comes the statement's closing dot, or `else` —
      `let n u64 find k. . .` · `let n u64 find k. . else return 0 . .`. The failure clause stands #strong[before]; the statement's closing dot:
      writing `else` after the statement has been closed is refused at translation (`E-STMT-ELSE`).
    ]
    #para("5")[
      #strong[The failure clause]; — in `let <name> <type> <value> else <leave> . .`, if the type of the value is `option t` · `result t e` and the type written
      is the contents `t`, then when there is a value its contents are bound to the name, and when it is empty (`none` · an error) what follows `else` is executed.
      What follows `else` #strong[shall leave]; (`return` · `break` · `continue` · `panic`) — falling through is refused
      (`E-GUARD-FALLTHROUGH`). So after it the name always has contents. `var` is the same.
    ]
    #para("5c")[
      The failure clause has three shapes. `else <leave> .` is one leaving statement — that statement is closed by its own dot.
      `else do … end` is several statements — every path of the block shall leave. `else error <name> do … end`
      #strong[binds the error value]; of a `result` #strong[to `<name>`]; and looks at it inside the block — `<name>` lives only inside that block, and every
      path of the block shall leave. If the value is an `option`, there is no error to receive, so `else error <name>` is refused (`E-BIND-ELSE`).
    ]
    #para("5d")[
      After the failure clause comes #strong[the statement's closing dot]; — after a leaving statement too (`… else return 0 . .`), and after a block too
      (`… else do … end .`). If that dot is missing, translation is refused (`E-DOT-MISSING`).
    ]
    #para("5a")[
      If the value is not an `option` · `result`, or the type of the contents differs from the type written, it is refused (`E-BIND-ELSE`). The type of the value is read
      from the output of the op called · the declaration of the name · the output of an actor handler — for any other expression, first bind it with its type written.
    ]
    #para("5b")[
      Binding an `option` · `result` value to its contents type without `else` is refused (`E-TYPE-LET`) — a place that forgot it can fail.
      If stopping is acceptable, write `else panic "…" .` (the effect `panic` follows).
    ]
    #caution("")[
      What this rule catches is not only the mistake of leaving a blank. If `let x f64 .5 .` is written,
      `.5` is not a floating-point literal (#cref("6.1.4") (2)), so that dot closes the form, and after the name
      #strong[only the type remains.]; A person believes they wrote a value while the processor saw none,
      so the diagnostic names that trap.
    ]
    #note[
      It matters that the default is `let`. A name that does not change needs to be #strong[checked only once]; by the reader,
      but a name that can change makes them ask again at every place it is used #emph["what value is it here"];. That question
      is semantic entropy (#cref("1.3")).
    ]
    #rejected("no value after the type", "module ex_let_novalue .

fn f output u8 .
do
  let a u8 .        rem 0 is not put in silently
  return a. .
end .", "E-LET-NOVALUE")
    #rejected("the type is not guessed", "module ex_let_notype .

fn f output u8 .
do
  let x 300 .
  return x. .
end .", "E-LET-NOTYPE")
    #rejected("`be` is not a word of this language — tests (3a)", "module ex_let_be .

fn f output u64 .
do
  let x be u64 7 . .
  return x. .
end .", "E-LET-BE")
    #rejected("the failure clause comes before the statement's closing dot — tests (4a)", "module ex_else_dot .

fn find input k u64 . output option u64 .
do
  if eq k. 0 . do return none . end .
  return some k. . .
end .

fn f input k u64 . output u64 .
do
  let n u64 find k. . . else return 0 .
  return n. .
end .", "E-STMT-ELSE")
    #rejected("the allocator clause is `use` · `keep` — tests (3c)", "module ex_using_old .

actor exact do
  state do n u64 . end .
  proc reserve input k u64 . output option mut slice u8 . effects state . do
    return none .
  end .
end .

proc f output u64 . effects state .
do
  var e exact spawn actor exact . .
  let b using e. mut slice u8 lit array 4 u8 _ . else return 0 . .
  return len b. . .
end .", "E-USING-OLD")
    #ex("the three failure clauses — tests (5)(5c)", "module ex_fail_clause .

enum perr do bad . empty . end .

fn parse input s u64 . output result u64 perr .
  errors empty eq s. 0 . .
  errors bad gt s. 100 . .
do
  if eq s. 0 . do return error empty . . end .
  if gt s. 100 . do return error bad . . end .
  return ok s. . .
end .

export fn one input s u64 . output u64 .
do
  let n u64 parse s. . else return 0 . .
  return add n. 1 . .
end .

export fn many input s u64 . output u64 .
do
  let n u64 parse s. . else do
    return 99 .
  end .
  return add n. 1 . .
end .

export fn why input s u64 . output u64 .
do
  let n u64 parse s. . else error e do
    if eq e. bad. . do return 1000 . end .
    return 2000 .
  end .
  return add n. 1 . .
end .",
      out: "one(5) = 6 · one(0) = 0 · many(200) = 99 · why(0) = 2000 · why(500) = 1000")
  ]
  #sub("6.5.2", "Conditions — `if`")[
    #part("Syntax")
    #syntax("if-statement", "if-statement ::= \"if\" term block { \"else\" \"if\" term block } [ \"else\" block ] \".\"
block        ::= \"do\" { statement } \"end\"")
    #part("Constraints")
    #para("1")[
      The type of the condition (`term`) shall be `bool` (#cref("6.2.3")).
    ]
    #para("2")[
      An `if-statement` is #strong[a statement];. It cannot come in a place where a value goes.
    ]
    #part("Dynamic semantics")
    #para("3")[
      The condition is computed in turn.
    ]
    #para("4")[
      If its value is true, the statements of the first `block` are executed in the order written.
    ]
    #para("5")[
      If its value is false and there is an `else if`, (3) is done again with that condition; if there is an `else`, the `block` after it is executed.
    ]
    #para("6")[
      If its value is false and there is no `else`, nothing is executed.
    ]
    #part("Diagnostics")
    #tbl("Diagnostics of the if statement")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Violation*], [*Diagnostic*],
      [(1) — the condition is not `bool`], [`E-TYPE-COND`],
      [(2) — `if` written in a value position], [`E-IF-VALUE`],
      [syntax — one more dot written after the condition closed (`if c. . do`)], [`E-CLOSER-EXTRA`],
      [syntax — no closing dot at the end of the chain], [`E-DOT-MISSING`],
      [syntax — something other than `do … end` written in the `block` position], [`E-CTRL-NODO`],
      [syntax — `else` written inside the `block`], [`E-STMT-ELSE`],
      )
    ]
    #part("Examples")
    #ex("three branches — tests (4)(5)", "module ex_if .

export fn sign input a i64 . output u64 .
do
  if gt a. 0 . do
    return 1 .
  end else if eq a. 0 . do
    return 0 .
  end else do
    return 2 .
  end .
end .",
      out: "sign(5) = 1 · sign(0) = 0 · sign(-3) = 2")
    #rejected("`if` gives no value — tests (2)", "module ex_if_value .

fn pick input a u64 . output u64 .
do
  let x u64 (if gt a 1 do 5 . end else do 6 . end) .   rem set or return in each branch
  return x. .
end .", "E-IF-VALUE")
    #part("Notes")
    #plain[
      Why must the body be a block? The condition ends at its own dot (`if gt a. 3 . do`) — the condition never swallows the next word.
      Still, if the body could be written as one statement, the same `if` would have two shapes, and the moment a second statement is added, whether it is inside
      or outside the `if` would depend on indentation. Since the body is always `do … end`, `end` says where the body ends, and the dot after it where the statement ends.
    ]
    #note[
      That the head's expression ends at `do` and the body is written as a block holds for `while` (#cref("6.5.3")) · the four loop heads (#cref("6.5.3.1")) ·
      `match` (#cref("6.6")) too. If the head's expression is a value that holds a block (`if eq p. lit pt do x 1 . end . . do`), the head's body is
      #strong[the last block]; — for the reader's sake, wrap such a value in parentheses.
    ]
    #rejected("one more dot after the head's expression", "module ex_ctrl_dot .

fn f input a u64 . output u64 .
do
  if gt a. 3 . . do
    return 1 .
  end .
  return 0 .
end .", "E-CLOSER-EXTRA")
    #note[
      To get a different value per branch, `set` a name or `return` in each branch.
    ]
  ]
  #sub("6.5.3", "Loops — `while`")[
    #para("1")[
      `while <condition> do <statements> end .` repeats the block while the condition is true. The condition is computed #strong[on each iteration, before entering
      the block]; — if it is false from the start, the block does not run even once.
    ]
    #para("1a")[
      The condition is one form and is closed by its own dot (`while lt i. n. . do … end .`). Writing one more dot after it is refused at translation
      (`E-CLOSER-EXTRA`). The statement ends at the dot after `end` (#cref("6.1.6") (2b)).
    ]
    #para("2")[
      `break` leaves the loop, and `continue` moves to the next iteration. Without a number, both apply to the #strong[innermost];
      loop. `break` carries no value.
    ]
    #para("2b")[
      With a number after the word, they apply to the loop #strong[counted that many outward];. `break 2 .` leaves both the innermost loop and the loop
      that encloses it. `continue 2 .` leaves the innermost loop and goes to the next iteration of the loop that encloses it.
      `1` is the same as writing no number. The number is a plain decimal literal, 1 or more, and cannot exceed the number of loops enclosing that statement
      (`E-LOOP-COUNT`). Loops are not given names (labels).
    ]
    #para("2c")[
      What is counted are the five loop heads (`while` · `for` · `repeat` · `range` · `cycle`). The blocks of `if` and `match` are not
      counted. What a loop passed on the way out opened is closed as when that loop is left normally.
    ]
    #note[
      Wrapping or unwrapping a loop makes the same number point at a different loop. Then count again.
    ]
    #para("2a")[
      These two are used #strong[only inside loops];. Writing them outside a loop does not conform
      (`E-LOOP-OUTSIDE`) — there is no loop to leave.
    ]
    #para("3")[
      The condition of a loop shall be `bool` too. Writing a number in a condition position is refused at translation (`E-TYPE-COND`).
    ]
    #para("3a")[
      `while` is a statement and gives no value. It has no `else` branch (`E-STMT-ELSE`).
    ]
    #ex("leaving nested loops at once", "module ex_break_count .

export fn first_zero input want u32 . output usize .
do
  let g lit slice array 3 u32 lit array 3 u32 4 7 1 . lit array 3 u32 9 0 5 . . .
  var hit usize 99 .
  repeat r usize len g. . do
    repeat c usize 3 do
      if eq idx idx g. r. . c. . want. . do
        set hit. add mul r. 3 . c. . .
        break 2 .
      end .
    end .
  end .
  return hit. .
end .")
    #rejected("two loops enclose it, but it tries to leave three", "module ex_break_far .

export fn f output u64 . do
  var n u64 0 .
  repeat a u64 3 do
    repeat b u64 3 do
      set n. add n. 1 . .
      break 3 .
    end .
  end .
  return n. .
end .", "E-LOOP-COUNT")
    #ex("conditions and loops", "module ex_ctl .

export fn count_big input n u32 . output u32 .
  requires le n. 100 . .
do
  var total u32 0 .
  var i u32 0 .
  while lt i. n. . do
    if gt i. 5 . do
      set total. add total. 1 . .
    end .
    set i. add i. 1 . .
  end .
  return total. .
end .")
  ]
  #sub("6.5.3.1", "Loops that go in order — `for` · `repeat` · `range` · `cycle`")[
    #syntax("the four loop heads", "for-stmt    ::= \"for\"    name [ \"mut\" ] term [ filter ] block \".\"
repeat-stmt ::= \"repeat\" name type term [ filter ] block \".\"
range-stmt  ::= \"range\"  name type term term [ \"step\" term ] [ filter ] block \".\"
cycle-stmt  ::= \"cycle\"  name type term \"while\" term \"next\" term [ filter ] block \".\"
filter      ::= \"if\" term")
    #para("1")[
      The four statements repeat the block #strong[putting the values the head decides into the name in turn];. Each head word does one thing —
      `for` goes over the elements of a source, `repeat` n times from 0, `range` between two ends, and `cycle` over the next values the author writes.
      There are no other shapes.
    ]
    #tbl("Loop heads")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Head*], [*Shape*], [*What the name takes in turn*],
      [elements], [`for x <slice> do`], [the elements of the slice — from the front],
      [element places], [`for x mut <slice> do`], [the slots of the slice themselves — can be read and written],
      [counting], [`repeat i <type> n do`], [`0, 1, …, n−1`],
      [range], [`range i <type> a b do` · `range i <type> a b step k do`], [from `a` to `b`, both ends included],
      [recurrence], [`cycle i <type> v while c next e do`], [starting at `v`, then `e` on each iteration],
      )
    ]
    #para("1a")[
      Each expression of the head is one form and is closed by its own dot — `repeat i u64 len xs. . do` · `range i u64 0 sub n. 1 . do` ·
      `cycle i u64 1 while lt i. n. . next mul i. 2 . do`. The ending words (`do` · `if` · `while` · `next` · `step`) are
      reserved, so they cannot come as names inside an expression. Writing one more dot after a closed expression is refused at translation (`E-CLOSER-EXTRA`).
    ]
    #para("1b")[
      The old shapes `for i count <type> n . do` · `for i range <type> a b . do` · `for i be <type> v . while c . next e . do` and
      the filter `. where c .` are refused (`E-FOR-OLD`). `for` does only one job, going over elements.
    ]
    #para("2")[
      #strong[Elements.]; The source shall be a slice. If it is not a slice, translation is refused (`E-TYPE-ITER`). The type of the name is
      the #strong[element type]; of the slice. String literals (slices of bytes) and list literals (`lit slice u16 10 20 30 .`) are
      sources too. If there are no elements, the block does not run even once.
    ]
    #para("2a")[
      The source ends at `do` (or at the filter's `if`) — whether it is one name (`for x xs. do`) or an expression (`for x view_array u16 b. . do`),
      it is the same.
    ]
    #para("3")[
      #strong[Element places.]; In `for x mut buf. do … end .`, `x` is that slot of `buf`: reading it gives the slot's value, and `set x. v. .` writes
      that slot. `buf` shall be `mut` (`E-TYPE-MUT`). During the loop the whole of `buf` is borrowed, so reading or writing
      `buf` inside the block is refused at translation (`E-FOR-HEAD`) — the elements are touched only through `x`.
    ]
    #para("3a")[
      In `for x <slice> do` written without `mut`, `x` is a #strong[copy]; holding that iteration's element value. `x` cannot be `set` inside the block
      (`E-FOR-HEAD`) — to change the slots, write `mut` in the head.
    ]
    #para("4")[
      #strong[Counting.]; `repeat i <type> n do` puts `0` to `n−1` into `i` in turn. If `n` is 0 or less, it does not run
      even once. `<type>` is the type of the count and #strong[is an integer type that is always written]; — if it is not written, or is not an integer type,
      translation is refused (`E-FOR-STEP`). `n` shall fit `<type>` (`E-TYPE-WIDTH`).
    ]
    #para("5")[
      #strong[Range.]; `range i <type> a b do` goes from `a` to `b` #strong[with both ends included];. The two ends decide the direction —
      `range i u64 3 5` is `3, 4, 5`, and `range i u64 5 3` is `5, 4, 3`. If the two ends are the same, it runs once.
      Both ends shall fit `<type>` (`E-TYPE-WIDTH`).
    ]
    #para("5a")[
      With `step k`, the next value is `current + k` and #strong[the sign of `k` decides the direction.]; If the start has already passed the end in that direction,
      it does not run even once (`range i i64 1 10 step -1`). If the next value passes the end, it ends there —
      `range i u8 250 255 step 3` is `250, 253`. `step 0` is refused at translation (`E-FOR-STEP`), and if a `k` not known at translation
      is 0 during execution, it stops.
    ]
    #para("5b")[
      The count #strong[does not overflow.]; `range i u8 0 255` runs 256 times and ends at 255 — it never goes beyond the type while computing
      what comes after the last value.
    ]
    #para("6")[
      #strong[Recurrence.]; `cycle i <type> v while c next e do … end .` starts `i` at `v`. Before entering an iteration
      it looks at `c`; if true, it runs the block, and when the block ends it puts the value of `e` into `i`. If `c` is false from the start, it does not run
      even once. `c` shall be `bool` (`E-TYPE-COND`), and `v` and `e` shall fit `<type>` (`E-TYPE-WIDTH`).
      The computation of `e` is ordinary arithmetic — it stops if it overflows (#cref("6.3.4")). Both clauses `while` · `next` shall be present, in this order
      (`E-FOR-HEAD`). A `continue` inside the block #strong[goes through]; `next` — it puts the value of `e` into `i` and looks at `c` again.
    ]
    #para("7")[
      #strong[Filtering.]; `if c` can be added at the end of the head, before `do`. It means the same as writing `if not c. . do continue . end .` at the start
      of the block — the condition is computed on each iteration. It attaches to any of the four head words. The `if` inside the head is not a statement but
      a clause — no statement can come inside the head, so they do not overlap.
    ]
    #para("8")[
      #strong[What the head read is frozen during the loop.]; The end · `step` · source are computed #strong[once]; on entering the loop.
      The count name (the name of `repeat` · `range` · `cycle`) and the names the head read cannot be `set` inside the block
      (`E-FOR-HEAD`). Changing the contents of slice slots is allowed.
    ]
    #para("9")[
      The name of a loop lives #strong[only inside the block];. When the block ends, that name is gone. A name that already exists in the same block cannot
      be used again (`E-NAME-SHADOW`).
    ]
    #para("10")[
      `break` and `continue` are used inside the four loops just as in `while` (#cref("6.5.3") (2)). None of the four gives a value.
    ]
    #ex("a head that counts numbers", "module ex_for_count .

export fn evens output u64 .
do
  var acc u64 0 .
  range i u64 10 1 step -2 do
    set acc. add acc. i. . .
  end .
  return acc. .
end .",
      out: "the sum of the even numbers from 1 to 10")
    #ex("going through a slice", "module ex_for .

export fn total_of input xs slice u8 . output u64 .
do
  var acc u64 0 .
  for x xs. do
    set acc. add acc. widen u64 x. . . .
  end .
  return acc. .
end .",
      out: "adds up all the elements")
    #ex("element places · recurrence · filtering", "module ex_for_more .

fn bump input b mut slice u64 . output u64 .
do
  for x mut b. do
    set x. mul x. 2 . .
  end .
  var acc u64 0 .
  for y b. do
    set acc. add acc. y. . .
  end .
  return acc. .
end .

export fn powers output u64 .
do
  var n u64 0 .
  cycle i u64 1 while lt i. 100 . next mul i. 2 . do
    set n. add n. 1 . .
  end .
  return n. .
end .

export fn evens output u64 .
do
  var acc u64 0 .
  repeat i u64 10 if eq mod i. 2 . 0 . do
    set acc. add acc. i. . .
  end .
  return acc. .
end .",
      out: "powers() = 7 · evens() = 20")
    #rejected("the count name cannot be changed inside the block", "module ex_for_head .

fn f output u64 .
do
  repeat i u64 3 do
    set i. 0 .
  end .
  return 0 .
end .", "E-FOR-HEAD")
    #rejected("`for` goes only over elements — tests (1b)", "module ex_for_old .

fn f output u64 .
do
  var acc u64 0 .
  for i count u64 3 do
    set acc (add acc i) .
  end
  return acc. .
end .", "E-FOR-OLD")
    #caution("")[
      `in` is #strong[not]; a word of this language. Writing `for x in xs` is refused
      (`E-VOCAB-REMOVED`). What is gone through comes directly after the name.
    ]
  ]
  #sub("6.5.4", "A condition that leaves — `guard`")[
    #para("1")[
      `guard` #strong[leaves on the spot]; if the condition is not true. What comes after `else`
      #strong[shall leave on every path];.
    ]
    #para("0")[
      `guard <condition> else <leave> . .` — the condition is one form and is closed by its own dot. The leaving statement after `else` is closed
      by its own dot too, and after it comes the dot that closes `guard` (`guard lt i. 9 . else return 0 . .`). The same holds when what follows `else` is a block
      (`guard c. else do … end .`).
    ]
    #para("1a")[
      After `else` there may come one statement or a block. If it is a block, #strong[every
      path]; of that block shall leave — the leaving statements are `return`, `break`, `continue` and `panic`.
    ]
    #para("2")[
      If some path of `else` does not leave but continues downward, translation is refused.
    ]
    #para("3")[
      Once past a `guard`, its condition #strong[is guaranteed to be true.]; The processor uses that fact in the code
      after it.
    ]
    #rejected("`else` does not leave", "module ex_guard_bad .

proc p input n u32 . output u32 . effects none .
do
  guard le n. 5 . else set n. 0 . .   rem continues downward without leaving
  return n. .
end .", "E-GUARD-FALLTHROUGH")
    #caution("`guard` is not another name for `if not`")[
      What `guard` says is #emph["if not this condition, it ends here"];. So the code below `guard`
      lives only in a world where the condition is true. If `else` does not leave, that promise breaks, and #strong[the code below
      wrongly believes it is true]; — so the language refuses it.
    ]
    #ex("guard", "module ex_guard .

export fn safe_head input data slice u8 . output u8 .
do
  guard ge len data. . 1 . else return 0 . .
  return idx data. 0 . .
end .")
    #ex("the type is written before the value, and `guard else` may be a block", "module ex_infer_guard .

rem The type is written after the name and before the value — it is not guessed.
fn inferred output u64 .
do
  let a u64 7 .
  return a. .
end .

rem `else` may be a block. The rule is \"does every path leave\".
fn guarded input n u8 . output u8 .
do
  guard gt n. 5 . else do
    let x u8 1 .
    return x. .
  end .
  return 9 .
end .",
      out: "inferred() = 7 · guarded(3) = 1 · guarded(9) = 9")
    #rejected("a path of `else` that does not leave is refused", "module ex_guard_fall .

fn f input n u8 . output u8 .
do
  guard gt n. 5 . else do
    let x u8 1 .
  end .
  return 9 .
end .", "E-GUARD-FALLTHROUGH")
  ]
  #sub("6.5.5", "Going back — `return`")[
    #para("1")[
      `return` ends the op and returns a value. The type of the value returned shall be the same as what is written in
      `output`.
    ]
    #para("1a")[
      This rule is the same #strong[wherever]; `return` is — the one statement after the `else` of a `guard`, and all statements inside an `else` block,
      receive the same check. So an op with `output result …` returns only with `ok <value>` ·
      `error <variant>` · an expression that gives a `result`, and returning a bare value is refused
      (`E-TYPE-RETURN`). Conversely, an op with a bare type written returning `ok <value>` is refused too.
    ]
    #rejected("a bare value returned in a `result` place", "module ex_bare_under_result .

enum short do
  too_short .
end .

fn head input b slice u8 . output result u8 short .
errors too_short .
do
  guard ge len b. . 2 . else return 0 . .   rem neither `ok 0` nor `error too_short`
  return ok idx b. 0 . . .
end .", "E-TYPE-RETURN")
    #plain[
      This place was #strong[outside the check]; for a while (until 2026-09-25) — the `else` of a `guard` was attached as one lump, not as a list of statements,
      so the checker did not go down into it. So the op above passed translation and
      stopped #strong[during execution]; the moment the caller asked `is_ok`. The rarer the path (input too short), the later it shows.
    ]
    #para("1b")[
      An op whose output is named (#cref("6.4.1") (3e)) returns that name — `return <name> .`. That name is a local that starts at 0, so
      there is a value to return even if the body never writes it. Another value may be returned too (the name is only a local). That every path shall end with `return`
      is the same as for unnamed outputs (paragraph 3).
    ]
    #para("2")[
      An op that returns no value (`output void`) can end early by writing just `return`.
    ]
    #para("2a")[
      An op that returns no value may #strong[run to the end of its body]; without `return`. There is no value to return,
      so where it ends is where it goes back.
    ]
    #para("3")[
      #strong[An op that returns a value]; shall end returning a value on every path. If there is a path that ends without a value,
      it does not conform.
    ]
    #para("4")[
      The following statements are regarded as ending by returning a value — `return`, an `if` ⋯ `else` both of whose branches return a value,
      and a `match` #strong[all of whose branches return a value];. Whether a `match` covers every case is
      checked separately (#cref("6.6")).
    ]
    #para("5")[
      `guard` · `while` · `for` have paths that leave, so by themselves they are not regarded as returning a value.
    ]
    #para("6")[
      An op whose body is machine instructions (#cref("6.9")) gives its value through `out reg`, so it is not the subject of this
      clause.
    ]
    #plain[
      Without this rule, on a path that ends without a value the processor would #strong[put 0 in instead.]; That 0 is
      nowhere in the source, so the result cannot be known even after reading the whole program (#cref("1.3")).
      So this language does not translate such paths at all.
    ]
    #ex("an op that gives no value may end without `return`", "module ex_void .

proc hold input n u8 . output void . effects none .
do
  let x u8 n. .
end .")
    #plain[
      There is no value to return, so #strong[where it ends is where it goes back];. Had it been an op that returns a value,
      the same shape would be refused (#cref("6.5.5") paragraph 3).
    ]
  ]
  #sub("6.5.6", "Blocks and indentation")[
    #para("1")[
      A block is opened with `do` and closed with `end`. Indentation #strong[has no meaning]; — it is for the reader.
    ]
    #para("2")[
      A name made inside a block is visible only inside that block. Reading that name after the block has closed does not conform
      (`E-NAME-SCOPE`) — on a path that did not enter, no value was ever put in that name,
      and the processor does not put 0 in instead (#cref("6.5.5") (3)).
    ]
    #rejected("reading a name made inside a block from outside the block", "module ex_name_scope .

fn pick input a u64 . output u64 .
do
  if gt a. 1 . do
    let big u64 mul a. 2 . .
  end .
  return big. .        rem on the path that did not enter there is no `big`
end .", "E-NAME-SCOPE")
    #plain[
      Some languages divide blocks by indentation. This language does not, because if invisible
      characters (spaces and tabs) change the meaning of a program, #strong[what is seen and what is meant can diverge];.
      `do` and `end` are visible.
    ]
  ]
  #sub("6.5.7", "Three ways of saying failure")[
    #para("1")[
      There are only three ways of saying failure, and their uses differ.
    ]
    #tbl("The three failure channels")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*What*], [*What it says*], [*What the caller does*],
      [`result t e`], [a failure that can be fixed], [asks which side and handles it. If it does not handle it, it passes it upward],
      [`option t`], [no value], [asks whether there is one and takes it out, or gives a value to use instead],
      [the `panic` effect], [a contract was broken], [cannot handle it. The program stops],
      )
    ]
    #para("2")[
      The variants that go into the error type of a `result` shall be #strong[the same]; as those the op's `errors` clause writes.
      Returning an error not written, or writing one and not returning it, does not conform.
    ]
    #para("3")[
      `panic` does not unwind. The program ends at the place where it stops — there is no way of catching it midway
      and carrying on.
    ]
    #ex("the three channels answer in different shapes", "module ex_channels .

enum io_error do
  too_big .
end .

rem ① a failure that can be fixed — result.
fn halve input a u8 . output result u8 io_error .
  errors too_big gt a. 200 . .
do
  guard le a. 200 . else return error too_big . . .
  return ok div a. 2 . . .
end .

rem ② no value — option.
fn lookup input k u8 . output option u8 .
do
  guard lt k. 3 . else return none . .
  return some mul k. 10 . . .
end .

rem ③ a broken contract — if the caller breaks the promise, it stops.
fn strict input a u8 . output u8 .
  requires le a. 200 . .
do
  return add a. 1 . .
end .",
      out: "halve(100) = ok 50 · halve(250) = err too_big · lookup(2) = some 20 · lookup(7) = none · strict(10) = 11 · strict(250) → trap")
    #plain[
      The question that separates the three is #emph["what can the caller do"];. A missing file — another file can be
      tried, so it is `result`. What is searched for not being in a list is simply absent, so it is `option`.
      A broken contract means the caller #strong[has already broken the promise];, and that is not something that can be
      fixed — so it stops.
    ]
  ]
  #sub("6.5.8", "Handling failure — `try` and the consuming forms")[
    #para("1")[
      When calling an op that returns a `result`, `try` takes out the value on success and on failure #strong[passes that error
      on upward as it is];.
    ]
    #para("2")[
      An op that uses `try` shall itself be able to return that error. Otherwise it is refused
      (`E-TRY-NORESULT`) — because the failure has nowhere to go. If a tail is attached to change the channel ((4)), or it is handled
      #strong[here]; by the means of (3), that op need not return a `result`.
    ]
    #para("3")[
      The ways of consuming `result` and `option` are closed to the following — `try` (including with a tail, (4)) ·
      asking (`is_ok` `is_error` `is_some` `is_none`) · taking out
      (`ok_value` `error_value` `some_value`) · giving a value to use instead (`value_or`).
    ]
    #para("4")[
      A tail can be attached after `try` to #strong[change the channel];. The value stays the same; only the container changes.
    ]
    #tbl("Changing channels")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Shape*], [*From what to what*], [*What is lost or gained*],
      [`try <expr> else_none`], [`result` → `option`], [the error #strong[is discarded]; — it no longer says why it failed],
      [`try <expr> else_error <variant>`], [`option` → `result`], [#strong[a name is given]; to the absence — it says why there is none],
      )
    ]
    #para("5")[
      Changing channels #strong[changes the type.]; The type of the place where `try <expr> else_none` is written is `option`,
      not the value inside it. Writing a value type and then using a channel change does not conform.
    ]
    #note[
      This is normative because once it was not so. The processor did not see the tail word, so
      an op written `output u64` #strong[returned `some 7 .`];, and still `--check` was green.
      If the norm does not speak about the place where the channel changes, nobody notices when a tool forgets that place.
    ]
    #para("6")[
      Taking out is #strong[a partial operation]; (#cref("6.2.8")). Taking out the absent side traps.
    ]
    #para("7")[
      The default of `value_or` #strong[is evaluated only when there is no value]; (lazy evaluation). So in the default position
      an expensive computation, or one that can fail, may be written — if there is a value, it does not run.
    ]
    #note[
      `value_or some 7 . div 1 0 . .` is #strong[7];. There is a value, so the default side does not run.
      That place is evaluated only when there is no value, and then the trap really happens.
      ☞ Until 2026-08-29 it was not so — the tool evaluated the default #strong[first];, so a program that had a value
      ended in a failure it need not have had. The canon (#cref("6.2.8") · DECISION-0003 C1) had fixed it as lazy evaluation,
      and the tool was fixed to match the canon. The values do not change; what changes is
      #strong[which traps and effects can happen];.
    ]
    #plain[
      Writing the code that checks for failure by hand every time makes it long, and long code gets things left out. `try` reduces that
      repetition to one word while #strong[leaving in the source the fact that the failure goes upward]; — no failure
      is silently ignored.
    ]
  ]
  #sub("6.5.9", "Stopping — `panic`")[
    #para("1")[
      `panic` stops the program at once (#cref("5.4")). It is an effect, so an op that uses it shall write
      that effect in its contract.
    ]
    #para("2")[
      A pure op (`fn`) cannot use `panic`. But the trap the processor raises when a contract is broken is separate from this,
      and it is not something the op did.
    ]
  ]
  #sub("6.5.10", "Loops that run divided — `parallel`")[
    #para("1")[
      Attaching the clause `parallel <piece> split .` to a loop declares that the loop may be run by several
      together, #strong[dividing the piece];.
    ]
    #para("2")[
      The processor does not believe that word but #strong[checks it.]; What it checks is that the steps of the loop do not rely on
      one another, and there are three conditions.
    ]
    #tbl("The conditions for a loop that can run divided")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Condition*], [*If violated*], [*What went wrong*],
      [reads only its own share], [`E-PAR-READ`], [if it reads someone else's place, whether that value is still old or already new depends on #strong[who runs first];],
      [writes only its own share], [`E-PAR-WRITE`], [if two steps write the same place, the value that remains depends on the order],
      [does not write to a place that lives across steps], [`E-PAR-CARRY`], [such a place #strong[binds]; the steps together. If they must gather, declare it with `reduce <accumulating place> <operation> .`],
      )
    ]
    #para("3")[
      If `parallel` names a piece but there is no loop that runs over the length of that piece, it is refused
      (`E-PAR-NOLOOP`) — because it cannot be known what is to be divided.
    ]
    #para("4")[
      The gathering operation shall #strong[give the same answer even if the order of grouping changes];. Operations that do not are refused
      (`E-PAR-ASSOC`). Addition of floating-point numbers does not, so it is blocked separately (`E-PAR-FLOAT`).
    ]
    #para("4a")[
      #strong[The starting value of the accumulating place shall be the identity of that operation.]; Otherwise it is refused (`E-PAR-IDENTITY`).
      When run divided, each piece starts again from that starting value, so a value that is not the identity enters the computation #strong[as many times
      as there are pieces]; — that is, the divided answer differs from the answer run one by one (the promise of (2) above breaks).
      The identity is 0 for `add` · `bit_or` · `bit_xor`, 1 for `mul`, and 0 for `max` of an unsigned width.
    ]
    #plain[
      If running divided is left only as #strong[a declaration];, then when that declaration is wrong the program usually runs fine
      and one day runs differently. So this language checks what is declared — if it cannot be checked, it does not simply not divide;
      it #strong[refuses translation.]; «Fast but sometimes wrong» is not something this language
      sells.
    ]
    #caution("")[
      #strong[Even if it does not run divided, the answer is the same.]; The conditions above are conditions for being divisible, not clauses that change the
      meaning. So the divided answer and the one-by-one answer are always the same — that is exactly why
      `E-PAR-FLOAT` exists.
    ]
  ]
  #sub("6.5.11", "What a body shall have")[
    #para("1")[
      An op that says it gives a value shall give a value #strong[on every path]; (`E-RETURN-PARTIAL`).
      If any one path reaches the end without a value, it does not conform.
    ]
    #para("2")[
      The heads of declarations (`fn` · `proc` · `on` · `test` …) shall have a `do … end` body
      (`E-STMT-NODO`).
    ]
    #para("3")[
      What comes at the outermost level shall begin with #strong[the head of a declaration]; (`E-TOPLEVEL`).
    ]
    #para("4")[
      If something that cannot come inside a form comes, it is refused (`E-FORM-UNEXPECTED`). So is a name that is not an op coming in
      the head position (`E-HEAD-NOT-AN-OP`).
    ]
    #plain[
      Without (1), the declaration «it gives a value» becomes something true #strong[only on some paths];. The caller
      trusts it and uses the value, and on that path there is no value. A declaration is #strong[a promise about every path];,
      so breaking it on even one path means it is not a promise.
    ]
    #rejected("no value on some path", "module ex_partial .

fn f input a u8 . output u8 .
do
  if gt a. 5 . do return 1 . end .
end .                      rem on the path where `a` is 5 or less there is no value", "E-RETURN-PARTIAL")
  ]
  #sub("6.5.12", "Only the errors written occur")[
    #para("1")[
      The errors an op raises shall be within what its `errors` clause writes (`E-ERR-UNDECLARED`).
    ]
    #para("2")[
      An `errors` clause writing #strong[a name not in]; the op's error type is refused too
      (`E-ERR-UNDEF`).
    ]
    #para("3")[
      If another op's failure is passed upward — with `try <op>`, or by returning that `result` as it is with `return <op> …` —
      that op's failure becomes this op's failure. So the errors the called op #strong[can raise];
      shall be within this op's `errors` clause (this op's error type if there is no clause)
      (`E-ERR-UNDECLARED`).
    ]
    #para("3a")[
      The errors a called op can raise are those its `errors` clause writes. If there is no clause, they are #strong[the whole error type];
      of that op — not writing a clause means «any error of this type can occur»,
      and the caller reads it with exactly that meaning.
    ]
    #caution("")[
      The two rules are two sides of the same sentence — #strong[what is written and what is raised shall be the same.]; If one side overflows,
      the caller gets a failure it did not see; if the other side overflows, it handles a failure that will not come
      (for the same reason as #cref("6.4.12") (4)).
    ]
    #rejected("raising an error not written", "module ex_err_undeclared .

enum e do
  bad .
end .

fn f output result u8 e .
do
  return error bad . .   rem `errors bad …` was not written
end .", "E-ERR-UNDECLARED")
    #rejected("passing on someone else's failure with `return`", "module ex_err_handed_on .

enum parse_error do
  bad_digit .
end .

enum load_error do
  too_long .
end .

fn read_digit input c u8 . output result u8 parse_error .
errors bad_digit .
do
  guard le c. 9 . else return error bad_digit . . .
  return ok c. . .
end .

fn load_byte input c u8 . output result u8 load_error .
errors too_long .
do
  guard le c. 200 . else return error too_long . . .
  return read_digit c. . .     rem `bad_digit` is not in `load_byte`'s promise
end .", "E-ERR-UNDECLARED")
    #rejected("an op without a clause passes on a failure it received", "module ex_err_clauseless .

enum parse_error do
  bad_digit .
end .

enum load_error do
  too_long .
end .

fn read_digit input c u8 . output result u8 parse_error .
errors bad_digit .
do
  guard le c. 9 . else return error bad_digit . . .
  return ok c. . .
end .

fn digit_or_fail input c u8 . output result u8 parse_error .
do
  return read_digit c. . .     rem no clause — anything of `parse_error` can occur
end .

fn load_byte input c u8 . output result u8 load_error .
errors too_long .
do
  guard le c. 200 . else return error too_long . . .
  let v u8 try digit_or_fail c. . . .     rem `parse_error` is not in `load_byte`'s promise
  return ok v. . .
end .", "E-ERR-UNDECLARED")
  ]
]

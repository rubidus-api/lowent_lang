## 6.5 Statements and control flow

(1) A ⟦문장|statement⟧ is something that is executed. An expression makes a value; a statement does work.

(1a) So using as a statement a form that **only gives a value and changes nothing** — a pure computation (`add s 100 .` · `eq s 7 .`) or an `fn` call —
      does not conform (`E-VALUE-DISCARDED`). That value goes nowhere. What was usually meant is
      `set s. add s. 100 . .`. A `proc` call is a statement, since its effect is its work.

```lowent-거부: a form that only gives a value cannot be used as a statement · E-VALUE-DISCARDED
module ex_discard .

fn bump input a u64 . output u64 .
  requires lt a. 100 . .
do
  var s u64 a. .
  add s. 100 .   rem what was meant is set s (add s 100) .
  return s. .
end .
```

## 6.5.1 Statements that make names

(1) `let` makes a name that **does not change**. A value once fixed does not change.

(2) `var` makes a name that **can change**. Its value is changed with `set`.

(2a) **Parameters can be `set` too.** A parameter received by value is a **local copy** within this op, so
      changing it does not change the caller's value. Writing a field of a struct parameter (array fields included) is the same —
      it writes to the local copy. The only writes that reach the caller are `mut` · `mut_ref` positions (⟦§8.4⟧).

(2b) So what `requires` · `ensures` say through a parameter name is **the value on entry**.
      Even after the body `set`s that name, what the contract says is the value on entry, unchanged.

```구문: binding
binding      ::= ( "let" | "var" ) name [ alloc-clause ] [ type ] term [ fail-clause ] "."
alloc-clause ::= ( "use" | "keep" ) name "."
fail-clause  ::= "else" leave "."
               | "else" block
               | "else" "error" name block
leave        ::= "return" [ term ] | "break" [ number ] | "continue" [ number ] | "panic" term
```

(3) The statement that makes a name is `let <name> <type> <value> .` (`var` the same) — **the type stands after the name and before the value.**
      If the value does not fit that type, translation is refused. The type is not guessed: it is `let x u64 300 .`, not
      `let x 300 .` (`E-LET-NOTYPE`). Guessed types let bare literals slip past the width check.
      If the value begins with `lit`, the type is written after that `lit`, so it may be left out in front (`let p lit pt do … end . .`).

(3a) No word is inserted between the name and the type. The `be` of the old shape `let <name> be <type> <value> .` is not a word of this
      language — writing it is refused at translation (`E-LET-BE`).

(3b) A type ends by its characters alone: it is a constructor of the closed vocabulary (`u64` · `slice u8` · `result u64 perr` · `array 4 u8`),
      one name, or parentheses. User types that take arguments, and forms whose number of arguments is not fixed (such as `result u64` without
      an error type), are wrapped in parentheses — `let b (box u64) mk. .` · `let r (result u64) ok k. . .`.

(3c) **The allocator clause** stands directly after the name, before the type — `use <allocator>.` gives back at the end of the block, and `keep <allocator>.`
      does not give back even on leaving the block (⟦§6.2.6⟧ (7e)(7f) · ⟦§8.14⟧). An allocator is a variable, so it is closed with a dot:
      `var v use al. option (vec u32 allocs.bump_bytes) vecgen.new al. 8 . .`.
      The old shapes `using <allocator> be` · `using <allocator> keep be` are refused (`E-USING-OLD`). A binding whose type contains `owned`
      (`let n use al. option (owned node) lit node … .`) does not give back at the end of the block even when written with `use` — ⟦§8.5⟧ (10)(11).

(4) After the type **there shall be a value.** Closing without a value is refused at translation (`E-LET-NOVALUE`).
      This language has no way of making a name and giving it a value later.

(4a) The value is one form and **is closed by its own dot** (⟦§6.1.5⟧). After it comes the statement's closing dot, or `else` —
      `let n u64 find k. . .` · `let n u64 find k. . else return 0 . .`. The failure clause stands **before** the statement's closing dot:
      writing `else` after the statement has been closed is refused at translation (`E-STMT-ELSE`).

(5) **The failure clause** — in `let <name> <type> <value> else <leave> . .`, if the type of the value is `option t` · `result t e` and the type written
      is the contents `t`, then when there is a value its contents are bound to the name, and when it is empty (`none` · an error) what follows `else` is executed.
      What follows `else` **shall leave** (`return` · `break` · `continue` · `panic`) — falling through is refused
      (`E-GUARD-FALLTHROUGH`). So after it the name always has contents. `var` is the same.

(5c) The failure clause has three shapes. `else <leave> .` is one leaving statement — that statement is closed by its own dot.
      `else do … end` is several statements — every path of the block shall leave. `else error <name> do … end`
      **binds the error value** of a `result` **to `<name>`** and looks at it inside the block — `<name>` lives only inside that block, and every
      path of the block shall leave. If the value is an `option`, there is no error to receive, so `else error <name>` is refused (`E-BIND-ELSE`).

(5d) After the failure clause comes **the statement's closing dot** — after a leaving statement too (`… else return 0 . .`), and after a block too
      (`… else do … end .`). If that dot is missing, translation is refused (`E-DOT-MISSING`).

(5a) If the value is not an `option` · `result`, or the type of the contents differs from the type written, it is refused (`E-BIND-ELSE`). The type of the value is read
      from the output of the op called · the declaration of the name · the output of an actor handler — for any other expression, first bind it with its type written.

(5b) Binding an `option` · `result` value to its contents type without `else` is refused (`E-TYPE-LET`) — a place that forgot it can fail.
      If stopping is acceptable, write `else panic "…" .` (the effect `panic` follows).

> [!주의]
> What this rule catches is not only the mistake of leaving a blank. If `let x f64 .5 .` is written,
> `.5` is not a floating-point literal (⟦§6.1.4⟧ (2)), so that dot closes the form, and after the name
> **only the type remains.** A person believes they wrote a value while the processor saw none,
> so the diagnostic names that trap.

> [!참고]
> It matters that the default is `let`. A name that does not change needs to be **checked only once** by the reader,
> but a name that can change makes them ask again at every place it is used *"what value is it here"*. That question
> is semantic entropy (⟦§1.3⟧).

```lowent-거부: no value after the type · E-LET-NOVALUE
module ex_let_novalue .

fn f output u8 .
do
  let a u8 .        rem 0 is not put in silently
  return a. .
end .
```

```lowent-거부: the type is not guessed · E-LET-NOTYPE
module ex_let_notype .

fn f output u8 .
do
  let x 300 .
  return x. .
end .
```

```lowent-거부: `be` is not a word of this language — tests (3a) · E-LET-BE
module ex_let_be .

fn f output u64 .
do
  let x be u64 7 . .
  return x. .
end .
```

```lowent-거부: the failure clause comes before the statement's closing dot — tests (4a) · E-STMT-ELSE
module ex_else_dot .

fn find input k u64 . output option u64 .
do
  if eq k. 0 . do return none . end .
  return some k. . .
end .

fn f input k u64 . output u64 .
do
  let n u64 find k. . . else return 0 .
  return n. .
end .
```

```lowent-거부: the allocator clause is `use` · `keep` — tests (3c) · E-USING-OLD
module ex_using_old .

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
end .
```

```lowent 예제: the three failure clauses — tests (5)(5c) · 결과: one(5) = 6 · one(0) = 0 · many(200) = 99 · why(0) = 2000 · why(500) = 1000
module ex_fail_clause .

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
end .
```

## 6.5.2 Conditions — `if`

### Syntax

```구문: if-statement
if-statement ::= "if" term block { "else" "if" term block } [ "else" block ] "."
block        ::= "do" { statement } "end"
```

### Constraints

(1) The type of the condition (`term`) shall be `bool` (⟦§6.2.3⟧).

(2) An `if-statement` is **a statement**. It cannot come in a place where a value goes.

### Dynamic semantics

(3) The condition is computed in turn.

(4) If its value is true, the statements of the first `block` are executed in the order written.

(5) If its value is false and there is an `else if`, (3) is done again with that condition; if there is an `else`, the `block` after it is executed.

(6) If its value is false and there is no `else`, nothing is executed.

### Diagnostics

> [!표] Diagnostics of the if statement
> #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Violation*], [*Diagnostic*],
> [(1) — the condition is not `bool`], [`E-TYPE-COND`],
> [(2) — `if` written in a value position], [`E-IF-VALUE`],
> [syntax — one more dot written after the condition closed (`if c. . do`)], [`E-CLOSER-EXTRA`],
> [syntax — no closing dot at the end of the chain], [`E-DOT-MISSING`],
> [syntax — something other than `do … end` written in the `block` position], [`E-CTRL-NODO`],
> [syntax — `else` written inside the `block`], [`E-STMT-ELSE`],
> )

### Examples

```lowent 예제: three branches — tests (4)(5) · 결과: sign(5) = 1 · sign(0) = 0 · sign(-3) = 2
module ex_if .

export fn sign input a i64 . output u64 .
do
  if gt a. 0 . do
    return 1 .
  end else if eq a. 0 . do
    return 0 .
  end else do
    return 2 .
  end .
end .
```

```lowent-거부: `if` gives no value — tests (2) · E-IF-VALUE
module ex_if_value .

fn pick input a u64 . output u64 .
do
  let x u64 (if gt a 1 do 5 . end else do 6 . end) .   rem set or return in each branch
  return x. .
end .
```

### Notes

> [!산문]
> Why must the body be a block? The condition ends at its own dot (`if gt a. 3 . do`) — the condition never swallows the next word.
> Still, if the body could be written as one statement, the same `if` would have two shapes, and the moment a second statement is added, whether it is inside
> or outside the `if` would depend on indentation. Since the body is always `do … end`, `end` says where the body ends, and the dot after it where the statement ends.

> [!참고]
> That the head's expression ends at `do` and the body is written as a block holds for `while` (⟦§6.5.3⟧) · the four loop heads (⟦§6.5.3.1⟧) ·
> `match` (⟦§6.6⟧) too. If the head's expression is a value that holds a block (`if eq p. lit pt do x 1 . end . . do`), the head's body is
> **the last block** — for the reader's sake, wrap such a value in parentheses.

```lowent-거부: one more dot after the head's expression · E-CLOSER-EXTRA
module ex_ctrl_dot .

fn f input a u64 . output u64 .
do
  if gt a. 3 . . do
    return 1 .
  end .
  return 0 .
end .
```

> [!참고]
> To get a different value per branch, `set` a name or `return` in each branch.

## 6.5.3 Loops — `while`

(1) `while <condition> do <statements> end .` repeats the block while the condition is true. The condition is computed **on each iteration, before entering
      the block** — if it is false from the start, the block does not run even once.

(1a) The condition is one form and is closed by its own dot (`while lt i. n. . do … end .`). Writing one more dot after it is refused at translation
      (`E-CLOSER-EXTRA`). The statement ends at the dot after `end` (⟦§6.1.6⟧ (2b)).

(2) `break` leaves the loop, and `continue` moves to the next iteration. Without a number, both apply to the **innermost**
      loop. `break` carries no value.

(2b) With a number after the word, they apply to the loop **counted that many outward**. `break 2 .` leaves both the innermost loop and the loop
      that encloses it. `continue 2 .` leaves the innermost loop and goes to the next iteration of the loop that encloses it.
      `1` is the same as writing no number. The number is a plain decimal literal, 1 or more, and cannot exceed the number of loops enclosing that statement
      (`E-LOOP-COUNT`). Loops are not given names (labels).

(2c) What is counted are the five loop heads (`while` · `for` · `repeat` · `range` · `cycle`). The blocks of `if` and `match` are not
      counted. What a loop passed on the way out opened is closed as when that loop is left normally.

> [!참고]
> Wrapping or unwrapping a loop makes the same number point at a different loop. Then count again.

(2a) These two are used **only inside loops**. Writing them outside a loop does not conform
      (`E-LOOP-OUTSIDE`) — there is no loop to leave.

(3) The condition of a loop shall be `bool` too. Writing a number in a condition position is refused at translation (`E-TYPE-COND`).

(3a) `while` is a statement and gives no value. It has no `else` branch (`E-STMT-ELSE`).

```lowent 예제: leaving nested loops at once
module ex_break_count .

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
end .
```

```lowent-거부: two loops enclose it, but it tries to leave three · E-LOOP-COUNT
module ex_break_far .

export fn f output u64 . do
  var n u64 0 .
  repeat a u64 3 do
    repeat b u64 3 do
      set n. add n. 1 . .
      break 3 .
    end .
  end .
  return n. .
end .
```

```lowent 예제: conditions and loops
module ex_ctl .

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
end .
```

## 6.5.3.1 Loops that go in order — `for` · `repeat` · `range` · `cycle`

```구문: the four loop heads
for-stmt    ::= "for"    name [ "mut" ] term [ filter ] block "."
repeat-stmt ::= "repeat" name type term [ filter ] block "."
range-stmt  ::= "range"  name type term term [ "step" term ] [ filter ] block "."
cycle-stmt  ::= "cycle"  name type term "while" term "next" term [ filter ] block "."
filter      ::= "if" term
```

(1) The four statements repeat the block **putting the values the head decides into the name in turn**. Each head word does one thing —
      `for` goes over the elements of a source, `repeat` n times from 0, `range` between two ends, and `cycle` over the next values the author writes.
      There are no other shapes.

> [!표] Loop heads
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Head*], [*Shape*], [*What the name takes in turn*],
> [elements], [`for x <slice> do`], [the elements of the slice — from the front],
> [element places], [`for x mut <slice> do`], [the slots of the slice themselves — can be read and written],
> [counting], [`repeat i <type> n do`], [`0, 1, …, n−1`],
> [range], [`range i <type> a b do` · `range i <type> a b step k do`], [from `a` to `b`, both ends included],
> [recurrence], [`cycle i <type> v while c next e do`], [starting at `v`, then `e` on each iteration],
> )

(1a) Each expression of the head is one form and is closed by its own dot — `repeat i u64 len xs. . do` · `range i u64 0 sub n. 1 . do` ·
      `cycle i u64 1 while lt i. n. . next mul i. 2 . do`. The ending words (`do` · `if` · `while` · `next` · `step`) are
      reserved, so they cannot come as names inside an expression. Writing one more dot after a closed expression is refused at translation (`E-CLOSER-EXTRA`).

(1b) The old shapes `for i count <type> n . do` · `for i range <type> a b . do` · `for i be <type> v . while c . next e . do` and
      the filter `. where c .` are refused (`E-FOR-OLD`). `for` does only one job, going over elements.

(2) **Elements.** The source shall be a slice. If it is not a slice, translation is refused (`E-TYPE-ITER`). The type of the name is
      the **element type** of the slice. String literals (slices of bytes) and list literals (`lit slice u16 10 20 30 .`) are
      sources too. If there are no elements, the block does not run even once.

(2a) The source ends at `do` (or at the filter's `if`) — whether it is one name (`for x xs. do`) or an expression (`for x view_array u16 b. . do`),
      it is the same.

(3) **Element places.** In `for x mut buf. do … end .`, `x` is that slot of `buf`: reading it gives the slot's value, and `set x. v. .` writes
      that slot. `buf` shall be `mut` (`E-TYPE-MUT`). During the loop the whole of `buf` is borrowed, so reading or writing
      `buf` inside the block is refused at translation (`E-FOR-HEAD`) — the elements are touched only through `x`.

(3a) In `for x <slice> do` written without `mut`, `x` is a **copy** holding that iteration's element value. `x` cannot be `set` inside the block
      (`E-FOR-HEAD`) — to change the slots, write `mut` in the head.

(4) **Counting.** `repeat i <type> n do` puts `0` to `n−1` into `i` in turn. If `n` is 0 or less, it does not run
      even once. `<type>` is the type of the count and **is an integer type that is always written** — if it is not written, or is not an integer type,
      translation is refused (`E-FOR-STEP`). `n` shall fit `<type>` (`E-TYPE-WIDTH`).

(5) **Range.** `range i <type> a b do` goes from `a` to `b` **with both ends included**. The two ends decide the direction —
      `range i u64 3 5` is `3, 4, 5`, and `range i u64 5 3` is `5, 4, 3`. If the two ends are the same, it runs once.
      Both ends shall fit `<type>` (`E-TYPE-WIDTH`).

(5a) With `step k`, the next value is `current + k` and **the sign of `k` decides the direction.** If the start has already passed the end in that direction,
      it does not run even once (`range i i64 1 10 step -1`). If the next value passes the end, it ends there —
      `range i u8 250 255 step 3` is `250, 253`. `step 0` is refused at translation (`E-FOR-STEP`), and if a `k` not known at translation
      is 0 during execution, it stops.

(5b) The count **does not overflow.** `range i u8 0 255` runs 256 times and ends at 255 — it never goes beyond the type while computing
      what comes after the last value.

(6) **Recurrence.** `cycle i <type> v while c next e do … end .` starts `i` at `v`. Before entering an iteration
      it looks at `c`; if true, it runs the block, and when the block ends it puts the value of `e` into `i`. If `c` is false from the start, it does not run
      even once. `c` shall be `bool` (`E-TYPE-COND`), and `v` and `e` shall fit `<type>` (`E-TYPE-WIDTH`).
      The computation of `e` is ordinary arithmetic — it stops if it overflows (⟦§6.3.4⟧). Both clauses `while` · `next` shall be present, in this order
      (`E-FOR-HEAD`). A `continue` inside the block **goes through** `next` — it puts the value of `e` into `i` and looks at `c` again.

(7) **Filtering.** `if c` can be added at the end of the head, before `do`. It means the same as writing `if not c. . do continue . end .` at the start
      of the block — the condition is computed on each iteration. It attaches to any of the four head words. The `if` inside the head is not a statement but
      a clause — no statement can come inside the head, so they do not overlap.

(8) **What the head read is frozen during the loop.** The end · `step` · source are computed **once** on entering the loop.
      The count name (the name of `repeat` · `range` · `cycle`) and the names the head read cannot be `set` inside the block
      (`E-FOR-HEAD`). Changing the contents of slice slots is allowed.

(9) The name of a loop lives **only inside the block**. When the block ends, that name is gone. A name that already exists in the same block cannot
      be used again (`E-NAME-SHADOW`).

(10) `break` and `continue` are used inside the four loops just as in `while` (⟦§6.5.3⟧ (2)). None of the four gives a value.

```lowent 예제: a head that counts numbers · 결과: the sum of the even numbers from 1 to 10
module ex_for_count .

export fn evens output u64 .
do
  var acc u64 0 .
  range i u64 10 1 step -2 do
    set acc. add acc. i. . .
  end .
  return acc. .
end .
```

```lowent 예제: going through a slice · 결과: adds up all the elements
module ex_for .

export fn total_of input xs slice u8 . output u64 .
do
  var acc u64 0 .
  for x xs. do
    set acc. add acc. widen u64 x. . . .
  end .
  return acc. .
end .
```

```lowent 예제: element places · recurrence · filtering · 결과: powers() = 7 · evens() = 20
module ex_for_more .

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
end .
```

```lowent-거부: the count name cannot be changed inside the block · E-FOR-HEAD
module ex_for_head .

fn f output u64 .
do
  repeat i u64 3 do
    set i. 0 .
  end .
  return 0 .
end .
```

```lowent-거부: `for` goes only over elements — tests (1b) · E-FOR-OLD
module ex_for_old .

fn f output u64 .
do
  var acc u64 0 .
  for i count u64 3 do
    set acc (add acc i) .
  end
  return acc. .
end .
```

> [!주의]
> `in` is **not** a word of this language. Writing `for x in xs` is refused
> (`E-VOCAB-REMOVED`). What is gone through comes directly after the name.

## 6.5.4 A condition that leaves — `guard`

(1) `guard` **leaves on the spot** if the condition is not true. What comes after `else`
      **shall leave on every path**.

(0) `guard <condition> else <leave> . .` — the condition is one form and is closed by its own dot. The leaving statement after `else` is closed
      by its own dot too, and after it comes the dot that closes `guard` (`guard lt i. 9 . else return 0 . .`). The same holds when what follows `else` is a block
      (`guard c. else do … end .`).

(1a) After `else` there may come one statement or a block. If it is a block, **every
      path** of that block shall leave — the leaving statements are `return`, `break`, `continue` and `panic`.

(2) If some path of `else` does not leave but continues downward, translation is refused.

(3) Once past a `guard`, its condition **is guaranteed to be true.** The processor uses that fact in the code
      after it.

```lowent-거부: `else` does not leave · E-GUARD-FALLTHROUGH
module ex_guard_bad .

proc p input n u32 . output u32 . effects none .
do
  guard le n. 5 . else set n. 0 . .   rem continues downward without leaving
  return n. .
end .
```

> [!주의] `guard` is not another name for `if not`
> What `guard` says is *"if not this condition, it ends here"*. So the code below `guard`
> lives only in a world where the condition is true. If `else` does not leave, that promise breaks, and **the code below
> wrongly believes it is true** — so the language refuses it.

```lowent 예제: guard
module ex_guard .

export fn safe_head input data slice u8 . output u8 .
do
  guard ge len data. . 1 . else return 0 . .
  return idx data. 0 . .
end .
```

```lowent 예제: the type is written before the value, and `guard else` may be a block · 결과: inferred() = 7 · guarded(3) = 1 · guarded(9) = 9
module ex_infer_guard .

rem The type is written after the name and before the value — it is not guessed.
fn inferred output u64 .
do
  let a u64 7 .
  return a. .
end .

rem `else` may be a block. The rule is "does every path leave".
fn guarded input n u8 . output u8 .
do
  guard gt n. 5 . else do
    let x u8 1 .
    return x. .
  end .
  return 9 .
end .
```

```lowent-거부: a path of `else` that does not leave is refused · E-GUARD-FALLTHROUGH
module ex_guard_fall .

fn f input n u8 . output u8 .
do
  guard gt n. 5 . else do
    let x u8 1 .
  end .
  return 9 .
end .
```

## 6.5.5 Going back — `return`

(1) `return` ends the op and returns a value. The type of the value returned shall be the same as what is written in
      `output`.

(1a) This rule is the same **wherever** `return` is — the one statement after the `else` of a `guard`, and all statements inside an `else` block,
      receive the same check. So an op with `output result …` returns only with `ok <value>` ·
      `error <variant>` · an expression that gives a `result`, and returning a bare value is refused
      (`E-TYPE-RETURN`). Conversely, an op with a bare type written returning `ok <value>` is refused too.

```lowent-거부: a bare value returned in a `result` place · E-TYPE-RETURN
module ex_bare_under_result .

enum short do
  too_short .
end .

fn head input b slice u8 . output result u8 short .
errors too_short .
do
  guard ge len b. . 2 . else return 0 . .   rem neither `ok 0` nor `error too_short`
  return ok idx b. 0 . . .
end .
```

> [!산문]
> This place was **outside the check** for a while (until 2026-09-25) — the `else` of a `guard` was attached as one lump, not as a list of statements,
> so the checker did not go down into it. So the op above passed translation and
> stopped **during execution** the moment the caller asked `is_ok`. The rarer the path (input too short), the later it shows.

(1b) An op whose output is named (⟦§6.4.1⟧ (3e)) returns that name — `return <name> .`. That name is a local that starts at 0, so
      there is a value to return even if the body never writes it. Another value may be returned too (the name is only a local). That every path shall end with `return`
      is the same as for unnamed outputs (paragraph 3).

(2) An op that returns no value (`output void`) can end early by writing just `return`.

(2a) An op that returns no value may **run to the end of its body** without `return`. There is no value to return,
      so where it ends is where it goes back.

(3) **An op that returns a value** shall end returning a value on every path. If there is a path that ends without a value,
      it does not conform.

(4) The following statements are regarded as ending by returning a value — `return`, an `if` ⋯ `else` both of whose branches return a value,
      and a `match` **all of whose branches return a value**. Whether a `match` covers every case is
      checked separately (⟦§6.6⟧).

(5) `guard` · `while` · `for` have paths that leave, so by themselves they are not regarded as returning a value.

(6) An op whose body is machine instructions (⟦§6.9⟧) gives its value through `out reg`, so it is not the subject of this
      clause.

> [!산문]
> Without this rule, on a path that ends without a value the processor would **put 0 in instead.** That 0 is
> nowhere in the source, so the result cannot be known even after reading the whole program (⟦§1.3⟧).
> So this language does not translate such paths at all.

```lowent 예제: an op that gives no value may end without `return`
module ex_void .

proc hold input n u8 . output void . effects none .
do
  let x u8 n. .
end .
```

> [!산문]
> There is no value to return, so **where it ends is where it goes back**. Had it been an op that returns a value,
> the same shape would be refused (⟦§6.5.5⟧ paragraph 3).

## 6.5.6 Blocks and indentation

(1) A block is opened with `do` and closed with `end`. Indentation **has no meaning** — it is for the reader.

(2) A name made inside a block is visible only inside that block. Reading that name after the block has closed does not conform
      (`E-NAME-SCOPE`) — on a path that did not enter, no value was ever put in that name,
      and the processor does not put 0 in instead (⟦§6.5.5⟧ (3)).

```lowent-거부: reading a name made inside a block from outside the block · E-NAME-SCOPE
module ex_name_scope .

fn pick input a u64 . output u64 .
do
  if gt a. 1 . do
    let big u64 mul a. 2 . .
  end .
  return big. .        rem on the path that did not enter there is no `big`
end .
```

> [!산문]
> Some languages divide blocks by indentation. This language does not, because if invisible
> characters (spaces and tabs) change the meaning of a program, **what is seen and what is meant can diverge**.
> `do` and `end` are visible.

## 6.5.7 Three ways of saying failure

(1) There are only three ways of saying failure, and their uses differ.

> [!표] The three failure channels
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*What*], [*What it says*], [*What the caller does*],
> [`result t e`], [a failure that can be fixed], [asks which side and handles it. If it does not handle it, it passes it upward],
> [`option t`], [no value], [asks whether there is one and takes it out, or gives a value to use instead],
> [the `panic` effect], [a contract was broken], [cannot handle it. The program stops],
> )

(2) The variants that go into the error type of a `result` shall be **the same** as those the op's `errors` clause writes.
      Returning an error not written, or writing one and not returning it, does not conform.

(3) `panic` does not unwind. The program ends at the place where it stops — there is no way of catching it midway
      and carrying on.

```lowent 예제: the three channels answer in different shapes · 결과: halve(100) = ok 50 · halve(250) = err too_big · lookup(2) = some 20 · lookup(7) = none · strict(10) = 11 · strict(250) → trap
module ex_channels .

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
end .
```

> [!산문]
> The question that separates the three is *"what can the caller do"*. A missing file — another file can be
> tried, so it is `result`. What is searched for not being in a list is simply absent, so it is `option`.
> A broken contract means the caller **has already broken the promise**, and that is not something that can be
> fixed — so it stops.

## 6.5.8 Handling failure — `try` and the consuming forms

(1) When calling an op that returns a `result`, `try` takes out the value on success and on failure **passes that error
      on upward as it is**.

(2) An op that uses `try` shall itself be able to return that error. Otherwise it is refused
      (`E-TRY-NORESULT`) — because the failure has nowhere to go. If a tail is attached to change the channel ((4)), or it is handled
      **here** by the means of (3), that op need not return a `result`.

(3) The ways of consuming `result` and `option` are closed to the following — `try` (including with a tail, (4)) ·
      asking (`is_ok` `is_error` `is_some` `is_none`) · taking out
      (`ok_value` `error_value` `some_value`) · giving a value to use instead (`value_or`).

(4) A tail can be attached after `try` to **change the channel**. The value stays the same; only the container changes.

> [!표] Changing channels
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Shape*], [*From what to what*], [*What is lost or gained*],
> [`try <expr> else_none`], [`result` → `option`], [the error **is discarded** — it no longer says why it failed],
> [`try <expr> else_error <variant>`], [`option` → `result`], [**a name is given** to the absence — it says why there is none],
> )

(5) Changing channels **changes the type.** The type of the place where `try <expr> else_none` is written is `option`,
      not the value inside it. Writing a value type and then using a channel change does not conform.

> [!참고]
> This is normative because once it was not so. The processor did not see the tail word, so
> an op written `output u64` **returned `some 7 .`**, and still `--check` was green.
> If the norm does not speak about the place where the channel changes, nobody notices when a tool forgets that place.

(6) Taking out is **a partial operation** (⟦§6.2.8⟧). Taking out the absent side traps.

(7) The default of `value_or` **is evaluated only when there is no value** (lazy evaluation). So in the default position
      an expensive computation, or one that can fail, may be written — if there is a value, it does not run.

> [!참고]
> `value_or some 7 . div 1 0 . .` is **7**. There is a value, so the default side does not run.
> That place is evaluated only when there is no value, and then the trap really happens.
> ☞ Until 2026-08-29 it was not so — the tool evaluated the default **first**, so a program that had a value
> ended in a failure it need not have had. The canon (⟦§6.2.8⟧ · DECISION-0003 C1) had fixed it as lazy evaluation,
> and the tool was fixed to match the canon. The values do not change; what changes is
> **which traps and effects can happen**.

> [!산문]
> Writing the code that checks for failure by hand every time makes it long, and long code gets things left out. `try` reduces that
> repetition to one word while **leaving in the source the fact that the failure goes upward** — no failure
> is silently ignored.

## 6.5.9 Stopping — `panic`

(1) `panic` stops the program at once (⟦§5.4⟧). It is an effect, so an op that uses it shall write
      that effect in its contract.

(2) A pure op (`fn`) cannot use `panic`. But the trap the processor raises when a contract is broken is separate from this,
      and it is not something the op did.

## 6.5.10 Loops that run divided — `parallel`

(1) Attaching the clause `parallel <piece> split .` to a loop declares that the loop may be run by several
      together, **dividing the piece**.

(2) The processor does not believe that word but **checks it.** What it checks is that the steps of the loop do not rely on
      one another, and there are three conditions.

> [!표] The conditions for a loop that can run divided
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*Condition*], [*If violated*], [*What went wrong*],
> [reads only its own share], [`E-PAR-READ`], [if it reads someone else's place, whether that value is still old or already new depends on **who runs first**],
> [writes only its own share], [`E-PAR-WRITE`], [if two steps write the same place, the value that remains depends on the order],
> [does not write to a place that lives across steps], [`E-PAR-CARRY`], [such a place **binds** the steps together. If they must gather, declare it with `reduce <accumulating place> <operation> .`],
> )

(3) If `parallel` names a piece but there is no loop that runs over the length of that piece, it is refused
      (`E-PAR-NOLOOP`) — because it cannot be known what is to be divided.

(4) The gathering operation shall **give the same answer even if the order of grouping changes**. Operations that do not are refused
      (`E-PAR-ASSOC`). Addition of floating-point numbers does not, so it is blocked separately (`E-PAR-FLOAT`).

(4a) **The starting value of the accumulating place shall be the identity of that operation.** Otherwise it is refused (`E-PAR-IDENTITY`).
      When run divided, each piece starts again from that starting value, so a value that is not the identity enters the computation **as many times
      as there are pieces** — that is, the divided answer differs from the answer run one by one (the promise of (2) above breaks).
      The identity is 0 for `add` · `bit_or` · `bit_xor`, 1 for `mul`, and 0 for `max` of an unsigned width.

> [!산문]
> If running divided is left only as **a declaration**, then when that declaration is wrong the program usually runs fine
> and one day runs differently. So this language checks what is declared — if it cannot be checked, it does not simply not divide;
> it **refuses translation.** «Fast but sometimes wrong» is not something this language
> sells.

> [!주의]
> **Even if it does not run divided, the answer is the same.** The conditions above are conditions for being divisible, not clauses that change the
> meaning. So the divided answer and the one-by-one answer are always the same — that is exactly why
> `E-PAR-FLOAT` exists.

## 6.5.11 What a body shall have

(1) An op that says it gives a value shall give a value **on every path** (`E-RETURN-PARTIAL`).
      If any one path reaches the end without a value, it does not conform.

(2) The heads of declarations (`fn` · `proc` · `on` · `test` …) shall have a `do … end` body
      (`E-STMT-NODO`).

(3) What comes at the outermost level shall begin with **the head of a declaration** (`E-TOPLEVEL`).

(4) If something that cannot come inside a form comes, it is refused (`E-FORM-UNEXPECTED`). So is a name that is not an op coming in
      the head position (`E-HEAD-NOT-AN-OP`).

> [!산문]
> Without (1), the declaration «it gives a value» becomes something true **only on some paths**. The caller
> trusts it and uses the value, and on that path there is no value. A declaration is **a promise about every path**,
> so breaking it on even one path means it is not a promise.

```lowent-거부: no value on some path · E-RETURN-PARTIAL
module ex_partial .

fn f input a u8 . output u8 .
do
  if gt a. 5 . do return 1 . end .
end .                      rem on the path where `a` is 5 or less there is no value
```

## 6.5.12 Only the errors written occur

(1) The errors an op raises shall be within what its `errors` clause writes (`E-ERR-UNDECLARED`).

(2) An `errors` clause writing **a name not in** the op's error type is refused too
      (`E-ERR-UNDEF`).

(3) If another op's failure is passed upward — with `try <op>`, or by returning that `result` as it is with `return <op> …` —
      that op's failure becomes this op's failure. So the errors the called op **can raise**
      shall be within this op's `errors` clause (this op's error type if there is no clause)
      (`E-ERR-UNDECLARED`).

(3a) The errors a called op can raise are those its `errors` clause writes. If there is no clause, they are **the whole error type**
      of that op — not writing a clause means «any error of this type can occur»,
      and the caller reads it with exactly that meaning.

> [!주의]
> The two rules are two sides of the same sentence — **what is written and what is raised shall be the same.** If one side overflows,
> the caller gets a failure it did not see; if the other side overflows, it handles a failure that will not come
> (for the same reason as ⟦§6.4.12⟧ (4)).

```lowent-거부: raising an error not written · E-ERR-UNDECLARED
module ex_err_undeclared .

enum e do
  bad .
end .

fn f output result u8 e .
do
  return error bad . .   rem `errors bad …` was not written
end .
```

```lowent-거부: passing on someone else's failure with `return` · E-ERR-UNDECLARED
module ex_err_handed_on .

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
end .
```

```lowent-거부: an op without a clause passes on a failure it received · E-ERR-UNDECLARED
module ex_err_clauseless .

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
end .
```

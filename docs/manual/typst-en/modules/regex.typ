#import "../../typst-ko/lib.typ": *

= `regex` --- Pike VM regular expressions without backtracking <mod-regex>

#modhead(file: "lib/regex.low", layer: [L0 --- pure computation (the caller's scratch)], caps: [none])

Checks and finds *what shape* a string has. Use it for input validation (did only digits arrive), finding patterns in logs, and simple tokenising. A pattern is
*compiled* once into a program, and the same program matches many inputs.

```lowent
use regex as rx .

let n option u64 rx.compile "a{2,4}b" prog st .
guard is_some n else return 1 .
let m option u64 rx.match_at prog "aaab" 0 cl nl mk .
```

The program (`slice u64`), the compiler state and the matcher's workspace are all the caller's slices. So it is `effects none` with no hidden allocation, reentrancy
is free, and it runs inside the VM/native cross-check.

*Why a Pike VM is safe.* Common regex engines *backtrack*. At a fork (`|`, `*`) they follow one path to the end and come back to try another when stuck. When forks
nest, paths to try multiply 2, 4, 8…, and one pattern like `(a*)*b` can take minutes on a few dozen bytes of input --- that is ReDoS (regular expression denial of
service). A Pike VM never goes back. It treats the pattern as an NFA and holds every "place it could be now" in a *thread list* (not OS threads, but values recording
which instruction of the program it stands on), advancing the whole list *one step at a time together* for each input byte. The same place is held only once, so the
list never exceeds the number of instructions. Hence *exponential blow-up is impossible in principle* for any pattern and input --- one match from a fixed start is
O(input × program).

== Design and boundaries

*Syntax accepted*

- Literals, `.` (except newline), `* + ?` (greedy), `[a-z]` and `[^…]`, `|`, `(…)`, `^ $` (string boundaries), escapes `\d \w \s \n \t \r` and literalising special
  characters (`\.` and the like).
- Inside classes, the literals `\n \t \r \\ \] \-`, unions of the sets `\d \w \s`, ranges `a-z` and negation `[^…]`.
- Repetition `{m}`, `{m,}`, `{m,n}` (greedy) --- *expanded at compile time*. A run-time counter would give each thread private state, breaking the invariant "a thread is
  one instruction position" and collapsing deduplication. The upper limit is 64.
- Capture groups `match_caps`, `\X` consuming one UTF-8 character, and Unicode properties `\p{…}` and `\P{…}` (`L N P Z M` and `Lu Ll Lt Lm Lo Nd Nl No`).

*Properties look back after consuming.* Property sets have hundreds to thousands of ranges, so expanding them into a byte automaton yields thousands of instructions ---
unmanageable in a design where the program buffer belongs to the caller. Instead, *after* consuming one code point it looks back to check the property. UTF-8 is
self-synchronising and can be read backwards, and the check consumes no bytes, so the grounds for linearity stay. The tables are held by #modref("unicode")[`unicode`].
Unknown property names are *rejected* --- answering with a broader table would be a silently wrong match.

*Not built* --- properties inside classes (`[\p{L}0-9]`; classes are byte bitmaps), case-insensitive matching (`(?i)`; needs folding tables), and backreferences and
lookaround (closer to never being built --- linearity dies the moment they arrive). Invalid patterns and short buffers are all rejected *as values* (`none`).

== Data structures

A whole program lives in one *program buffer* (`slice u64`). `prog[0]` is the instruction count, `prog[1]` the class count, followed by instructions of three words each,
`[op a b]`. *Code grows from the front, and character class bitmaps (256 bits = 4 words) grow backwards from the tail* --- when the two meet, compilation gives `none`. The
bitmap of class k is at `prog[len − 4·(k+1) .. len − 4·k)`.

#dtable(
  columns: 3,
  id: "mod-regex-opcodes",
  caption: [Instruction codes],
  [*Code · name*], [*a · b*], [*Meaning*],
  [0 CHAR], [byte value], [consume that byte and advance],
  [1 ANY], [---], [consume any byte except newline (10)],
  [2 CLASS], [class number k], [consume a byte in bitmap k],
  [3 SPLIT], [branch 1 · branch 2], [run both branches (branch 1 first = greedy)],
  [4 JMP], [target], [unconditional jump],
  [5 MATCH], [---], [reaching here is a match],
  [6 BOL · 7 EOL], [---], [`^` passes only at position 0, `$` only at `len s`],
  [8 SAVE], [slot number], [record a capture position (consumes no byte)],
  [9 CPPROP], [property code], [is the code point *just consumed* of that property (consumes no byte)],
)

Property codes are 0=L · 1=N · 2=P · 3=Z · 4=M · 5=Lu · 6=Ll · 7=Nd · 8=Lt · 9=Lm · 10=Lo · 11=Nl · 12=No, and *negation adds 16* (`\P{L}` = 16). Because the
negation offset was generous, the encoding did not change by a single bit when the tables grew from 8 to 13.

*Compiler state* `st` is four slots (instruction count · class count · pattern position · failure flag), initialised by `compile`, so only length 4 or more must be
ensured. *Matcher scratch* is the current and next thread lists `clist` and `nlist`, the deduplication stamps `marks`, and the start position arrays `cstart` and `nstart`
that `search` also takes. All are `slice u64`, each *at least the instruction count long*.

== Ops at a glance

#dtable(
  columns: 3,
  id: "mod-regex-ops",
  caption: [Ops of `regex` --- all `effects none`],
  [*op*], [*Signature*], [*When it cannot*],
  [`compile`], [`proc (pat slice u8, prog mut slice u64, st mut slice u64) → option u64`], [`none` on syntax error or short buffer],
  [`match_at`], [`proc (prog slice u64, s slice u8, at u64, clist, nlist, marks) → option u64`], [`none` for no match · empty program · short scratch],
  [`find`], [`proc (prog slice u64, s slice u8, clist, nlist, marks) → option u64`], [`none` if no match anywhere],
  [`search`], [`proc (prog slice u64, s slice u8, clist, nlist, marks, cstart, nstart) → option u64`], [`none` for no match anywhere · empty program · short scratch],
  [`test_at`], [`proc (prog slice u64, s slice u8, clist, nlist, marks) → bool`], [never fails (false is the answer)],
  [`class_has`], [`fn (prog slice u64, k u64, b u64) → bool`], [never fails],
)

Names starting with `rx_` are compiler and matcher internals, invisible from outside. Scratch parameters are all `mut slice u64`.

== Ops in detail

- *`compile pat prog st`* --- needs `len prog ≥ 8` and `len st ≥ 4`. Success gives `some <instruction count>`, which is the minimum length for matcher scratch. `none` for
  an unclosed group `(ab`, `) | * + ?` in atom position, an unclosed `[…]`, a reversed range `[z-a]`, a lone `\` at the end, bad counts (`a{4,2}`, `a{}`, `a{2`, over 64),
  or code meeting bitmaps (buffer too small). The empty pattern is valid --- it compiles to one `MATCH` and matches empty anywhere.
- *`match_at prog s at clist nlist marks`* --- gives *the longest end* (greedy) of a match starting at `at` as `some end` (exclusive). The start is part of the question, so
  it takes `at`. `clist` is the thread list at the current position, `nlist` the list at the next (the two swap each byte), and `marks` stamps "this instruction was already
  added at this position". Without stamps the same place enters several times and the list-length bound breaks. An empty match is a match (`some at`). Cost is O(`len s` ×
  instructions).
- *`find`* --- *the leftmost position where a match starts*. It restarts `match_at` for `at = 0, 1, …, len s`, so the bound is O(n²·m), but each attempt is linear, so there
  is no exponential blow-up. If you need the end, call `match_at` once more from the returned start.
- *`search`* --- answers the same question as `find` *in one pass*. Removing the restart loop means each thread must carry "where it started", hence two more scratch
  slices `cstart` and `nstart`. A start thread is added at each position only while no match has been found yet, so cost is O(input × instructions), and since a further-left
  start always wins, leftmost priority holds. It gives *the same answer* as `find` --- the development repository's tests compare the two.
- *`test_at`* --- is there a match anywhere. Use it when you need only yes or no, not a position.
- *`class_has prog k b`* --- does class `k` of the compiled program contain byte `b`. The bitmap lives in the tail of the program buffer, so it takes `prog`. The range of `k`
  is not checked.

== Using it

*Escapes are two layers deep.* Lowent string literal escapes are a closed set of fourteen (#chref("surface")), and `\d` or `\w` are not among them (others are `E-STR-ESCAPE`). To write regex `\d` in source,
write *`"\\d"`* --- the literal turns `\\` into one backslash, and the regex compiler reads those two bytes `\d` as the digit class.

```lowent
module ex_regex .

use regex as rx .

proc demo input prog mut slice u64 . input st mut slice u64 .
  input cl mut slice u64 . input nl mut slice u64 . input mk mut slice u64 .
  output u64 . effects none .
do
  guard ge (len prog) 32 else return 90 .

  rem "ab.d*" on "abcdddx": . consumes c, greedy d* consumes ddd, end = 6
  let n option u64 rx.compile "ab.d*" prog st .
  guard is_some n else return 1 .
  let m option u64 rx.match_at prog "abcdddx" 0 cl nl mk .
  guard is_some m else return 2 .
  guard eq (some_value m) 6 else return 3 .

  rem as a regex this is [a-c]+z\d --- in source, "\\d"
  let n2 option u64 rx.compile "[a-c]+z\\d" prog st .
  guard is_some n2 else return 4 .
  let m2 option u64 rx.match_at prog "abz7" 0 cl nl mk .
  guard is_some m2 else return 5 .
  guard eq (some_value m2) 4 else return 6 .

  rem "ab$" only at the end --- find gives the leftmost start
  let n3 option u64 rx.compile "ab$" prog st .
  guard is_some n3 else return 7 .
  let f option u64 rx.find prog "xxab" cl nl mk .
  guard is_some f else return 8 .
  guard eq (some_value f) 2 else return 9 .
  let g option u64 rx.find prog "abx" cl nl mk .
  guard eq (is_some g) false else return 10 .
  return 42 .
end
```

`search` and counted repetition have the same shape.

```lowent
proc demo_search input prog mut slice u64 . input st mut slice u64 .
  input cl mut slice u64 . input nl mut slice u64 . input mk mut slice u64 .
  input cs mut slice u64 . input ns mut slice u64 .
  output u64 . effects none .
do
  guard ge (len prog) 32 else return 90 .
  let n option u64 rx.compile "b+c" prog st .
  guard is_some n else return 1 .
  let a option u64 rx.find prog "xxbbbc" cl nl mk .
  guard is_some a else return 2 .
  let b option u64 rx.search prog "xxbbbc" cl nl mk cs ns .
  guard is_some b else return 3 .
  guard eq (some_value a) (some_value b) else return 4 .
  guard eq (some_value b) 2 else return 5 .

  let n3 option u64 rx.compile "a{2,4}b" prog st .
  guard is_some n3 else return 9 .
  let m3 option u64 rx.match_at prog "aaab" 0 cl nl mk .
  guard is_some m3 else return 10 .
  guard eq (some_value m3) 4 else return 11 .
  rem below the lower bound is not a match
  let f option u64 rx.match_at prog "ab" 0 cl nl mk .
  guard eq (is_some f) false else return 12 .
  return 42 .
end
```

The caller provides, for example, `prog` of 32 slots, `st` of 4, and `cl`, `nl`, `mk` (and `cs`, `ns` for `search`) of 32 each --- anything above the instruction count
`compile` returned. `{m,n}` expands, so the instruction count grows with the repetition count.

A backtracking engine would wander through 2#super[64] paths giving `(a*)*b` sixty-four `a`s, but a Pike VM finishes linearly with its thread list.

```lowent
let n option u64 rx.compile "(a*)*b" prog st .
guard is_some n else return 1 .
let f option u64 rx.match_at prog
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" 0 cl nl mk .
guard eq (is_some f) false else return 2 .
```

== Counter-examples

#dtable(
  columns: 2,
  id: "mod-regex-bad",
  caption: [What is rejected as a value, and what at translation],
  [*Call*], [*Result*],
  [`rx.compile "(ab" prog st`], [`none` --- unclosed group],
  [`rx.compile "*a" prog st`], [`none` --- repetition with nothing before it],
  [`rx.compile "abcdefghij" tiny st` (`tiny` of 8 slots)], [`none` --- code meets bitmaps],
  [`rx.match_at prog s 0 short short short`], [`none` --- scratch shorter than the instruction count],
  [`rx.compile "a{4,2}" prog st` · `"a{}"` · `"a{2"` · `"a{65}"`], [`none` --- bad counts (the limit of 64 stops blow-up, since repetition expands)],
  [`rx.compile "\d" prog st`], [`E-STR-ESCAPE` before running --- write `"\\d"`],
)

== Cautions

- *Pass `prog` to matching at the same length as at compilation.* Class bitmaps live at the buffer's tail, addressed relative to `len prog`. Passing a `subslice` keeps the
  instructions but misaligns the bitmap addresses, so `[…]` and `\d\w\s` silently look at the wrong bytes. Do not cut the program; carry it whole.
- *One program buffer, one pattern.* Compiling again overwrites the previous program. To alternate two patterns, provide two buffers.
- *`find` or `search` is a choice of cost.* The answers are the same. For long inputs and frequent searching use `search`; to save scratch on small inputs use `find`.
- *`marks` re-initialises itself every call.* Nothing needs preserving between calls.
- *Watch for empty matches.* Patterns like `a*` succeed with length 0 anywhere, and `find` always gives `some 0` for them. If you need "consumed at least one character",
  compare the end with the start.
- *The matching unit is the byte.* On UTF-8 input, `.` and `[^…]` consume one byte, not one code point. Ask for one code point with `\X`, and for Unicode categories with
  `\p{…}`.

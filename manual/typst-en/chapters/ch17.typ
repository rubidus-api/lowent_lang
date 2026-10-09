#import "../../typst-ko/lib.typ": *

= Designing failure

#chapter-toc()

#prereq(
  ([#chref("option-result"), Types that hold answers], [three ways to report failure]),
  ([#chref("contracts"), Contracts], [`requires` is the caller's responsibility, `errors` the op's]),
  ([#chref("capabilities"), Capabilities], [the entry point shows where the program touches the outside]),
)

#deepqa[
  What question did #chref("option-result") use to separate `result`, `option` and `panic`?
][
  "What can the caller do?" If another path can be tried, `result`; if the thing is simply absent, `option`; if the caller already broke a promise and it
  cannot be fixed, stop. This chapter applies that question not to one op but to *every layer* of a program.
]

#why[
  Knowing all the grammar does not by itself decide where and how failure should be reported. The same "invalid port number" must be a `result` in the
  layer reading a configuration file, and a contract in an inner layer that has already checked it. Get this wrong and checks repeat in layer after layer,
  or no layer checks at all. As the last chapter of Part IV, it lays out how to use contracts, errors and capabilities together to place failure.
]

#organizer[
  You will learn the design that separates values coming from outside from inner invariants, reporting with `result` at the boundary and with contracts
  inside. You will see the criteria for splitting error enums and what to put in `errors` clauses, and for deciding whether to pass failures up with `try`
  or handle them on the spot as they move up the layers. You will also see where it is fine to throw information away with `option`, and where `panic` is
  acceptable.
]

#chapter-questions()

== The boundary and the inside

A program has *places where values come in from outside*: program arguments, file contents, bytes from the network, user input. Such values carry no
promises. And there is an *inside* where values that passed a check flow around.

The first principle of the design is to separate the two.

#dtable(
  columns: 3,
  id: "errdesign-where",
  caption: [Where failure is reported],
  [*Place*], [*Nature of values*], [*How to report*],
  [Boundary (parsing, input)], [Anything can arrive], [`result` and `errors`],
  [Inside (computation)], [Already passed a check], [`requires` · `range` · `newtype`],
  [No layer can handle it], [A state that must not exist], [`panic`],
)

#demo("examples/ch17/parse.low")

`parse_u16` is a boundary. The bytes may be empty, may not be digits, may be too large. All three are *fixable failures*, so they are returned as a
`result`. Demanding "digits only, please" with a contract would be the wrong design --- the caller hands the bytes to this op as soon as it receives them,
so it has no way to keep that demand.

Drawn as where a value comes from and where it goes:

```text
 outside (no promises)    boundary             inside (already filtered)
 args · files · bytes ──▶ parse_u16 ── ok ──────▶ slot_of …
                          result + errors      requires · range · newtype
                             │ error
                             ▼
                          the calling layer decides what to do
```

#qa[
  How does an error written without a condition, like `errors not_digit .`, differ from one with a condition, like `errors empty eq len s. . 0 . .`?
][
  With a condition, it becomes an outgoing contract: "under this condition, *exactly* this error happens". Returning normally while the condition holds
  is a contract violation. Callers can trust that avoiding the condition means they need not handle that error. Errors whose condition cannot be written as
  a contract expression (are all bytes digits?) are written without one and promise only "it can happen". Where a condition can be written, writing it tells
  callers more.
]

== How to split an error enum

Give an error variant to *each case where the caller would act differently*. `empty`, `not_digit` and `too_big` were split because a different message can be
shown for each. If callers do the same thing for all of them, merge the variants; more variants mean more work for the code that handles them with `match`.

Conversely, avoid a giant enum that holds the errors of unrelated layers in one op. When file errors, parse errors and configuration errors are mixed into one
enum, the `errors` clause in the head paints a wider picture than the op can actually produce, and declarations of impossible errors are rejected by
translation (#chref("contracts")).

== Moving up the layers

A layer that receives a failure does one of two things: *pass it on* or *handle it there*.

#demo("examples/ch17/layers.low")

- `slot_of` is inside. If the number of slots were 0, the remainder operation would stop, but that is the caller's fault, so it is written as
  `requires gt slots. 0 . .`.
- `check_port` is close to a boundary. Port 0 means the configuration is wrong and can be fixed, so it is a `result`.
- `pick_slot` passes the failure up with `try`, and writes the same error in its own `errors`. After success it knows `p` is not 0 and calls the inner op.
- `main` is the outermost layer. There is nowhere further to pass the failure, so it *handles* it --- writes a message and returns exit code 1.

The path a failure takes upward when `port` is 0. Going down, layers call; coming up, each passes the failure on or handles it.

```text
 main ─────────────────────────  handles it: writes "bad port", returns 1
  │  ▲ error port_zero
  ▼  │
 pick_slot ────────────────────  passes it on: try sends it up as is
  │  ▲ error port_zero      │
  ▼  │                      ▼ only after success
 check_port                 slot_of
 (boundary: result)         (inside: requires gt slots 0)
```

As a rule: *if the nearest layer that could receive the failure can handle it, do not pass it on; if it cannot, pass it on.* `try` shrank the cost of passing
it on to one word, but the fact that it is passed on stays in the caller's `errors` clause.

#misconception[It is safest if every op returns a `result`][
  If even the inside returns `result`s, every call gets a `try` and every head an `errors`. Yet inside, the failure *cannot happen* --- the boundary already
  filtered it out. Writing impossible failures creates handling code, and that code never runs. Write inner invariants with contracts and types (`range`,
  `newtype`). Then the check stays once at the boundary, and inside it is used as a fact and actually removed.
]

== Where information may be thrown away

`port_or_default` does not need the *reason* for failure. Whatever went wrong, it uses the default port 8080. In such a layer, ask the `result` and ignore it,
or turn it into an `option` with `try … else_none` and use `value_or`. Throwing information away is *that layer's decision*. If a lower layer throws it away
early as an `option`, an upper layer that needs the reason has no way to get it back. So throw away as high up as possible.

== Where `panic` is acceptable

Use `panic` when meeting a state that cannot be recovered from. The criterion is "which layer could handle this state?".

- A program invariant is broken (an internal table is corrupted). No layer can produce a correct answer, so stop.
- Going on would cause greater harm (about to record data that failed verification).

Conversely, wrong user input, a missing file or a dropped connection are not reasons to `panic`; some layer can handle them. And `panic` is an effect, so the
moment you use it, `effects panic` spreads to the head of that op and every op calling it (#chref("effects")). That cost is what makes you use `panic`
sparingly.

#realcase[The core of the HTTP request parser is rejection][
  The standard library's `http` module reads HTTP/1.1 requests. The line its documentation introduces itself with is "its core is *rejection*". Attacks such
  as request smuggling live where a server and a proxy *accept an ambiguous request differently*. So the parser accepts ambiguous input as little as possible
  and returns named errors. When a boundary op says clearly with a `result` what it rejects, the inside can trust the shape of the requests it accepted
  (#chref("lib-io-net")).
]

== Common mistakes

#antipattern[Calling `panic` on bad input at the boundary][
  #demo("examples/ch17/mistake_panicinput.low")

  The user only typed `4x`, and the whole program stops. Bad input is *something that happens all the time*, and some layer can deal with
  it --- ask again, use a default, show a message. `panic` removes every one of those choices, and `effects panic` spreads to every caller
  on top of that. Return failure as a `result`, like `parse_u16` at the start of this chapter, and let the calling layer decide what to do.
]

#antipattern[Throwing the reason away in a lower layer][
  #demo("examples/ch17/mistake_dropwhy.low")

  The moment `digit_or_none` turns the error into an `option` with `else_none`, the "why" is gone. The upper layer `explain` wants
  different messages for empty input and non-digit input, but sees 0 for both, and there is no way to get the reason back. Carry the
  reason all the way up and discard it only in the layer that knows it may be discarded.

  #demo("examples/ch17/dropwhy_fixed.low")
]

Split on an error variant directly with `case error <variant name>`.

#demo("examples/ch17/errvariant.low")

When the name after `case error` is a *declared variant*, that arm takes that variant alone. When it is not, it binds the whole error value
to that name (`case error e`). One spelling carries two meanings, but the rule is the one that holds everywhere else in a `match`: a bare
name that is a variant is that variant (#chref("control")). Write one arm per variant and the exhaustiveness check finds the one you
forgot; bind a name instead and that single arm takes every error, so there is nothing left to find.

#antipattern[Calling an op that returns a `result` as a statement][
  #demo("examples/ch17/mistake_dropresult.low")

  `check_port out 0 .` returned an error, but nobody received it: the port is 0, yet "started" would be printed and the exit code would be 0. The
  design of returning failure as a value only holds when the caller *looks at* that value, so the tool reports it as `W-RESULT-DISCARD`.
  Receive the `result` of such an op with `let` and ask, pass it on with `try`, or split it with `match`. Binding it and never reading it gets
  the same warning --- the failure vanishes just the same. Where there genuinely is nothing to do with it (closing a handle on an error path),
  write `drop <name> .` to say you are letting this one go. The fault is not ignoring a failure; it is ignoring it *silently*.
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "errors-design-glance",
  caption: [Shapes for designing failure --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [boundary op `output result t e .` + `errors`], [return the failure of outside values as a value], [the calling layer chooses what to do],
  [inner op `requires` · `range` · `newtype`], [invariants of already-filtered values], [one check stays at the boundary; inside it is removed],
  [`def enum parse_error do empty . not_digit . end .`], [one variant per different caller action], [more variants, more handling work],
  [`let v u16 try check_port port. . . .`], [pass it upward when you cannot handle it], [the passing stays visible in `errors`],
  [`case error e do match e. do … end . end .`], [bind the error value, then split its variants], [the name in `case error <name>` is a new binding unless it names a declared variant],
  [`try … else_none` · `value_or`], [discard the reason], [discard as high up as possible],
  [`proc … effects panic .` + `panic "…"`], [stop in a state no layer can handle], [not for bad input or missing files],
  [entry point `output u8 .`], [the outermost layer --- handle failure and report by exit code], [there is nowhere further to pass it],
)

#recap[
  At boundaries where values come from outside, report failure with `result` and `errors`; inside, where checks have passed, write invariants with contracts
  and types. Give error variants to each case where the caller acts differently. A layer receiving a failure handles it if it can and passes it on with `try`
  if not. Throw information away as high up as possible, and use `panic` only for states no layer can handle.
]

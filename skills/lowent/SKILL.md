---
name: lowent
description: >
  Write, check, run, and build Lowent — a contract-centric, cost-visible systems
  language whose primary author is an AI agent. Load this when a task asks you to
  read or write `.low` code, run `lowentc`, interpret its diagnostics, or generate
  a module's machine-readable API index. Triggers on: ".low", "lowent", "lowentc",
  "fn", "proc", "--diag-json", "--emit-c".
---

# Lowent

Lowent is a small, single-paradigm systems language. Its distinguishing bet is that
**contracts are the interface and costs are visible** — an op declares what it
requires, what it ensures, which effects it has, and which capabilities it needs, and
the compiler checks all of it. The primary author is an agent, so the toolchain is
built to be read by a program, not only a human.

**This skill is the how-to. The normative truth is the standard** — the clause canon
`docs/spec/canon/*.md`. When this file and the canon disagree, the canon wins — tell the user,
don't paper over it.

## The one rule that saves you time

`lowentc` **has no default mode.** You must pick exactly one of `--check`, `--run`,
`--emit-c`, `--test`, `--fmt`, `--doc`, or a dump (`--ir`/`--cst`/`-t`/`--ops`).
Running `lowentc file.low` with no mode prints usage and exits 2.

## Read diagnostics as data, not prose

Always pass **`--diag-json`** when a program (you) will consume the output. Each
diagnostic is one JSON line on **stderr**, and the verdict is one more line:

```
$ lowentc --check --diag-json prog.low
{"rule":"E-CHAR","sev":"error","phase":"lex","span":{"line":3,"col":12},"relation":"violates","msg":"unexpected character"}
{"result":"violations","exit":1}
```

- `rule` is the **stable** code (`E-EFFECT-NO-CAP`, `E-VM-BOUNDS`, `E-CHAR`, …) — branch on it,
  never on `msg`. `msg` is a human convenience and its wording is not a contract.
- `repair`, when present, is a stable **repair id** (`R-ADD-CAP`, `R-DROP-EFFECT`, …) — the kind
  of fix the code determines. No `repair` means the fix depends on intent.
- `phase` is the pass that spoke (`lex`, `parse`, `check`, `ir`, …).
- The `{"result":…,"exit":…}` line means: don't scrape the prose verdict.
- Without `--diag-json` the same information is printed as `== phase (N) ==` headers
  plus `line:col CODE: msg` — fine for a human, awkward for a parser.

⚠ `line`/`col` locate within a file but the record carries **no file identifier**.
With multiple input files you cannot tell which file from the JSON alone. Don't invent
one.

## The loop

```sh
# 1. static check — contracts, effects, ownership, visibility, capabilities
lowentc --check --diag-json prog.low

# 2. run one op on the VM (slice args are [a,b,c]; scalars are bare)
lowentc --run main prog.low
lowentc --run sum prog.low [1,2,3,4]

# 3. run the `test` blocks
lowentc --test prog.low

# 4. build native — emits C on stdout, then hand it to a C compiler
lowentc --emit-c prog.low > prog.c && cc -O2 prog.c -lm -o prog

# 5. generate the machine-readable API index (see below)
lowentc --doc-out out/ prog.low
```

Multiple `.low` files on one command line join into **one compilation unit**.
A source states its own dependencies with `use <name> from "<path>" .` — the path is
relative to the file that declares it, and it links itself.

## Emitting docs for the next agent (REQ-0006 — this is why the skill exists)

`--doc-out DIR` writes an agent-facing bundle, following the **llms.txt** convention:

- `llms.txt` — curated index: `# module`, a one-line `>` summary, then a linked
  `## Ops` list. The token-efficient entry point.
- `<module>.md` — one Model-Card-style section per op (kind, signature, effects,
  capabilities, `requires`/`ensures`, errors, def-hash, and tests collected as
  examples).
- `llms-full.txt` — the whole thing inlined, when a reader wants it in one file.
- `<module>.lowctx` — the compact machine card (`--doc` also prints to stdout).

So when a downstream agent needs to *use* a Lowent module, run `--doc-out` and give it
the `llms.txt` — that is the intended handoff.

## Anatomy of an op

```lowent
module sorted_search .

rem  line comment. block comment is:  note END ... END

fn sorted                         rem  fn = pure (never write `effects`). proc = effectful.
  input xs slice u8 .
  output bool .
  requires ge len xs. . 1 . .        rem  contract flows into the caller
do
  var i u64 1 .
  while lt i. len xs. . . do
    guard le idx xs. sub i. 1 . . idx xs. i. . . else return false . .
    set i. add i. 1 . .
  end .
  return true .
end .
```

Shape rules you will hit immediately:
- **A name opens, a stop closes.** `ge len xs. . 1 .` is `ge(len(xs), 1)`: `xs.` is the variable,
  the next stop closes `len`, the last one closes `ge`. A variable is always written with its stop
  (`xs.`). Literals and `true`/`false`/`none` take none, and neither do names being *defined*
  (`let x`, `input n`, type names, names in a `case` pattern). **Arity does not end a form — only
  its stop does.** Parentheses are optional decoration: `ge (len xs. .) 1 .` means the same.
- **Single-word identifiers.** No multi-word names; snake_case by convention
  (`mut_ref`, `file_system`).
- **`.` closes every form** — values, statements, clauses, declarations, each enum variant, and
  block forms after their `end` (`if c. do … end .`, `fn … end .`; an `else` chain takes one stop,
  at the very end). One stop too few is `E-DOT-MISSING`, one too many `E-CLOSER-EXTRA`. A newline
  is just whitespace: it never closes anything. Glued *inside* a name, `.` qualifies it
  (`vecgen.open`, `err.too_short`); there is no `p.x` field access — write `field p. x .`.
- **Statements you will write constantly**: `let n u64 add a. 1 . .` (the call's stop, then the
  binding's) · `set i. add i. 1 . .` (the place is a value too) · `return x. .` ·
  `guard gt n. 0 . else return 0 . .` · `method o. name. arg. .` · `payload v. variant field .`.
- **Clauses have one order**: `satisfies` right after the name, then `comptime` inputs,
  capability/region inputs, data inputs, then `output`, `effects`, `link`, then `requires`,
  `ensures`, `errors`, `tests` (`E-CLAUSE-ORDER`). Each clause closes with its own stop:
  `proc save input fs cap file_system . input name slice u8 . output u64 . effects io . do … end .`.
- **Every body is `do … end`**: op bodies, control blocks AND block declarations —
  `def struct p do x u8 . end .`, `def enum e do a . end .`,
  `trait t do area input s self . output u64 . end .`,
  `actor c do state do v u64 . end . … end .` (`def struct p .` / a bare line break → `E-STMT-NODO`).
  A trait signature has no `fn`/`proc`; its `effects` line says what the op may do.
- **Capabilities are named at the use site**: a host leaf takes its capability as the first
  operand (`write_out out. 1 s. .`, `alloc_bytes al. capacity n. .`); holding it is not enough
  (`E-CAP-MISSING`).
- **Infix arithmetic only inside `expr`** (`+ - * /`), and the island closes with its own stop:
  `expr a. + b. * 2 .`. Everywhere else it is prefix (`add sub mul div mod`). Comparisons/logic
  are always prefix words (`eq ne lt le gt ge and or not`).
- `fn` promises **no effects**; an `fn` that does IO is `E-EFFECT-CALC`, and writing
  `effects none` on an `fn` is `E-EFFECT-REDUNDANT`. Effectful work is `proc` with a declared
  `effects …` set and the capabilities it needs.

## When something won't build

- `--check` is green but `--emit-c` says an op **cannot be lowered**: it passed every
  static check but is outside the runnable core (`--ir` names those ops). It will not
  run; that is a real limit, not a warning to ignore.
- An `E-IR-UNDEF` is not a missing feature — the program names something that isn't
  defined. Fix the program.

## Don't

- Don't parse `msg`. Branch on `code`.
- Don't expect a default mode.
- Don't treat this skill as normative. If it drifts from the canon, the canon is right —
  say so.


#import "../../typst-ko/lib.typ": *

= Modules --- hidden by default

#chapter-toc()

#prereq(
  ([#chref("surface"), The surface], [a dotted name is a path to a declared name]),
  ([#chref("capabilities"), Capabilities], [the heads of exported ops show what a library touches]),
)

#deepqa[
  In #chref("capabilities")'s last case study, what did you need to look at to find out whether an imported library reaches the network?
][
  You look through the heads of the ops the library *exports* for any that take `cap net` (and `cap c` or `cap machine`). This chapter covers what "export"
  means, and how to bring in names from other files.
]

#why[
  Once a program is more than one file, two questions come up: *what is visible from outside?* and *what does this file depend on?* If the first answer is
  blurry, you cannot tell who breaks when internals change; if the second is blurry, the same source brings in different things on different machines.
  Lowent has both written in the source. As the first chapter of Part VI, "Abstraction", it starts from the outermost unit of splitting code.
]

#organizer[
  You will learn that one file is one module and that only things marked `export` are visible outside. You will pick up how to import another file with
  `use … from "place"`, how to pass several files as one compilation unit, and how to import standard-library names. You will also see that qualifying with
  the module name does not reach hidden names, the design decision to have no search path, and that the order of top-level declarations does not matter.
]

#chapter-questions()

== One file, one module

#idx("module")
One source file is one module. A module writes its name on the first line, and the module name need not match the file name.

#demo("examples/ch21/geom.low")

The `geom` module exports the `point` struct and the `manhattan` op with `export`. `helper` has no `export`, so it can be used only inside this module.

```text
          module geom (geom.low)                            module app
  ┌────────────────────────────────────┐
  │ export struct point      ─────────┼──▶ door ──▶ geom.point       ✔ visible
  │ export fn manhattan      ─────────┼──▶ door ──▶ geom.manhattan   ✔ visible
  │ fn helper   (no export)            │            geom.helper      ✘ E-VISIBILITY
  └────────────────────────────────────┘
      what is inside the wall leaves only through a door made by export
```

#idx("export")
*Hidden is the default.* What is visible from outside is a promise, and hiding something by mistake is easier to fix than promising something by mistake.
When emitting native code, too, only `export`ed ops become symbols callable from C; everything else is `static`.

== Importing

`use <module> from "<place>" .` imports a module from the file at that place. The place is a path *relative to the file that declares it*.

#demo("examples/ch21/app.low")

Imported names are called qualified by the module name --- `geom.point`, `geom.manhattan`. The attached dot is not an operation on a value but a path to a
declared name (#chref("surface")).

Qualification does not reach hidden names.

#demo("examples/ch21/hidden.low")

In the words of the diagnostic, "a module that cannot keep anything private is not a module, it is a prefix".

#qa[
  Where does `use geom .`, with no `from "…"`, look?
][
  Inside the same compilation unit. Passing several files, as in `lowentc --check app.low geom.low`, makes those files one unit. If the module is not in the
  unit, the compiler says with `W-USE-EXTERNAL` that it cannot confirm the module exists, and rejects actual uses of the name. Standard-library names
  (`use allocs .`) resolve from where the standard modules are installed.
]

== What can be exported, and how to shorten a name

`export` goes on most named declarations. Some do not take it.

#dtable(
  columns: 3,
  id: "modules-export-kinds",
  caption: [Declarations that take `export`],
  [*Declaration*], [*`export`*], [*Why*],
  [`fn` · `proc`], [yes], [ops other modules call],
  [`struct` · `enum` · `type` · `newtype`], [yes], [the input and output types of an exported op must be exported too, or it cannot be used],
  [`trait` · `actor`], [yes], [a promise types in other modules satisfy · an actor other modules spawn],
  [top-level `let` (constant)], [no --- `E-TOPLEVEL`], [constants stay inside the module; to share one, export a `fn` that returns the value],
  [`test`], [no --- `E-TOPLEVEL`], [tests belong to whoever builds that module],
)

When a module name is long, or two modules share a name, shorten it with `as`.

#demo("examples/ch21/alias.low")

After `use geom from "geom.low" as g .` you call `g.point` and `g.manhattan`. The alias is a name used only inside this file; the module's
real name (`geom`) does not change. Inside this file, though, the original name no longer stands --- writing `geom.point` is `E-USE-ALIASED`.
An alias is a rename, not a second name: if a module could be called by two spellings, a reader would have to check that they mean the same
thing. Importing the same name twice gives `E-NAME-COLLISION`, and the remedy that diagnostic suggests is this alias.

#antipattern[Writing an alias and then using the original name][
  #demo("examples/ch21/mistake_aliasold.low")

  The moment you write `as g`, the module's name in this file is `g` and nothing else. `geom.point` is refused with `E-USE-ALIASED`. If both
  spellings stood, code would get changed on one side only, and a reader would have to keep checking that the two are the same module. The one
  place the original name belongs is the `use` line.
]

== There is no search path

Many languages fetch a module from somewhere on a search path when you write just its name. Then what the program depends on becomes knowledge outside the
source, and the same source can bring in different files on different machines. Importing in Lowent is one of three things.

- Write the place: `use geom from "geom.low" .`
- Pass it in the same compilation unit: `lowentc --check app.low geom.low`
- Use a reserved standard-library name: `use allocs .`

The principle is the same when a package manifest (`pkg.low`) lists external dependencies. Dependencies are pinned *with their content hash*, and if the bytes
change the build is refused (#chref("build-test")).

#misconception[A module's name comes from its file name][
  This trips people up in the standard library. What `use` looks for is the `module` declaration inside the file. The module name of `lib/alloc.low` is
  `allocs`, `lib/str.low` is `strings`, and `lib/vec.low` is `vecs`. The list of modules whose names differ is the first table of the standard-library part
  (#chref("lib-map")).
]

== When names collide

A module cannot declare the same name twice.

#demo("examples/ch21/dup.low")

The second does not hide the first; rather, one of the two silently becomes unreachable, so it is rejected. When imported names collide, the call site names
the module explicitly. Importing never overwrites a name that already exists (`E-NAME-COLLISION`).

== Top-level order does not matter

Top-level declarations in a module are independent of order. An op declared later can be called earlier, and ops can call each other.

#demo("examples/ch21/order.low")

`is_even` calls `is_odd`, declared after it, and `is_odd` calls `is_even` back. Local names *inside an op body*, on the other hand, must be declared before use.
A file as a whole is skimmed, but a body is read top to bottom.

== Common mistakes

#antipattern[Using an imported name without its module name][
  #demo("examples/ch21/mistake_bare.low")

  Some languages let you use imported names bare, or spill them all with `import *`. Then you cannot tell by reading whether `point`
  belongs to this file or to some module, and names collide quietly as imports grow. Lowent has no spilling: a name from another module is
  always qualified, as in `geom.point`. The diagnostic appears twice on the same line because the type position of `let` and the `make`
  position are counted separately.
]

#antipattern[Leaving the extension out of the `from` place][
  #demo("examples/ch21/mistake_noext.low")

  The place is never guessed. Write `"geom"` and the tool looks for a file named exactly `geom`; if there is none, it is `E-DEP-MISSING`.
  The reason is the same as for having no search path: once the tool starts trying `.low` or searching other directories, what the program
  depends on moves outside the source again. Write the file name exactly, as in `use geom from "geom.low" .`.
]

#antipattern[Importing a standard module by its file name][
  #demo("examples/ch21/mistake_filename.low")

  The module in the file `lib/str.low` is named `strings`. `use str .` finds no such module, so it warns with `W-USE-EXTERNAL` ("cannot
  confirm it exists"), and the place that actually uses the name is rejected with `E-IR-UNDEF`. Read the warning first and check the module
  name.

  #demo("examples/ch21/filename_fixed.low")
]

#antipattern[Using a hidden type in an exported op's signature][
  #demo("examples/ch21/secretbox.low")

  `secretbox` itself translates, but the exporting side is warned with `W-EXPORT-HIDDEN`: this export cannot be used from outside.
  Without that warning the problem only shows up in the module that imports it.

  #demo("examples/ch21/mistake_privtype.low")

  `make_secret` is exported, but the type of its result, `secret`, is hidden. The importer cannot write a name to hold the result and is
  rejected with `E-VISIBILITY`. The export is unusable. Export the input and output types of an exported op as well.
]

#antipattern[Writing another module's enum variant with the module name in a `case`][
  #demo("examples/ch21/sizes.low")

  The `sizes` module exports the enum `kind` and `classify`, which returns one. If the importing side qualifies the variants with the module name,
  this happens.

  #demo("examples/ch21/mistake_enumcase.low")

  `describe 500` is `big` and answers 2, and `describe 5` answers 1: a qualified name is read as a variant. Until 2026-09-16 it was not ---
  lowering read `case sizes.small` not as a variant but as *a slot that matches any value*, so the first arm took every value and 500 also
  answered 1. The checker narrowed qualified names to variants and lowering did not; the two layers now read the same tree. Writing only the
  variant name is still shorter, and the type of `k` decides which enum it belongs to.

  #demo("examples/ch21/enumcase_fixed.low")
]

#misconception[Two modules must not import each other][
  #demo("examples/ch21/cycle_a.low")

  `cycle_a` imports `cycle_b` and `cycle_b` imports `cycle_a`, yet it translates and runs. Unlike C headers, where only what was read first
  is known, names are resolved after the whole translation unit is gathered --- the same principle that makes the order of top-level
  declarations irrelevant. Still, modules that import each other are easier to read merged into one, or with the shared part moved into a
  third module. The recursion depth is bounded by the contract (`requires le n 10 .`).
]

== This chapter's syntax at a glance

#dtable(
  columns: 3,
  id: "modules-glance",
  caption: [Module syntax --- shape · meaning · why it looks this way],
  [*Shape*], [*Meaning*], [*Why*],
  [`module geom .`], [this file is module `geom` (first line)], [the module name is separate from the file name --- `use` looks for it],
  [`export fn manhattan …` · `export struct point …`], [make it visible outside], [hidden by default --- what is visible is a promise],
  [`use geom from "geom.low" .`], [import from a place relative to the declaring file], [no search path --- dependencies are in the source],
  [`use allocs .`], [import from the same unit or the standard library], [otherwise `W-USE-EXTERNAL`],
  [`geom.point` · `geom.manhattan a b`], [qualify imported names with the module name], [no spilling],
  [`geom.helper` (a hidden name)], [rejected (`E-VISIBILITY`)], [qualifying does not open the door],
  [one name declared twice · two imports with one name], [rejected (`E-NAME-DUP` · `E-NAME-COLLISION`)], [never decide quietly which one is reached],
  [order of top-level declarations], [irrelevant --- they may call each other], [a file is scanned; a body is read top to bottom],
  [`use geom from "geom.low" as g .`], [call the imported module `g` in this file], [untangles long or clashing names --- the module's real name stays],
  [`export let …` · `export test …`], [refused (`E-TOPLEVEL`)], [export a constant through a `fn` that returns it],
  [`case small` on another module's enum], [write only the variant name], [`case sizes.small` matches any value in this edition],
)

#recap[
  One file is one module, and only what is `export`ed is visible outside. `use <module> from "<place>" .` imports from a place relative to the declaring file;
  without a place it looks in the same compilation unit or the standard library. There is no search path. Qualification does not reach hidden names, and
  declaring the same name twice is rejected. Top-level order does not matter, but locals in a body are declared first.
]

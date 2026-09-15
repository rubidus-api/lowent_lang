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

#recap[
  One file is one module, and only what is `export`ed is visible outside. `use <module> from "<place>" .` imports from a place relative to the declaring file;
  without a place it looks in the same compilation unit or the standard library. There is no search path. Qualification does not reach hidden names, and
  declaring the same name twice is rejected. Top-level order does not matter, but locals in a body are declared first.
]

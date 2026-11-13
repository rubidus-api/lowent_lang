#import "../lib.typ": *

#sub("6.10", "Modules")[
  #para("1")[
    One source file is one #t("모듈", "module"). A module writes its own name on its first line.
  ]
  #para("2")[
    A module name need not be the same as the file name.
  ]
  #sub("6.10.1", "What is exported")[
    #para("1")[
      Only what is marked `export` can be used by other modules. What is not marked is used only inside that module.
    ]
    #para("2")[
      That is, #strong[hidden is the default];. The exporting side shall write it.
    ]
    #para("2a")[
      `export` attaches to declarations: `fn` · `proc` · `struct` · `enum` · `type` · `newtype` · `actor` ·
      `trait`. It does not attach to module constants (`let`) · module variables (`var`) · `test` blocks (`E-TOPLEVEL`) ---
      a constant is not part of a module's surface (give it out through an op), and a test belongs to the module that has it.
    ]
    #para("2b")[
      If the signature of an exported op uses a named type that is not exported (struct · enum · newtype · actor), the processor
      reports it (`W-EXPORT-HIDDEN`) --- the importing side cannot write that type, so it cannot make a name to receive
      the value, and that export cannot be used from outside. The program itself conforms, so it is a report, not a
      refusal. Transparent aliases (`type`) are not included here, since the underlying type can be written.
      #strong[Why it is not a refusal];: in a unit made of a single module there is no «outside», so nobody exists who cannot
      use that export. The processor cannot know whether this unit will be used as a library or end as a program,
      so it does not refuse what has not yet done harm.
    ]
    #ex("exporting", "module ex_export .

rem Only what is marked `export` is used by other modules.
export fn visible input n u32 . output u32 .
do
  return mul n. 2 . .
end .

rem This is used only inside this module.
fn hidden input n u32 . output u32 .
do
  return add n. 1 . .
end .")
    #plain[
      Some languages are the other way round — public by default, with a mark to hide. This language
      makes hiding the default because #strong[what is visible from outside is a promise];. Hiding something by mistake
      is easier to fix than promising something by mistake.
    ]
  ]
  #sub("6.10.2", "What is imported")[
    #para("1")[
      `use` imports another module. Where to import it from is written too.
    ]
    #para("2")[
      The things inside are called through the name of the imported module.
    ]
    #para("3")[
      If the module is not at the place to import from, translation is refused.
    ]
    #shape("importing — the form that writes the place", "module <my module name> .

use <module to import> from \"<the place of that module>\" .")
    #para("4")[
      There is also a form that does not write the place. Then that module shall be #strong[in the same translation unit]; —
      when several files are handed over as one unit, it is looked for among them.
    ]
    #shape("importing — within the same unit", "module <my module name> .

use <module to import> .")
    #para("4a")[
      `use <module> [from "<place>"] as <alias> .` #strong[renames how that module is called.]; The alias
      stands in every place where a module name stands — the place that qualifies an op (`<alias>.<op>`), and the place that writes
      a type (`<alias>.<actor>`), alike.
    ]
    #para("4b")[
      Once an alias is written, #strong[the original module name does not stand within that unit]; (`E-USE-ALIASED`).
      An alias is not an additional name but #strong[a renaming];. If both names stood, there would be code that calls the same module
      with two spellings, and the reader would have to check whether the two are the same --- the alias was written because the original
      name was inconvenient, and if that name kept standing, the alias would have done nothing.
    ]
    #plain[
      A discipline this language repeats stands here too: #strong[one meaning, one spelling.]; If there are two ways of writing the same thing,
      a reader has to know both, and someone fixing it has to fix both. The `use` line itself is
      the exception --- that is exactly the place where the original name is written.
    ]
    #shape("importing — under another name", "module <my module name> .

use <module to import> as <alias> .")
    #para("5")[
      If a name whose place is not written is not in the unit, the processor #strong[shall say so.];
      The processor of this edition says so with a warning (`W-USE-EXTERNAL`) — there is no search path for looking for modules,
      so whether that name exists cannot be known from this unit alone.
      If that name is actually used, it is refused then.
    ]
    #note[
      #strong[Having no search path]; is the point of this design. If only a name were written and a tool fetched it from
      somewhere, what the program relies on would become knowledge outside the source — the same source could
      import different things on different machines. Either write the place, or hand them over together as one unit,
      one or the other.
    ]
    #note[
      This place is written as #strong[shapes]; instead of examples that actually run. Importing translates only if the file
      actually exists, so an example that runs anywhere cannot be made. What the checker compiles is
      only `#ex`, and shapes are not its subject.
    ]
    #note[
      Writing in the source where something is imported from connects with the discipline of this language — if only names were written and the tool
      found on its own where they come from, what the program relies on would become knowledge outside the
      source.
    ]
  ]
  #sub("6.10.3", "When names collide")[
    #para("1")[
      The same name cannot be declared twice in one module. Doing so is refused at translation.
    ]
    #para("1a")[
      The top-level names of a module are in #strong[one namespace]; — modules · `fn` · `proc` (including `extern`) · `type` · `newtype` ·
      `struct` · `enum` and its variants · `actor` · `trait` · module `let`/`var` · `test`. Even of different kinds, the same name
      is refused (`E-NAME-DUP`). Fields · ops belonging to a type (`<type>.<name>`) · the state and handlers of an actor · the ops a trait requires
      are inside their container, and in different containers the same name can be used.
    ]
    #para("2")[
      If imported names collide with one another, which one is meant shall be written out at the place of the call.
    ]
    #rejected("the same name declared twice", "module ex_dup .

fn f output u32 . do return 1 . end .
fn f output u32 . do return 2 . end .", "E-NAME-DUP")
    #rejected("even of different kinds it is one namespace — a module let and an fn", "module ex_dup_kinds .

let limit u64 3 .
fn limit output u64 . do return 1 . end .", "E-NAME-DUP")
  ]
  #sub("6.10.4", "The order of declarations")[
    #para("1")[
      The top-level declarations in a module #strong[do not depend on order.]; An op declared later can be called earlier,
      and ops that call each other can be used.
    ]
    #para("2")[
      Inside an op body it is not so — local names #strong[shall be declared before they are used];.
    ]
    #plain[
      The two rules differ because the ways of reading differ. A whole file is read by skimming,
      but a function body is read from top to bottom. If a name that has not appeared yet is used in a body, the reader
      has to go back.
    ]
  ]
  #sub("6.10.5", "Ops inside an op — local ops")[
    #para("1")[
      `fn` · `proc` declarations can be put #strong[directly below]; an op body (in the statement positions of the body's `do … end`). These are called
      #t("로컬 op", "local op")s. Putting them inside `if` · loops · blocks or inside another local op, or declaring types · actors ·
      traits · `test`s · modules inside a body, does not conform (`E-LOCAL-PLACE`).
    ]
    #para("2")[
      The name of a local op is visible #strong[only inside the body of that op];. Inside it, order does not matter as with top-level declarations —
      it can be called before its declaration, and local ops of the same op can call each other and call back (#cref("6.10.4") (1)).
      Other ops do not see that name. Different ops can use the same local name.
    ]
    #para("3")[
      A local op #strong[does not see]; the outer op's parameters · locals · regions · borrows · capabilities. Using such names does not
      conform (`E-LOCAL-CAPTURE`). Values that are needed are passed as parameters. A local op has the same head (clauses) and body
      as an ordinary op, and the same rules of effects · contracts · errors · ownership. The effects of a local `proc` are the effects of the place that calls it (#cref("7")).
    ]
    #para("4")[
      The name of a local op does not hide the name of the outer op, its parameters · locals, or names the module already has
      (`E-NAME-SHADOW`). Local ops of one op cannot have the same name (`E-NAME-DUP`), and the names of core ops cannot
      be used (`E-NAME-BUILTIN`).
    ]
    #para("5")[
      A local op cannot carry `export` · `extern` · `unsafe` (`E-LOCAL-EXPORT`) — to use it from outside, declare it at the top level.
    ]
    #ex("a helper op put close to where it is used", "module ex_local .

export fn count_selected input xs slice u8 . input mask u8 . output u64 .
do
  fn has_bits input x u8 . input bits u8 . output bool .
  do
    return eq bit_and x. bits. . bits. . .
  end .
  return pipe xs. do
    filter has_bits with mask. .
    count .
  end . .
end .")
    #rejected("a local op does not see outer names", "module ex_local_capture .

export fn f input n u64 . output u64 .
do
  fn g input k u64 . output u64 .
  do
    return add k. n. . .
  end .
  return g 1 . .
end .", "E-LOCAL-CAPTURE")
    #rejected("a local op is put only directly below the body", "module ex_local_place .

export fn f input n u64 . output u64 .
do
  if gt n. 1 . do
    fn g input k u64 . output u64 .
    do
      return k. .
    end .
  end .
  return n. .
end .", "E-LOCAL-PLACE")
    #note[
      A local op is not a closure. Only the range in which its name is visible is narrow; it takes nothing secretly from outside, so
      the processor treats it like an ordinary op — there is no object made at run time and no hidden argument.
    ]
  ]
]

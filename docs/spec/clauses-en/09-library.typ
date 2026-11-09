#import "../lib.typ": *

#clause("9", "Library")[
  #para("1")[
    This clause does not specify a list of individual library functions. What it specifies is #strong[the rules a library shall
      keep];. As long as those rules are kept, any library is within the guarantees of this language.
  ]
  #plain[
    The C standard holds the language and the library in one document and specifies function by function. Lowent does not,
    because libraries change much faster than languages. If a list of functions were written here,
    this document would soon be out of date. Writing #strong[rules]; instead does not go out of date.
  ]
  #sub("9.1", "Libraries are written in the same language")[
    #para("1")[
      #strong[The portable layer]; of the standard library is written in the language this document specifies and has no privileges.
      That part receives the same rules as code the author writes directly.
    ]
    #para("1a")[
      But the library as a whole is not one layer. At the places that reach the outside world there are narrow leaves,
      and they cross a #strong[trust boundary]; (#cref("4.4")) using `unsafe` or `extern`.
      That boundary is kept narrow, and where it is is written in the source.
    ]
    #para("1b")[
      Turning names into addresses (DNS) is #strong[a different leaf from making connections];. Both receive `cap net`
      and produce the `io` effect, but there is a reason to separate them: a program that already has an address #strong[never touches];
      name resolution, and tests can measure connections #strong[without a network]; by giving addresses by hand.
      And name resolution can give different answers depending on the machine and the moment, and which line that wavering is on
      is visible in the source.
    ]
    #plain[
      For convenience, one connection leaf that also takes names would be better. But then #emph["how many times does this line touch
      the network"]; could not be known from the source alone --- the same question this language repeats in capabilities and effects,
      and it gets the same answer: #strong[name what is hidden.];
    ]
    #para("2")[
      That is, library ops also write contracts (#cref("6.4")), declare effects (#cref("7.1")),
      and receive capabilities as arguments (#cref("7.2")).
    ]
    #note[
      That libraries have no privileges is a matter of checkability. If there were a hidden
      passage only libraries could use, the program's guarantees would break the moment that passage was crossed.
    ]
  ]
  #sub("9.2", "What libraries shall keep")[
    #para("1")[
      a) #strong[They return failure as a value.]; They do not express failure with special values (−1 and the like)
      (#cref("6.2.8")).
    ]
    #para("2")[
      b) #strong[If destroying can fail, they do not hide it]; (#cref("8.5")).
    ]
    #para("3")[
      c) #strong[They do not make up capabilities themselves.]; Capabilities they need are received as arguments.
    ]
    #para("4")[
      d) #strong[They declare effects honestly];, including the effects of what they call inside.
    ]
    #para("5")[
      e) #strong[For numbers whose base can differ, they write which base.]; For values such as rows · columns · indices · page numbers, where counting from 0
      or from 1 can differ, the op's `lowdoc` or the module contract #strong[shall state it];.
      This language's indices are 0-based, so libraries are #strong[0-based by default];, and if an outside convention
      is 1-based (ANSI cursor positioning of terminals and the like), that conversion is done #strong[inside the op]; — if the caller had to know
      the outside convention, that would be knowledge outside the source (#cref("1.3")).
    ]
    #note[
      Example: the row and column of `term.goto <buffer> <position> <row> <column>` are #strong[0-based]; (the top left of the screen is
      `0 0`). ANSI is 1-based, so the op adds 1 inside before putting it out — that addition is not the caller's
      business.
    ]
    #ex("libraries keep the same rules", "module ex_lib .

enum parse_error do
  too_short .
end .

rem Failure is returned as a value — no special number (−1 and the like) is used.
export fn first_byte input data slice u8 . output result u8 parse_error .
  errors too_short lt len data. . 1 . .
do
  guard ge len data. . 1 . else return error too_short . . .
  return ok idx data. 0 . . .
end .")
    #plain[
      These four are not new rules; they say that #strong[libraries keep the rules already fixed]; too.
      If libraries were made exceptions, the program's guarantees would break the moment such a library was used.
    ]
  ]
  #sub("9.3", "What is language and what is library")[
    #para("1")[
      The core ops and builtin ops of #cref("6.3.3") are #strong[language];. Their names are fixed. The names of core ops cannot be hidden by
      the author, and builtin ops stand only after `call_builtin`, so they do not meet the author's names.
    ]
    #para("1a")[
      Builtin ops are material for building libraries. The standard library provides ops that wrap builtin ops, and application
      programs call those ops — calling builtin ops directly is the business of places that build system libraries and processors.
    ]
    #para("2")[
      Everything else is library, used by importing modules.
    ]
    #para("3")[
      For a feature to enter the language, the following shall hold — it cannot be expressed as a library,
      or expressing it as one hides a cost.
    ]
    #para("4")[
      Between the language and libraries there is one more layer — the #t("리프", "leaf").
    ]
    #term("리프", "leaf")[
      The thinnest piece that touches the operating system or hardware. It cannot be written in Lowent, so the processor
      provides it. A leaf #strong[shall be thin]; — what can be written in Lowent is not a leaf but
      a library.
    ]
    #para("5")[
      Whether a piece qualifies as a leaf is decided by the following question — #emph["can this be written in Lowent."];
      If it can, it is a library. #strong[Convenience does not qualify something as a leaf.];
    ]
    #tbl("The three layers")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Layer*], [*Who makes it*], [*Qualification*],
      [language], [this document], [cannot be expressed as a library, or expressing it as one hides a cost],
      [leaf], [the processor], [touches the operating system · hardware. Cannot be written in Lowent],
      [library], [anyone], [everything else. Written in Lowent],
      )
    ]
    #diagram("the three layers — reading one file", " library   files.open, files.read, files.slurp   Lowent code: owned handle, result
    |
 leaf      file_open, file_read, file_close      from the processor, needs cap file_system
    |
 OS        open, read, close                     the kernel
 language  add, len, idx, narrow ...           fixed words (6.3.3), used at every layer")
    #plain[
      For example, «opening a file and receiving an integer handle» touches the kernel, so it is a leaf. Wrapping that handle
      in #strong[a value that cannot be forgotten]; can be written in Lowent, so it is library.
      «A convenience function that reads a whole file» is library too — being convenient does not make it a leaf.
    ]
    #caution("Convenience is not a reason to enter the language")[
      #emph["It is convenient to write it this way"]; is the business of libraries. Adding a word to the language makes that word something every
      program has to memorise (#cref("6.1.2")). So the language accepts only #strong[what cannot be expressed];.
    ]
  ]
  #sub("9.4", "How ripe is it — the ladder of maturity")[
    #para("1")[
      Each library module writes its #strong[maturity];. There are four rungs, and a higher rung #strong[includes all]; the duties of the lower
      rungs.
    ]
    #tbl("The rungs of maturity and their duties")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Rung*], [*The duties of that rung*],
      [`experimental`], [the maturity #strong[is written];. Nothing more — that is why it is an experiment],
      [`incubating`], [＋ a decision document that owns it #strong[actually exists];],
      [`standard`], [＋ there is a description for people to read, and a regression witness calls that module #strong[by name];],
      [`deprecated`], [＋ what to switch to is written],
      )
    ]
    #para("2")[
      A module that does not keep the duties of the rung it writes does not conform. #strong[Raising]; a rung
      comes after meeting the duties; if they are not met, it shall stay on the lower rung.
    ]
    #note[
      #strong[The value of the ladder is not in the upper rungs but in the lower ones.]; Writing `experimental` is
      writing #emph["do not rely on this yet"]; #strong[in the source];, and that one line answers for the user #emph["may I design
      relying on this"];. Without rungs every module looks the same, and then
      #strong[the least ripe is read as if it were the ripest];.
    ]
    #note[
      A rung is #strong[the size of a promise];, not a ranking of quality. A well-made experimental module can be better than a sloppy standard
      module — what differs is #emph["how much it has said it will not change"];.
    ]
  ]
  #sub("9.5", "What enters the standard library")[
    #para("1")[
      Things enter the library #strong[by evidence];, not #strong[by existing];. The following six are the measures
      that decide entry.
    ]
    #tbl("The library charter")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*Measure*], [*What it means*],
      [if it can be written in this language, write it in this language], [something becomes a leaf inside the processor only when it #strong[uses an outside authority]; or is work this language cannot express. #strong[Convenience is not a ground];],
      [the signature is the ledger of costs and authority], [effects · capabilities · the caller's storage · allocation · ownership · completion · machine constraints #strong[shall be visible at the boundary];],
      [separate computation from authority], [building a shape and putting it out, parsing and opening a file, the next step of random numbers and obtaining entropy, protocol encoding and carrying are #strong[each different things];],
      [divide places along several axes], [authority · storage · execution tier (⟦§10.8⟧) · profile (⟦§10.7⟧) #strong[are not squeezed into one ladder];],
      [keep what is inside small], [up to a portable base and thin, audited host adapters. Large outside dependencies · policy-heavy facilities · surfaces still in research are not brought in],
      [each module has evidence], [it declares its support boundary and has unit · negative · oracle · failure · profile gates],
      )
    ]
    #para("2")[
      This document #strong[does not write a list]; of libraries. What exists is decided by a ledger that is counted again from the source
      each time, and if this document copied it, #strong[they would part from the moment of copying.];
    ]
    #plain[
      If the charter doubled as a list, something still a proposal would look #strong[as if it already existed]; just because it was in the table.
      That really happened — three APIs that did not exist were read as fact because they were in the table.
      So this clause specifies only #strong[what enters];, and leaves #strong[what has entered];
      to the counting side.
    ]
  ]
]

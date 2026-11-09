#import "../lib.typ": *

#clause("4", "Conformance")[
  #sub("4.1", "Conforming programs")[
    #para("1")[
      A #t("적합한 프로그램", "conforming program") is a program that keeps all the syntax (#cref("6")) and
      constraints (#cref("6")) of this document.
    ]
    #para("2")[
      A conforming program does not rely on behaviour this document does not specify.
    ]
    #para("2a")[
      Diagnostics come in two kinds: #strong[those that stop translation]; (their names begin with `E-`) and #strong[those that do
      not]; (`W-` · `NOTE-`). Only the former decide conformance; a program can conform even if
      the latter are present.
    ]
    #para("2b")[
      That is, a conforming program is a program with #strong[no diagnostic that stops translation];.
    ]
    #ex("a conforming program", "module ex_conform .

rem This program keeps everything this document specifies.
export fn clamp input n u32 . output u8 .
  requires le n. 255 . .
  ensures le ret. 255 . .
do
  return narrow u8 n. . .
end .

proc main output u8 . effects none .
do
  return 0 .
end .")
    #plain[
      Called as `clamp 200`, the program above gives 200. Called as `clamp 300` —
      since the contract said #emph["at most 255"]; — it #strong[stops right there]; (`E-VM-CONTRACT`).
      Not returning a wrong answer is this language's promise, and that promise is kept at execution.
    ]
    #caution("Conforming and correct are different things")[
      A conforming program can still give a wrong answer. Conformance is #strong[whether the rules of the language were kept];,
      not #strong[whether it does what was intended];. The latter is handled by contracts (#cref("6.4.3")) and tests.
    ]
  ]
  #sub("4.2", "Conforming processors")[
    #para("1")[
      A #t("적합한 처리기", "conforming processor") shall satisfy all of the following.
    ]
    #para("2")[
      a) It shall translate and execute conforming programs with the meaning this document specifies.
    ]
    #para("3")[
      b) It shall issue #strong[at least one diagnostic]; for a program that does not conform. Whether translation continues after a diagnostic
      is decided by the processor.
    ]
    #para("4")[
      c) Where it uses a diagnostic identifier this document specifies, it does not use it with a changed meaning.
    ]
    #para("5")[
      d) Where it provides extensions this document does not specify, they do not change the meaning of a conforming program
      that does not use them.
    ]
    #note[
      b) does not mean #emph["it shall find every error"];. For one non-conforming program, it is enough that #strong[at least one];
      diagnostic is issued. Since Lowent's principle is not to postpone checks, however,
      real processors usually collect and report all errors.
    ]
  ]
  #sub("4.3", "Implementation-defined behaviour and free choice")[
    #para("1")[
      The places where this document leaves discretion to the processor are of #strong[two kinds];, and those places are #strong[explicitly marked];.
    ]
    #term("구현 정의", "implementation-defined")[
      Something the processor decides but #strong[shall document];. Example: the maximum recursion depth. A program can ask
      for the value, and it may differ between processors.
    ]
    #term("자유 선택", "free choice")[
      A place where #strong[the choice must be left free]; for the sake of optimisation or of the processor's circumstances. The processor
      chooses one of two or more behaviours or results this document allows at that place. The processor #strong[need not
      document]; which it chose.
    ]
    #para("1a")[
      For each place of #t("자유 선택", "free choice") this document states #strong[the range that may be chosen from];. The processor chooses only within that range.
    ]
    #para("1b")[
      The processor may choose differently at the same place of the same program at each translation and each execution.
    ]
    #para("1c")[
      #t("자유 선택", "free choice") #strong[is not something left unspecified.]; Whichever member of the range is chosen, it is behaviour this document specifies.
    ]
    #para("1d")[
      A place of #t("자유 선택", "free choice") is marked in the text with a «free choice» box. Annex F gathers all of them, and a place not in annex F
      is not #t("자유 선택", "free choice"). Adding a place or widening a range is a revision of this document.
    ]
    #para("1e")[
      A conforming program does not rely on #strong[which one]; the processor chooses at a #t("자유 선택", "free choice") (#cref("4.1") (2)).
    ]
    #para("1f")[
      There are places this document #strong[has not yet decided];. Such places are not discretion — they are not a choice given to the processor
      but something this document must decide later. They are marked in the text with an «open issue» box and gathered in annex E.
    ]
    #term("모호", "open issue")[
      A place this document #strong[has not yet decided and must decide later];. The possible readings are written side by side.
      Once decided, it becomes a normative paragraph, or, if it is decided to leave it open, a free choice.
    ]
    #para("1g")[
      An open issue and a #t("자유 선택", "free choice") are different. An open issue is #strong[undecided];; a #t("자유 선택", "free choice") is #strong[a decision]; — a decision to leave it open.
      A conforming program does not rely on the place of an open issue either (#cref("4.1") (2)).
    ]
    #para("2")[
      There is no discretion outside these two kinds. In the #strong[safe subset]; (#cref("4.4")) undefined behaviour
      does not exist — no program in it is executed in a state of «nobody knows what will
      happen».
    ]
    #plain[
      In plain words the two kinds are these. #strong[Implementation-defined]; is «it may differ between processors, but the processor
      writes down what it chose». #strong[Free choice]; is «any of these may be chosen, and the choice need not be written down».
    ]
    #plain[
      Free choice exists so that the processor can take a faster path. For example in `add f x. . g y. . .`
      the answer is the same whichever of `f` and `g` is computed first. If the order were nailed down, the processor could not use
      a faster order even when it knew one. So that order is left open.
    ]
    #plain[
      Leaving something open does not mean anything at all may happen. Each place #strong[states the range that may be chosen from];,
      and nothing outside it happens. And such places are exactly the list in annex F — at a place not on the list
      the processor chooses nothing at will. Ranges are fixed narrowly, place by place.
    ]
    #plain[
      The one thing a person writing a program needs to remember: #strong[do not rely on which one is chosen.]; If the order matters,
      write it in a form whose order is fixed (the box in #cref("6.3.1") says how).
    ]
  ]
  #sub("4.4", "The safe subset and trust boundaries")[
    #para("1")[
      The #t("안전한 부분집합", "safe subset") is the set of programs that do not use `unsafe` and `extern`.
      The guarantees of the previous clause are about this set.
    ]
    #para("2")[
      `unsafe` and `extern` mark a #t("신뢰 경계", "trust boundary") (#cref("6.9")).
      At that place the language's guarantee ends and #strong[a promise written by a person]; begins.
    ]
    #para("3")[
      If the promises beyond the boundary are true, the behaviour of the whole program is as this document specifies. If a promise is false,
      this document guarantees nothing — what happens then is not determined.
    ]
    #para("4")[
      So boundaries #strong[shall be narrow];. The wider they are, the larger the area this document's guarantees do not reach.
    ]
    #plain[
      This clause exists for the sake of honesty. One cannot call a function written in another language and say #emph["our language
      has no undefined behaviour"]; — this document does not know what that function will do.
      What can be said is #strong[where the language's guarantee stops and where a person's promise begins];,
      and `unsafe` and `extern` #strong[leave that place in the source];. Being able to find it later is
      as important as the guarantee.
    ]
  ]
  #sub("4.5", "Environmental limits")[
    #para("1")[
      A processor may have resource limits. But when it hits a limit it #strong[shall not silently
      truncate]; — it shall issue a diagnostic or trap.
    ]
    #para("2")[
      A processor shall document the limits it supports (implementation-defined).
    ]
    #para("3")[
      The diagnostics issued when a limit is hit are grouped into families — when a table inside the processor overflows,
      `E-IR-…` · `E-TYPE-LIMIT` · `E-PARSE-LIMIT` are issued; when a resource runs out during execution, `E-VM-…`. This document
      does not list them one by one; #strong[what is required is (1) and (2)]; — not truncating silently, and
      documenting the limits.
    ]
    #plain[
      The names of the diagnostics are not nailed into the norm one by one because they are #strong[the processor's internal
      affairs];. Splitting one table into two changes the names too, and if the norm had to be revised each time, that
      norm would be no more than a shadow following the processor. What the norm shall fix is #strong[what is promised];,
      and the promise is two things: «say so when it overflows» and «write down how far it goes».
    ]
    #caution("")[
      Writing down a limit and #strong[keeping]; a limit are different. Writing it down but truncating silently on overflow
      violates (1); keeping it but not writing it down means the user learns it existed #strong[only after hitting it];.
      Both are needed.
    ]
    #note[
      #emph["Not truncating silently"]; is the point of this clause. Every processor has limits.
      The problem is going past a limit and #strong[carrying on without a word];; the program's result then
      differs from what the source said — and that is semantic entropy (#cref("1.3")).
    ]
  ]
  #sub("4.6", "The edition of this document and the edition of the language")[
    #para("1")[
      This document has #strong[an edition number separate]; from the language. The «document edition» on the cover is this document's edition,
      and the «language edition» is the edition of the language this document describes.
    ]
    #para("2")[
      A rise in the last digit of the document edition means #strong[the text grew or was corrected];, not that the language changed.
      When the language changes, the language edition rises, and the middle digit or higher of this document rises with it.
    ]
  ]
  #sub("4.7", "What is in this document but a processor cannot yet do")[
    #para("1")[
      There may be things this document specifies that a processor #strong[cannot do yet];. Then
      the processor #strong[does not treat them as if they did not exist.];
    ]
    #para("2")[
      At such a place the processor shall say #emph["this word is in the specification, and what cannot do it is the
      processor"];. That is, it is not an error of the program.
    ]
    #para("3")[
      This distinction is normative because #strong[what a person does next differs];. Told #emph["unknown name"];,
      a person goes looking for a typo that is not there; told #emph["not yet"];, they look for another way or
      wait. One diagnostic decides half a person's day.
    ]
    #para("4")[
      A processor shall make such places #strong[countable]; — asked which ops passed every static check but cannot be
      lowered to execution, it shall answer.
    ]
    #plain[
      This is not a clause that «excuses incompleteness». It is the opposite — a clause that says #strong[show]; the
      incompleteness. Between a processor that silently cannot do something and one that says it cannot, there is
      a difference in trust.
    ]
  ]
]

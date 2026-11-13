// 로우엔트(Lowent) 표준 명세 — 영어판을 엮는 자리 (2026-11-09 · W-20261109-1103).
// ★ 정본은 한국어판(`main.typ` · `canon/`)이다. 이 판은 그것을 옮긴 것이며, 둘이 어긋나면 한국어판이 옳다.
//   `--input lang=en` 으로 짓는다(`scripts/build-spec.sh en`). 부록 B ~ F 는 한국어 표에서 나는 생성물이라 한국어 그대로 싣는다.
#import "lib.typ": *

#set document(title: "Lowent Standard Specification " + spec-version + " (English edition)", author: "rubidus")

#if not _html {
  set page(paper: "a4", margin: (x: 2.4cm, y: 2.6cm),
           header: context {
             // 표지에는 머리글을 두지 않는다
             if counter(page).get().first() > 1 [
               #set text(size: 0.8em, fill: rgb("#777"))
               Lowent Standard Specification — English edition #h(1fr) document #spec-version · language #lang-version
               #v(-0.6em) #line(length: 100%, stroke: 0.4pt + rgb("#bbb"))
             ]
           },
           footer: context [
             #set text(size: 0.85em, fill: rgb("#666"))
             #h(1fr) #counter(page).display("1") #h(1fr)
           ])
  set text(font: ("Noto Serif", "Noto Serif CJK KR"), size: 10.5pt, lang: "en")
  set par(justify: true, leading: 0.72em)
  show raw: set text(font: ("D2Coding",), size: 9.2pt)
  show heading: set text(font: ("Noto Sans CJK KR", "Noto Sans"))
  // 조항 제목의 크기 — 번호의 마디 수가 수준이다(lib.typ 의 `_head`).
  show heading.where(level: 1): it => block(above: 0em, below: 1em)[#text(weight: "bold", size: 1.4em)[#it.body]]
  show heading.where(level: 2): it => block(above: 1.6em, below: 0.7em)[#text(weight: "bold", size: 1.18em)[#it.body]]
  show heading.where(level: 3): it => block(above: 1.4em, below: 0.6em)[#text(weight: "bold", size: 1.08em)[#it.body]]
  show heading.where(level: 4): it => block(above: 1.3em, below: 0.55em)[#text(weight: "bold", size: 1.0em)[#it.body]]
  show outline.entry.where(level: 1): set block(above: 0.9em)
  show outline.entry.where(level: 1): set text(weight: "bold")
}
#show raw: set text(font: ("D2Coding",))

#if not _html [
  #v(6cm)
  #align(center)[
    #text(size: 2.4em, weight: "bold", font: ("Noto Sans", "Noto Sans CJK KR"))[Lowent]

    #v(0.2em)
    #text(size: 1.3em, fill: rgb("#555"))[The Lowent Programming Language — Standard Specification]

    #v(0.6em)
    #text(size: 1.05em, fill: rgb("#555"))[English edition]

    #v(2.2em)
    #text(size: 1.1em)[Document edition #spec-version]

    #v(0.4em)
    #text(size: 0.95em, fill: rgb("#666"))[Language edition #lang-version · built #built-on]
  ]
  #v(1fr)
  #align(center)[
    // 겉장의 한마디도 본문의 장치와 같은 모양이다 — 왼쪽 세로선 하나(2026-10-12).
    #block(width: 80%, inset: (left: 9pt, top: 6pt, bottom: 6pt), stroke: (left: 1.2pt + rgb("#d5d5d5")))[
      #set align(left)
      #text(size: 0.9em)[
        This document defines the Lowent language itself. It should be possible to learn and implement the
        language from this document alone, and no place is left that can be understood only by reading another document.

        #v(0.4em)
        *This is a translation.* The normative text is the Korean edition; where the two differ, the Korean edition prevails.
        Clause and paragraph numbers are the same in both editions. Annexes B to F are generated from Korean tables and appear in Korean.
        #include "annex-en/status.typ"
      ]
    ]
  ]
  #pagebreak()
  #outline(title: [Contents], depth: 3, indent: 1.2em)
  #pagebreak()
] else [
  #html.elem("header", attrs: (class: "cover"), {
    html.elem("h1", [Lowent — The Lowent Programming Language Standard Specification (English edition)])
    html.elem("p", [Document edition #spec-version · language edition #lang-version · built #built-on])
    html.elem("p", [This is a translation. The normative text is the Korean edition; where the two differ, the Korean edition prevails. Annexes B to F are generated from Korean tables and appear in Korean.])
    include "annex-en/status.typ"
  })
]

#include "clauses-en/01-scope.typ"
#include "clauses-en/02-normative-refs.typ"
#include "clauses-en/03-terms.typ"
#include "clauses-en/04-conformance.typ"
#include "clauses-en/05-environment.typ"
#include "clauses-en/06-1-lexical.typ"
#include "clauses-en/06-2-types.typ"
#include "clauses-en/06-3-expressions.typ"
#include "clauses-en/06-4-contracts.typ"
#include "clauses-en/06-5-statements.typ"
#include "clauses-en/06-6-more.typ"
#include "clauses-en/06-10-modules.typ"
#include "clauses-en/06-11-traits.typ"
#include "clauses-en/06-12-pipe.typ"
#include "clauses-en/07-effects.typ"
#include "clauses-en/08-memory.typ"
#include "clauses-en/09-library.typ"
#include "clauses-en/10-concurrency.typ"
#include "annex-en/a-grammar.typ"
#include "annex/b-diagnostics.typ"
#include "annex/c-index.typ"
#include "annex/d-builtins.typ"
#include "annex/e-ambiguity.typ"
#include "annex/f-freechoice.typ"

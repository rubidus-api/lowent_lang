// 로우엔트(Lowent) 표준 명세 — 엮는 자리
#import "lib.typ": *

#set document(title: "로우엔트 표준 명세 " + spec-version, author: "rubidus")

#if not _html {
  set page(paper: "a4", margin: (x: 2.4cm, y: 2.6cm),
           footer: context [
             #set text(size: 0.85em, fill: rgb("#666"))
             #h(1fr) #counter(page).display("1") #h(1fr)
           ])
  set text(font: ("Noto Serif", "Noto Serif CJK KR"), size: 10.5pt, lang: "ko")
  set par(justify: true, leading: 0.72em)
  show raw: set text(font: ("D2Coding",), size: 9.2pt)
  show heading: set text(font: ("Noto Sans CJK KR", "Noto Sans"))
}
#show raw: set text(font: ("D2Coding",))

#if not _html [
  #v(6cm)
  #align(center)[
    #text(size: 2.4em, weight: "bold", font: ("Noto Sans CJK KR",))[로우엔트]

    #v(0.2em)
    #text(size: 1.3em, fill: rgb("#555"))[Lowent 프로그래밍 언어 표준 명세]

    #v(2.2em)
    #text(size: 1.1em)[문서 판 #spec-version]

    #v(0.4em)
    #text(size: 0.95em, fill: rgb("#666"))[언어 판 #lang-version · 낸 날 #built-on]
  ]
  #v(1fr)
  #align(center)[
    #block(width: 80%, inset: 0.8em, stroke: 0.5pt + rgb("#999"))[
      #text(size: 0.9em)[
        이 문서는 로우엔트 언어 자체를 정의한다. 이 문서만으로 언어를 배우고 구현할 수
        있어야 하며, 다른 문서를 읽어야 알 수 있는 자리를 남기지 않는다.
      ]
    ]
  ]
  #pagebreak()
  #outline(title: [차례], depth: 2, indent: 1.2em)
] else [
  #html.elem("header", attrs: (class: "cover"), {
    html.elem("h1", [로우엔트 — Lowent 프로그래밍 언어 표준 명세])
    html.elem("p", [문서 판 #spec-version · 언어 판 #lang-version · 낸 날 #built-on])
  })
]

#include "clauses/01-scope.typ"
#include "clauses/02-normative-refs.typ"
#include "clauses/03-terms.typ"
#include "clauses/04-conformance.typ"
#include "clauses/05-environment.typ"
#include "clauses/06-1-lexical.typ"
#include "clauses/06-2-types.typ"
#include "clauses/06-3-expressions.typ"
#include "clauses/06-4-contracts.typ"
#include "clauses/06-5-statements.typ"
#include "clauses/06-6-more.typ"
#include "clauses/06-10-modules.typ"
#include "clauses/06-11-traits.typ"
#include "clauses/06-12-pipe.typ"
#include "clauses/07-effects.typ"
#include "clauses/08-memory.typ"
#include "clauses/09-library.typ"
#include "clauses/10-concurrency.typ"
#include "annex/a-grammar.typ"
#include "annex/b-diagnostics.typ"
#include "annex/d-builtins.typ"
#include "annex/e-ambiguity.typ"
#include "annex/c-index.typ"
#include "annex/f-freechoice.typ"

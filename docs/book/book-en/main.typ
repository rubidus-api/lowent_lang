// Lowent Book — English edition. Build: scripts/build-book-en.sh
#import "../book/style.typ" as st
#import "../book/lib.typ": *

#let book-version = "v0.1.0"
#let book-updated = "2026-09-15"
#let book-status = "draft"
#let book-repo = "https://github.com/rubidus-api/lowent_lang"

#set document(title: "Lowent Book " + book-version, author: "rubidus")
#set page(paper: "a4", numbering: "1",
  margin: (inside: st.margin-inside, outside: st.margin-outside,
           y: st.margin-y), binding: left,
  // ── A running head on every page (author, 2026-08-10) ─────────
  //   Book name on the left, edition on the right. Omitted on the cover and on
  // ── Running head and folio (author, 2026-08-13) ────────────────
  //   ★ 좌철 책의 관례: 펼쳤을 때 *바깥쪽*에 둔다. 쪽 번호는 책장을 넘기며
  //     훑는 것이고 머리말은 「지금 어디인가」를 흘깃 보는 것이라, 둘 다
  //     손가락과 눈이 먼저 닿는 바깥 모서리에 있어야 한다. 안쪽(제본 쪽)에
  //     두면 책을 벌려야 보인다.
  //   · 홀수 쪽(오른쪽 면) --- 바깥은 오른쪽
  //   · 짝수 쪽(왼쪽 면)  --- 바깥은 왼쪽
  //   머리말은 두 조각(책 이름·판 번호)이라 자리를 서로 바꾼다. 관례대로
  //   *책 이름을 안쪽, 판 번호를 바깥쪽*에 둔다 --- 바깥에서 눈에 먼저
  //   들어와야 하는 것은 「어느 판인가」다.
  header: context {
    let here-page = here().page()
    let marks = query(<no-running-head>)
    let skip = marks.any(m => m.location().page() == here-page)
    let n = counter(page).at(here()).first()
    if skip or n == 0 { return }
    set text(font: ("Noto Sans CJK KR", "Noto Sans"), size: st.runhead-size,
             weight: "bold", fill: st.runhead-fill)
    let outer-right = calc.odd(n)
    block(width: 100%, above: 0pt, below: 0pt)[
      #grid(columns: (1fr, 1fr),
        align(left)[#if outer-right [Lowent Book] else [#book-version]],
        align(right)[#if outer-right [#book-version] else [Lowent Book]])
    ]
  },
  footer: context {
    let n = counter(page).at(here()).first()
    if n == 0 { return }
    set text(font: ("Noto Sans CJK KR", "Noto Sans"), size: st.runhead-size,
             fill: st.runhead-fill)
    align(if calc.odd(n) { right } else { left })[#n]
  })
// 영어판 본문은 라틴 Noto 로 조판한다. CJK 판(…CJK KR)의 라틴 자형은
// 본디 다른 글꼴이라 영문 조판에는 맞지 않는다 — CJK 는 뒤에 두어
// 이따금 인용되는 한글만 받는다.
#set text(font: ("Noto Serif", "Noto Serif CJK KR"), size: 10.5pt, lang: "en")
#set par(justify: true, leading: 0.78em, first-line-indent: (amount: 1em, all: true))

// 바깥 주소(URL)로 가는 링크는 눈에 보이게 — 클릭할 수 있다는 표시다
// (저자 지시 2026-08-07). 안쪽 링크(차례·색인·상호 참조)는 그대로 둔다.
#show link: it => if type(it.dest) == str {
  text(fill: rgb("#1A4F8A"), it)
} else { it }
#show heading: set text(font: ("Noto Sans", "Noto Sans CJK KR"))
// 절 제목(1.2 꼴)은 위아래로 숨을 준다 — 기본값은 본문에 너무 붙는다
#show heading: set par(leading: st.head-leading)
#show heading.where(level: 1): set block(below: st.head-tight.at(0))
#show heading.where(level: 2): set block(above: st.head-above.at(1), below: st.head-tight.at(1))
#show heading.where(level: 3): set block(above: st.head-above.at(2), below: st.head-tight.at(2))

// ── Headings are marked by a full-width rule (author, 2026-08-10) ──
// A device says "region" with a vertical rule on the left; a heading says
// "boundary" with a horizontal one. Different jobs, different marks. The
// thickness follows the level --- #chref("regions")/**/.0pt, section 1.0pt, sub 0.5pt.
// 밑줄은 글자 바로 아래에 붙이고(above 를 줄이고), 그만큼을 선 아래 여백으로
// 옮긴다 --- 다음 글과의 거리는 그대로 유지된다(저자 지시 2026-08-10).
#let _head-rule(w, gap) = block(width: 100%, above: 0.05em, below: gap,
                                line(length: 100%, stroke: w + st.head-rule-fill))
#show heading.where(level: 1): it => it + _head-rule(st.head-rule-w.at(0), st.head-gap.at(0))
#show heading.where(level: 2): it => it + _head-rule(st.head-rule-w.at(1), st.head-gap.at(1))
#show heading.where(level: 3): it => it + _head-rule(st.head-rule-w.at(2), st.head-gap.at(2))
#show heading.where(level: 4): it => it + _head-rule(st.head-rule-w.at(3), st.head-gap.at(3))
// 코드는 영어권 독자에게 익숙한 라틴 고정폭 Noto Sans Mono 로 통일한다.
// D2Coding 은 쓰지 않는다. 유니코드·인코딩 설명처럼 코드 안에 한글이
// 꼭 있어야 하는 자리만 Noto Sans CJK KR 이 뒤에서 받는다.
#show raw: set text(font: ("Noto Sans Mono", "Noto Sans CJK KR"), size: 0.96em, ligatures: false)
// ★ Inline code only, enlarged (author, 2026-08-10). Measured: at 0.96em the string
//   `<stdio.h>` renders 4% shorter than the same string in the body face. Noto Serif
//   and Noto Sans Mono happen to share an x-height exactly, so 1.00em matches.
//   (The Korean edition needs 1.06 --- its body face is Noto Serif CJK KR.)
//   Code *blocks* stay at 0.96em: they do not sit beside body text, and widening
//   them risks overflowing an 80-column line.
#show raw.where(block: false): set text(size: 1.0em)
// 인쇄를 위해 구문 강조 색을 쓰지 않는다 (잉크·토너 절약)
// 회색조에서도 구분되는 절제된 구문 강조 (RFC-0006 §7.2)
#set raw(theme: "/book/theme-print.tmTheme", syntaxes: "/book/lowent.sublime-syntax")
// 표·그림 번호는 장마다 1부터 다시 센다 (lib.typ 의 float 카운터)
#show heading.where(level: 1): it => { reset-float-counters(); it }

#set heading(numbering: none)

#page(numbering: none, header: none)[
  #cover-body(
    title: "Lowent Book",
    subtitle: "Systems Programming with Contracts and Effects",
    status: book-status,
    version: book-version,
    updated: [last updated #book-updated],
    author: "rubidus",
    contact: link("mailto:rubidus@gmail.com")[rubidus\@gmail.com],
    repo: link("https://github.com/rubidus-api/lowent_lang")[github.com/rubidus-api/lowent_lang],
    blurb: [
      An introduction to the Lowent language, and its user manual.

      #v(0.3cm)
      Written for readers who have programmed a little, \
      up to those who already use a systems language such as C or Rust.
    ],
  )
]

// 부와 장의 순서는 등록부 한 곳에서 온다 (RFC-0028).
#import "../book/registry.typ" as reg
#let parts = reg.parts.map(p => (
  p.en,
  if p.intro == none { none } else { "parts/" + p.intro + ".typ" },
  p.chapters.map(id => reg.chapter-no.at(id)),
))

// 목차: 장만 나열하면 길어지므로 *부 단위로 묶어* 낸다.
// 장 제목과 쪽 번호는 실제 heading 에서 가져온다(수작업 목록 금지).
// ── Colophon (page 2) ──────────────────────────────────────────
// ★ In a left-bound book this sits at the *foot* of the page after the cover
//   (author's instruction, 2026-08-11). It used to follow the preface, which
//   breaks the reading once --- a colophon is looked up, not read.
#page(numbering: none, header: none)[
  #v(1fr)
  #[
  #set heading(numbering: none)
  #set par(justify: false, first-line-indent: 0em, leading: 0.85em, spacing: 0.95em)
  #show link: it => text(fill: black, it)

  == Copyright and contact

  #metalist(
    ([author], [rubidus]),
    ([contact], link("mailto:rubidus@gmail.com")[rubidus\@gmail.com]),
    ([repository], link("https://github.com/rubidus-api/lowent_lang")[github.com/rubidus-api/lowent_lang]),
    ([edition], [#book-version — #book-status]),
    ([last updated], [#book-updated]),
  )

  #v(0.45cm)

  *The text* — Creative Commons Attribution-NonCommercial-ShareAlike 4.0
  International (CC BY-NC-SA 4.0). You may share and adapt it freely so long as
  you credit the source; commercial use is not permitted, and adaptations must
  carry the same licence.
  #linebreak()
  #text(size: 10pt, fill: rgb("#555555"))[https://creativecommons.org/licenses/by-nc-sa/4.0/]

  *The example code* — the MIT licence. Take it and use it freely. The Lowent
  compiler and standard library used by the examples are under the repository's MIT licence.

  Every demonstration in this book prints output really obtained by checking and
  running the code with `lowentc`; programs that run were also checked to give the
  same answer on the VM and in a native build. The typesetting is done with Typst.

  Lowent is an *experimental* language. Its syntax and library may change, and this
  book is revised along with each edition.

  This book keeps being corrected. What you are reading is the edition numbered
  above; corrections and additions follow it. The newest edition and the record
  of changes are on the author's GitHub. If you are holding an older copy, look
  there first. Error reports and suggestions are received in the same place.
]

= A note on this translation

The Korean edition is the original. This English edition is translated from it
chapter by chapter, and *chapter numbers are kept identical to the original*,
so a cross-reference to "chapter 12" means the same chapter in both editions.
The examples are shared: both editions print the same programs and the same
verified output.

#pagebreak(weak: true)
]

#context {
  let heads = query(heading.where(level: 1)).filter(h => h.numbering != none)
  let by-num = (:)
  for h in heads {
    let n = counter(heading).at(h.location()).first()
    by-num.insert(str(n), h)
  }
  // 번호 없는 1단계 제목(머리말·번역 노트·부록·찾아보기)도 차례에 싣는다.
  let plain = query(heading.where(level: 1)).filter(h => h.numbering == none)
  let ch-pages = heads.map(h => counter(page).at(h.location()).first())
  let first-ch = calc.min(..ch-pages)
  let last-ch = calc.max(..ch-pages)
  let page-of = h => counter(page).at(h.location()).first()
  let front-extra = plain.filter(h => page-of(h) < first-ch)
  let back-extra = plain.filter(h => page-of(h) > last-ch)
  // 장 → 그 장에 딸린 절. 1·2단계를 한 번에 훑어 문서 순서로 묶는다 ---
  // 선택자 조합(.after().before())보다 확실하다(저자 지시 2026-08-10).
  let flat = query(heading.where(level: 1).or(heading.where(level: 2)))
  let secs-by-chapter = (:)
  let cur = none
  for x in flat {
    if x.level == 1 {
      cur = str(x.location().page()) + "-" + str(x.location().position().y.pt())
      secs-by-chapter.insert(cur, ())
    } else if cur != none {
      secs-by-chapter.insert(cur, secs-by-chapter.at(cur) + (x,))
    }
  }
  let secs-of = h => {
    let k = str(h.location().page()) + "-" + str(h.location().position().y.pt())
    secs-by-chapter.at(k, default: ())
  }
  let row = (label, h) => toc-row(label, page-of(h), dest: h.location())
  let sub-row = (no, h) => toc-subrow(no, h.body, page-of(h), dest: h.location())


  block(width: 100%)[
    #set par(leading: 1.0em)
    #text(font: ("Noto Sans", "Noto Sans CJK KR"), size: 15pt, weight: "bold")[Contents]
    #v(0.9em)
    #for h in front-extra {
      row(h.body, h)
      for sh in secs-of(h) { sub-row("", sh) }
    }
    #for (part-title, intro, chs) in parts {
      toc-part(part-title)
      for i in chs {
        let h = by-num.at(str(i), default: none)
        if h != none {
          row([#i. #h.body], h)
          for sh in secs-of(h) {
            let no = numbering("1.1", ..counter(heading).at(sh.location()))
            sub-row(no, sh)
          }
        }
      }
    }
    #if back-extra.len() > 0 {
      toc-part[Appendices and index]
      for h in back-extra {
        row(h.body, h)
        // ★ 부록 A~F 도 소제목까지 편다 (저자 지시 2026-08-10)
        for sh in secs-of(h) { sub-row("", sh) }
      }
    }
  ]
}

#pagebreak()

#set heading(numbering: none)
#include "front/preface.typ"
#include "front/reading-paths.typ"

#set heading(numbering: "1.1")
#counter(heading).update(0)

// ── Body ────────────────────────────────────────────
// Same skeleton as the Korean edition. Only translated chapters are included;
// the heading counter is set per chapter so numbering matches the original.
#let translated = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43)


#for (part-title, intro, chs) in parts {
  let have = chs.filter(c => c in translated)
  pagebreak(weak: true)
  [#metadata("part-page")<no-running-head>]     // no running head on this page
  align(center + horizon)[
    #text(font: ("Noto Sans", "Noto Sans CJK KR"), size: 20pt, weight: "bold", part-title)
    #if intro != none and have.len() > 0 {
      v(1.2cm)
      block(width: 80%, align(left, include intro))
    }
    #if have.len() < chs.len() {
      v(1.0cm)
      block(width: 70%)[
        #set text(size: 10pt)
        #set par(justify: false, first-line-indent: 0em)
        #align(center)[
          Not yet translated:
          #chs.filter(c => c not in translated).map(str).join(", ")
          #linebreak()
          (read these in the Korean edition)
        ]
      ]
    }
  ]
  pagebreak(weak: true)
  for i in have {
    counter(heading).update(i - 1)
    let n = if i < 10 { "0" + str(i) } else { str(i) }
    include "chapters/ch" + n + ".typ"
  }
}

// ── Appendices ──────────────────────────────────────
#pagebreak(weak: true)
#align(center + horizon, text(font: ("Noto Sans", "Noto Sans CJK KR"), size: 20pt, weight: "bold", "Appendices"))
#pagebreak(weak: true)
#set heading(numbering: none)
#include "appendix/a1-vocabulary.typ"
#include "appendix/a2-diagnostics.typ"
#include "appendix/a3-mistakes.typ"
#include "appendix/a4-grammar.typ"

// ── Index ───────────────────────────────────────────
#pagebreak(weak: true)
#include "back/index.typ"

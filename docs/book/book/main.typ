// Lowent Book — 조판 진입점. 빌드: scripts/build-book.sh
#import "style.typ" as st
#import "lib.typ": *

// 이 책의 판 번호. 갱신할 때마다 여기만 고친다 (VERSION.md 와 함께).
#let book-version = "v0.1.0"
#let book-date = "2026년 9월"
#let book-updated = "2026-09-15"          // 최종 수정일
#let book-status = "초안(draft)"           // 판의 성격
#let html-mode = sys.inputs.at("mode", default: "paged") == "html"
#let book-repo = "https://github.com/rubidus-api/lowent_lang"

#set document(title: "Lowent Book " + book-version, author: "rubidus")
#set page(paper: "a4", numbering: "1",
  margin: (inside: st.margin-inside, outside: st.margin-outside,
           y: st.margin-y), binding: left,
  // ── 쪽마다 머리말과 쪽 번호 (저자 지시 2026-08-13) ─────────────
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
#set text(font: ("Noto Serif CJK KR",), size: 10.5pt, lang: "ko")
#set par(justify: true, leading: 0.78em, first-line-indent: (amount: 1em, all: true))

// 바깥 주소(URL)로 가는 링크는 눈에 보이게 — 클릭할 수 있다는 표시다
// (저자 지시 2026-08-07). 안쪽 링크(차례·색인·상호 참조)는 그대로 둔다.
#show link: it => if type(it.dest) == str {
  text(fill: rgb("#1A4F8A"), it)
} else { it }
#show heading: set text(font: ("Noto Sans CJK KR",))
#show heading: set par(leading: st.head-leading)
// 코드 글꼴 = D2Coding (비리가처판, 리가처도 명시적으로 끔)
#show raw: set text(font: "D2Coding", size: 0.96em, ligatures: false)
// ★ 인라인 코드만 키운다 (저자 지적 2026-08-10: `<stdio.h>` 가 작아 보인다).
//   재 보니 실제로 작았다 --- `<stdio.h>` 를 D2Coding 0.96em 으로 찍으면 같은
//   문자열을 본문 글꼴로 찍은 것보다 *10% 낮다*. 본문과 높이를 맞추는 배율이
//   1.065 인데, D2Coding 이든 Noto Sans Mono 든 그 값이 같다 --- 즉 글꼴을
//   바꿔서 될 일이 아니라 크기의 문제였다.
//   코드 *블록*은 그대로 둔다: 블록은 한글과 나란히 놓이지 않아 작아 보이지 않고,
//   11% 를 키우면 80칸 줄이 38.4em 에서 42.6em 으로 넓어져 넘칠 위험이 있다.
#show raw.where(block: false): set text(size: 1.06em)
// 인쇄를 위해 구문 강조 색을 쓰지 않는다 (잉크·토너 절약)
// 회색조에서도 구분되는 절제된 구문 강조 (RFC-0006 §7.2)
#set raw(theme: "/book/theme-print.tmTheme", syntaxes: "/book/lowent.sublime-syntax")
// 표·그림 번호는 장마다 1부터 다시 센다 (lib.typ 의 float 카운터)
#show heading.where(level: 1): it => { reset-float-counters(); it }

#set heading(numbering: "1.1")
#show heading.where(level: 1): it => pagebreak(weak: true) + it
// 절 제목(1.2 꼴)은 위아래로 숨을 준다 — 기본값은 본문에 너무 붙는다
#show heading.where(level: 1): set block(below: st.head-tight.at(0))
#show heading.where(level: 2): set block(above: st.head-above.at(1), below: st.head-tight.at(1))
#show heading.where(level: 3): set block(above: st.head-above.at(2), below: st.head-tight.at(2))

// ── 제목은 전폭 밑줄로 표시한다 (저자 지시 2026-08-10) ──────────
// 장치는 왼쪽 세로선으로 「영역」을 말하고, 제목은 가로줄로 「경계」를 말한다.
// 두 표시가 하는 일이 다르므로 모양도 갈라 둔다. 두께는 수준을 따른다 ---
// 장 2.0pt, 절 1.0pt, 항 0.5pt. 굵기만으로 깊이를 읽을 수 있게.
// 밑줄은 글자 바로 아래에 붙이고(above 를 줄이고), 그만큼을 선 아래 여백으로
// 옮긴다 --- 다음 글과의 거리는 그대로 유지된다(저자 지시 2026-08-10).
#let _head-rule(w, gap) = block(width: 100%, above: 0.05em, below: gap,
                                line(length: 100%, stroke: w + st.head-rule-fill))
#show heading.where(level: 1): it => it + _head-rule(st.head-rule-w.at(0), st.head-gap.at(0))
#show heading.where(level: 2): it => it + _head-rule(st.head-rule-w.at(1), st.head-gap.at(1))
#show heading.where(level: 3): it => it + _head-rule(st.head-rule-w.at(2), st.head-gap.at(2))
#show heading.where(level: 4): it => it + _head-rule(st.head-rule-w.at(3), st.head-gap.at(3))

// ── 표제 ──────────────────────────────────────────────
#page(numbering: none, header: none)[
  #cover-body(
    title: "Lowent Book",
    subtitle: "계약과 효과로 짜는 시스템 프로그래밍",
    status: book-status,
    version: book-version,
    updated: [최종 수정 #book-updated],
    author: "rubidus",
    contact: link("mailto:rubidus@gmail.com")[rubidus\@gmail.com],
    repo: link("https://github.com/rubidus-api/lowent_lang")[github.com/rubidus-api/lowent_lang],
    blurb: [
      이 책은 로우엔트 언어의 입문서이자 사용 설명서입니다.

      #v(0.35cm)
      대상 독자는 프로그래밍을 조금 해 본 사람부터, \
      C·Rust 같은 시스템 언어를 쓰는 사람까지입니다.
    ],
  )
]

// ── 본문 구성 ────────────────────────────────────────
// (제목, 부 도입부 파일 또는 none, 장 번호들)
// 부와 장의 순서는 등록부 한 곳에서 온다 (RFC-0028).
#import "registry.typ" as reg
#let parts = reg.parts.map(p => (
  p.ko,
  if p.intro == none { none } else { "parts/" + p.intro + ".typ" },
  p.chapters.map(id => reg.chapter-no.at(id)),
))

// ── 판권 (2쪽) ──────────────────────────────────────────────────
// ★ 좌철 책의 관행대로 표지 다음 쪽 *아래쪽*에 둔다(저자 지시 2026-08-11).
//   예전에는 머리말 뒤에 있었는데, 그 자리는 읽는 흐름을 한 번 끊는다 ---
//   판권은 「찾아보는 것」이지 「읽는 것」이 아니다.
#page(numbering: none, header: none)[
  #v(1fr)
  // 판 번호는 여기서만 쓰므로 book-version 을 그대로 인용한다.
  #[
  #set heading(numbering: none)
  #set par(justify: false, first-line-indent: 0em, leading: 0.85em, spacing: 0.95em)
  #show link: it => text(fill: black, it)

  == 저작권과 연락처

  #metalist(
    ([지은이], [rubidus]),
    ([연락], link("mailto:rubidus@gmail.com")[rubidus\@gmail.com]),
    ([저장소], link("https://github.com/rubidus-api/lowent_lang")[github.com/rubidus-api/lowent_lang]),
    ([판], [#book-version — #book-status]),
    ([최종 수정], [#book-updated]),
  )

  #v(0.45cm)

  *본문* — 크리에이티브 커먼즈 저작자표시-비영리-동일조건변경허락 4.0 국제
  라이선스(CC BY-NC-SA 4.0). 출처를 밝히면 자유롭게 공유하고 고칠 수 있으나,
  영리 목적 이용은 허용되지 않으며, 고친 결과물에는 같은 라이선스를 적용해야
  합니다.
  #linebreak()
  #text(size: 10pt, fill: rgb("#555555"))[https://creativecommons.org/licenses/by-nc-sa/4.0/]

  *예제 코드* — MIT 라이선스. 자유롭게 가져다 쓰실 수 있습니다. 예제가 쓰는
  로우엔트 컴파일러와 표준 라이브러리는 저장소의 MIT 라이선스를 따릅니다.

  이 책의 모든 코드 시연은 실제로 `lowentc` 로 검사·실행해 얻은 출력을 그대로
  인쇄한 것입니다. 실행하는 예제는 VM 과 네이티브 빌드가 같은 답을 내는지도
  확인했습니다. 조판은 Typst로 했습니다.

  로우엔트는 *실험 단계*의 언어입니다. 문법과 라이브러리가 바뀔 수 있으며, 이 책은
  그 판에 맞춰 함께 고쳐집니다.

  이 책은 계속 고쳐집니다. 지금 읽고 계신 것은 위 번호의 판이고, 그 뒤로도
  오류 수정과 내용 보강이 이어집니다. 가장 새로운 판과 그동안의 변경 내역은
  저자의 GitHub에 있습니다. 오래된 사본을 들고 계시다면 그쪽을 먼저 확인하시는
  편이 좋습니다. 오류 신고와 수정 제안도 같은 자리에서 받습니다.
]
]

#context {
  let heads = query(heading.where(level: 1)).filter(h => h.numbering != none)
  let by-num = (:)
  for h in heads {
    let n = counter(heading).at(h.location()).first()
    by-num.insert(str(n), h)
  }
  // 번호 없는 1단계 제목(머리말·부록·찾아보기)도 차례에 싣는다.
  // 본문 장의 쪽 범위와 견주어 앞부속과 뒷부속을 가른다.
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
  // 차례 한 줄 — 제목과 쪽 번호를 양끝에 두고 눌러서 본문으로 간다
  // 제목과 쪽 번호는 가운데 점선으로 잇는다 (저자 지시 2026-08-06)
  let row = (label, h) => toc-row(label, page-of(h), dest: h.location())

  let sub-row = (no, h) => toc-subrow(no, h.body, page-of(h), dest: h.location())

  block(width: 100%)[
    #set par(leading: 1.0em)
    #text(font: ("Noto Sans CJK KR",), size: 15pt, weight: "bold")[차례]
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
          // ★ 장 제목만이 아니라 절까지 편다 (저자 지시 2026-08-10).
          //   찾는 자리를 앞차례에서 바로 짚을 수 있어야 한다.
          for sh in secs-of(h) {
            let no = numbering("1.1", ..counter(heading).at(sh.location()))
            sub-row(no, sh)
          }
        }
      }
    }
    #if back-extra.len() > 0 {
      toc-part[부록과 찾아보기]
      for h in back-extra {
        row(h.body, h)
        // ★ 부록도 소제목까지 편다 (저자 지시 2026-08-10)
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
#pagebreak(weak: true)

// ── 본문 (13부 #chref("wrapup")) ─────────────────────
// (제목, 부 도입부 파일 또는 none, 장 번호들)
#for (part-title, intro, chs) in parts {
  pagebreak(weak: true)
  [#metadata("part-page")<no-running-head>]     // 이 쪽에는 머리말을 넣지 않는다
  align(center + horizon)[
    #text(font: ("Noto Sans CJK KR",), size: 20pt, weight: "bold", part-title)
    #if intro != none {
      v(1.2cm)
      block(width: 80%, align(left, include intro))
    }
  ]
  pagebreak(weak: true)
  for i in chs {
    let n = if i < 10 { "0" + str(i) } else { str(i) }
    include "chapters/ch" + n + ".typ"
  }
}

// ── 부록 ─────────────────────────────────────────────
#pagebreak(weak: true)
#align(center + horizon, text(font: ("Noto Sans CJK KR",), size: 20pt, weight: "bold", "부록"))
#pagebreak(weak: true)
#set heading(numbering: none)
#include "appendix/a1-vocabulary.typ"
#include "appendix/a2-diagnostics.typ"
#include "appendix/a3-mistakes.typ"
#include "appendix/a4-grammar.typ"

// ── 찾아보기 ─────────────────────────────────────────
#pagebreak(weak: true)
#include "back/index.typ"

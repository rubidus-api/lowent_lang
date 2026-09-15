// 서술 장치 (RFC-0001 §3). 모든 본문 장치는 여기의 함수만 사용한다.

// ── 장치 라벨의 지역화 ────────────────────────────────
// 라벨은 여기 한 곳에만 둔다. 영어판 빌드는 `--input lang=en` 으로 고른다.
// 장치를 고치면 한국어판과 영어판이 함께 바뀐다 (사본을 만들지 않는다).
#let _lang = sys.inputs.at("lang", default: "ko")
#let _html = sys.inputs.at("mode", default: "paged") == "html"

// ── 장 참조 (RFC-0028) ────────────────────────────────
// 원고는 장을 *이름*으로 가리키고, 번호는 등록부에서 온다. 순서를 바꾸면
// `book/registry.typ` 한 곳만 고치면 되고, 없는 이름은 빌드가 잡아 준다.
#import "registry.typ": chapter-no

#let chno(id) = {
  if id not in chapter-no {
    panic("알 수 없는 장 id: " + id + " (book/registry.typ 를 보라)")
  }
  chapter-no.at(id)
}

// 한국어: 32장 / 영어: chapter 32 (문장 첫머리면 cap: true 로 Chapter 32)
#let chref(id, cap: false) = {
  let n = str(chno(id))
  if _lang == "en" { (if cap { "Chapter " } else { "chapter " }) + n }
  else { n + "장" }
}

// 여럿: 33·34장 / chapters 33 and 34
#let chrefs(..ids, cap: false) = {
  let ns = ids.pos().map(id => str(chno(id)))
  if _lang == "en" {
    let head = if cap { "Chapters " } else { "chapters " }
    if ns.len() == 1 { head + ns.at(0) }
    else { head + ns.slice(0, -1).join(", ") + " and " + ns.last() }
  } else { ns.join("·") + "장" }
}

// 범위: 6–9장 / chapters 6–9
#let chrange(from, to, cap: false) = {
  let a = str(chno(from))
  let b = str(chno(to))
  if _lang == "en" { (if cap { "Chapters " } else { "chapters " }) + a + "–" + b }
  else { a + "–" + b + "장" }
}

// 출처 각주. ★ Typst 의 HTML 내보내기는 각주 *참조*만 내보내고 본문을 버린다
// (0.15.1 에서 확인: 참조 13개에 본문 1개). 그래서 HTML 에서는 본문이 사라지지
// 않도록 바로 뒤에 작은 글씨의 인라인 주석으로 편다.
// PDF 에서는 그 쪽 하단의 각주다. HTML 에서는 일단 본문에 펴 두면
// wrap-html.py 의 collect_notes 가 번호를 매겨 *장 끝*의 「주」로 옮긴다.
#let note(body) = if _html {
  html.elem("span", attrs: (class: "src-note"), body)
} else {
  footnote(body)
}
// ── 스타일 이름표 (조판 견본 전용, 저자 지시 2026-08-10) ─────────────
// 견본에서 「이 모양의 이름이 무엇인가」를 그 자리에 함께 보인다. 본문에서는
// 쓰지 않는다 --- 쓰면 책에 이름표가 찍힌다.
#let stylename(name) = if _html {
  html.elem("code", attrs: (class: "style-name"), name)
} else {
  box(inset: (x: 3pt, y: 1pt), outset: (y: 1pt), radius: 1.5pt,
      fill: rgb("#eeeeee"))[
    #text(font: "D2Coding", size: 0.72em, fill: rgb("#555555"), name)
  ]
}

#let _L = (
  ko: (q: "문", a: "답", back: "돌아보기", recap: "복습 정리",
       stdin: "표준 입력으로 준 것", output: "실행 결과",
       platform: "플랫폼 노트", organizer: "이 장이 끝나면",
       why: "이 장의 필요성과 맥락", chtoc: "이 장의 차례",
       prereq: "먼저 알아야 할 것", questions: "이 장에서 답할 질문",
       misc: "흔한 오해", tbl: "표", fig: "그림",
       real: "실제 사례", anti: "반례", math: "수학"),
  en: (q: "Q", a: "A", back: "Looking back", recap: "Recap",
       stdin: "Given on standard input", output: "Output",
       platform: "Platform note", organizer: "By the end of this chapter",
       why: "The need for this chapter, and its context", chtoc: "In this chapter",
       prereq: "What to know first", questions: "The questions this chapter answers",
       misc: "A common misconception", tbl: "Table", fig: "Figure",
       real: "In practice", anti: "Counter-example", math: "The mathematics"),
).at(_lang)

// ── 표·그림 번호와 캡션 (저자 지시 2026-08-06) ─────────
// 번호는 장마다 1부터 다시 센다(main.typ 의 show 규칙이 되돌린다).
// 캡션은 언제나 대상 *아래*, 가운데 정렬로 붙는다. 캡션 글이 없으면
// 번호만 인쇄한다 — 번호는 사람이 손으로 적지 않는다.
#let _float-key = <lowent-float>
#let _tbl-no = counter("lowent-table")
#let _fig-no = counter("lowent-figure")
#let reset-float-counters() = {
  _tbl-no.update(0)
  _fig-no.update(0)
}
#let _float-caption(kind, ctr, cap, id: none) = {
  // step 과 get 은 같은 context 안에서 보면 안 된다 — 단계를 올린 뒤
  // *새* context 에서 읽어야 갱신된 값이 나온다(안 그러면 0 이 찍힌다).
  ctr.step()
  context {
    let chap = counter(heading.where(level: 1)).get()
    let chap = if chap.len() > 0 { str(chap.first()) } else { "0" }
    let no = chap + "." + str(ctr.get().first())
    let label = kind + " " + no
    // 이름으로 가리킬 수 있도록 자리와 번호를 남긴다 (RFC-0029).
    // 부록의 목록이 하나도 빠뜨리지 않도록, id 가 없는 것도 남긴다.
    [#metadata((id: id, kind: kind, no: no, cap: cap))#_float-key]
    if _html {
      html.elem("p", attrs: (class: "float-caption"),
        if cap == none { label } else { [#label — #cap] })
    } else {
      block(width: 100%, above: 6pt, below: 1.1em)[
        #align(center, text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 0.96em)[
          #strong[#label]#if cap != none [ — #cap]])
      ]
    }
  }
}

// ── HTML 마크업을 직접 낸다 ──────────────────────────
// Typst 의 HTML 내보내기를 사후에 훑어 클래스를 붙이던 방식은, 조판 쪽 구조를
// 조금만 바꿔도 서식이 통째로 사라졌다(86장 사고, 2026-08-06). 그래서 장치는
// HTML 을 스스로 낸다 — 클래스가 원고에서 결정되므로 깨질 자리가 없다.
// ── 표·그림 참조 (RFC-0029) ───────────────────────────
// 번호가 아니라 *이름*으로 가리킨다. 표가 늘거나 장이 옮겨져 번호가 바뀌어도
// 원고는 그대로다. 없는 이름은 빌드가 잡는다.
// ★ 조사는 번호의 *읽는 소리*에 달렸다. 「표 40.7」은 칠(ㄹ 받침)로 끝나 "이",
//   「표 40.2」는 이(모음)로 끝나 "가" 다. 번호가 바뀌면 조사도 바뀌어야 하므로
//   원고에 조사를 박아 두지 않고 여기서 고른다 --- `josa: "이/가"` 로 적는다.
#let _has-final(digit) = digit in ("0", "1", "3", "6", "7", "8")   // 영·일·삼·육·칠·팔

#let _josa(no, spec) = {
  let pair = spec.split("/")
  if pair.len() != 2 { panic("조사는 '이/가' 처럼 둘로 적는다: " + spec) }
  if _has-final(no.at(no.len() - 1)) { pair.at(0) } else { pair.at(1) }
}

#let _floatref(id, want, josa) = context {
  let hits = query(<lowent-float>).filter(m => m.value.id == id and m.value.kind == want)
  if hits.len() == 0 {
    panic("알 수 없는 " + want + " id: " + id)
  }
  let m = hits.first()
  let text = m.value.kind + " " + m.value.no
  let tail = if josa == none { "" } else { _josa(m.value.no, josa) }
  if _html { text + tail } else { link(m.location(), text) + tail }
}
#let tblref(id, josa: none) = _floatref(id, _L.tbl, josa)
#let figref(id, josa: none) = _floatref(id, _L.fig, josa)

#let _html-box(cls, label, body) = html.elem("div", attrs: (class: "dev " + cls), {
  if label != none {
    html.elem("p", attrs: (class: "dev-label"), label)
  }
  // ★ 본문은 반드시 한 겹으로 감싼다 (저자 지적 2026-08-10).
  //   선을 자식마다 그으면 ① 문단 사이에서 선이 끊기고 ② 한 문단짜리 본문은
  //   맨 글(텍스트 노드)이라 선을 받지 못한다. 감싸면 둘 다 사라진다.
  html.elem("div", attrs: (class: "dev-body"), body)
})

// 인쇄를 위해 채움(fill)을 쓰지 않는다 — 선의 굵기와 모양으로만 구분한다.
// 박스 안 본문의 첫 칸 들여쓰기.
//
// ★ Typst 의 함정(저자 지적 2026-08-07): 본문이 *한 문단뿐*이면 그 내용은
//   par 요소가 되지 않고 인라인 내용 그대로 블록에 담긴다. 그러면
//   first-line-indent 가 적용될 대상이 없어 들여쓰기가 빠진다 — 그래서 같은
//   실제 사례 상자인데 문단이 둘 이상인 것만 들여쓰기가 되어 보였다.
//   해결: 본문이 순수 인라인 내용일 때만 par() 로 감싼다. 코드 블록·목록·표
//   같은 블록 요소가 섞여 있으면 감싸지 않는다(감싸면 그 요소가 사라진다).
#let _inline-funcs = (
  "text", "space", "strong", "emph", "linebreak", "smartquote", "link",
  "footnote", "box", "h", "sub", "super", "highlight", "underline", "strike",
  "overline", "symbol", "ref", "cite", "metadata",
)
#let _is-inline(c) = {
  let f = repr(c.func())
  if f in ("raw", "equation", "quote") { not c.at("block", default: false) }
  else { f in _inline-funcs }
}
#let _inline-only(c) = {
  if c == none { false }
  else if c.has("children") { c.children.all(x => _is-inline(x)) }
  else { _is-inline(c) }
}
#let _indent-body(body) = if _html { body } else {
  block(width: 100%)[
    #set par(first-line-indent: (amount: 1em, all: true))
    #if _inline-only(body) { par(body) } else { body }
  ]
}

// ── 왼쪽 세로선 규율 (저자 지시 2026-08-10) ────────────────────
// 전에는 장치를 네 변 상자로 둘렀다. 좌우 안쪽 여백이 양쪽에서 폭을 갉아
// 본문이 좁아졌다. 이제 *왼쪽 세로선 하나*로 영역을 표시한다 --- 오른쪽은
// 열어 두어 본문과 같은 폭을 쓴다.
//   · 제목 쪽 선이 내용 쪽 선보다 굵다 (문답의 「문」과 「답」을 가른다)
//   · 두 선은 서로 *붙지 않는다* --- 사이에 여백을 둔다
// ★ 숫자를 여기 적지 않는다 --- 조판 값의 단일 출처는 style.typ 이다(RFC-0027).
#import "style.typ" as st
#let _rail-head = st.rail-head
#let _rail-body = st.rail-body
#let _rail-gap = st.rail-gap
#let _rail-pad = st.rail-pad

#let _device(title, body, rule, icon) = if _html {
  _html-box(
    if icon == _L.real { "realcase" } else if icon == _L.anti { "antipattern" }
    else if icon == _L.math { "mathbox" } else if title == _L.recap { "recap" }
    else { "device" },
    if title != none {
      if icon != none { icon + ". " + title } else { title }
    } else { none },
    body,
  )
} else {
  // 왼쪽 세로선만 --- 제목 줄은 굵게, 내용은 가늘게, 둘 사이에 여백을 둔다
  // ★ 제목 줄의 굵기는 장치마다 다르지 않다(저자 지시 2026-08-10). 예전에는
  //   호출자가 넘긴 `rule` 을 그대로 썼는데, 그것이 상자 시절의 테두리 굵기라
  //   수학 1pt · 실제 사례 2.2pt · 문답 3.2pt 로 갈려 보였다. 이제 `rule` 은
  //   옛 호출부와의 약속으로만 남고, 굵기는 한 곳(style.typ)에서 온다.
  let _ = rule
  let head-rule = _rail-head
  block(width: 100%, above: 1.05em, below: 1.05em, breakable: true)[
    #set par(first-line-indent: 0em)
    #if title != none {
      block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
            inset: (left: _rail-pad, top: st.dev-label-top,
                   bottom: st.dev-label-bottom),
            stroke: (left: head-rule))[
        #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold",
              size: 0.98em, fill: black)[#if icon != none [#icon. #h(3pt)]#title]
      ]
    }
    #block(width: 100%, above: 0pt, below: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[
      #_indent-body(body)
    ]
  ]
}

// ── 장 서두 4종 (RFC-0008 §2) ────────────────────────
// 넷은 하나의 시각 단위다: 굵은 왼쪽 세로선이 서두 전체를 묶고, 라벨은
// 회색 대문자풍 굵은 글씨로 통일한다. 아이콘은 쓰지 않는다(대체 글꼴 사고
// 방지). 안에서는 얇은 규칙선으로 칸을 가른다.
// 서두·문답·실제 사례가 공유하는 왼쪽 세로선 (저자 지시 2026-08-06)
#let _side-rule = 2.2pt + rgb("#111111")

#let _open-rule = _side-rule
// 서두 라벨은 본문(명조)과 확실히 갈라 보이도록 고딕·크게·굵게 쓴다.
// 밑선은 두지 않는다 — 고딕 굵은 글씨만으로 충분히 구별된다
// (저자 지시 2026-08-06).
#let _open-label(t) = block(below: 9.5pt, width: 100%)[
  #text(
    font: ("Noto Sans CJK KR", "Noto Sans"),
    weight: "bold", size: 1.15em, fill: black, tracking: 0.01em, t,
  )
]
#let label-class(label) = {
  if label == _L.prereq { "prereq" } else if label == _L.back { "deepqa" } else if label == _L.why { "why" } else if label == _L.organizer { "organizer" } else if label == _L.questions { "questions" } else { "open-other" }
}

// `name:` 은 조판 견본 전용이다 --- 라벨은 lib 이 만들므로, 견본이 「이 칸을
// 뭐라고 부르는가」를 붙일 자리가 달리 없었다(저자 지적 2026-08-10).
// 책에서는 넘기지 않으므로 아무것도 달라지지 않는다.
#let _open-block(label, body, first: false, name: none) = {
let _label0 = label
let label = if name == none { label } else { [#label #stylename(name)] }
if _html {
  // 본문은 언제나 한 겹으로 감싼다 — 맨 글로 두면 여백 규칙이 걸리지 않아
  // 칸마다 위쪽 간격이 달라진다(「이 장이 끝나면」 사고, 2026-08-06).
  html.elem("div", attrs: (class: "dev open " + label-class(_label0)), {
    html.elem("p", attrs: (class: "dev-label"), label)
    html.elem("div", attrs: (class: "open-body"), body)
  })
} else {
  // 문답과 같은 꼴로 --- 표지 줄은 굵은 왼쪽 선, 내용은 가는 선, 둘 사이는
  // 떨어뜨린다. 칸과 칸 사이의 가로선은 두지 않는다(저자 지시 2026-08-10).
  block(width: 100%, above: if first { 1.1em } else { 0.55em },
        below: 0pt, breakable: false)[
    #set par(first-line-indent: (amount: 1em, all: true),
             leading: st.open-body-leading, spacing: st.open-body-spacing)
    #set block(spacing: st.open-body-spacing)
    #block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
           inset: (left: _rail-pad, top: st.open-label-top,
                   bottom: st.open-label-bottom),
           stroke: (left: _rail-head))[#_open-label(label)]
    #block(width: 100%, above: 0pt, below: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[#body]
  ]
}
}

// 3.1 문답 (즉문즉답) — 기본 리듬
// 3.1 문답 — 문과 답은 *하나의 덩어리*다. 왼쪽 세로선 하나가 둘을 잇고,
// 질문 줄은 굵은 고딕과 옅은 바탕으로 도드라지게 한다 (저자 지시 2026-08-06).
#let _qa_rail = _side-rule
#let _qa(label_q, label_a, q, a) = if _html {
  html.elem("div", attrs: (class: "qa-box"), {
    html.elem("div", attrs: (class: "dev qa-q"), {
      [#metadata(q)<qa-q>]
      html.elem("p", {
        html.elem("span", attrs: (class: "dev-label"), label_q + ".")
        [ ]
        q
      })
    })
    html.elem("div", attrs: (class: "dev qa-a"), {
      html.elem("p", {
        html.elem("span", attrs: (class: "dev-label"), label_a + ".")
        [ ]
        a
      })
    })
  })
} else {
  block(width: 100%, above: 1.25em, below: 1.25em, breakable: true)[
    #set par(first-line-indent: (amount: 1em, all: false))
    #block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
           inset: (left: _rail-pad, top: st.dev-label-top,
                   bottom: st.dev-label-bottom),
           stroke: (left: _rail-head))[
      #metadata(q)<qa-q>
      #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold", size: 1em)[
        #label_q. #h(2pt) #q]
    ]
    #block(width: 100%, above: 0pt, below: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[
      #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold",
            size: 0.98em, fill: rgb("#444444"))[#label_a.]
      #h(3pt) #a
    ]
  ]
}
#let qa(q, a) = _qa(_L.q, _L.a, q, a)

// 3.2 심화 문답 (장 서두 회고 전용)
// ② 인출 문답 — 선행 개념을 표시하는 데 그치지 않고 실제로 꺼내 보게 한다
#let deepqa(q, a, name: none) = _open-block(_L.back, name: name, if _html {
  // HTML: 질문 문단 + 답 칸. 표지 "답." 앞에는 들여쓰기가 붙지 않는다.
  {
    html.elem("p", q)
    html.elem("div", attrs: (class: "qa-a"),
      html.elem("p", {
        html.elem("span", attrs: (class: "dev-label"), _L.a + ".")
        [ ]
        a
      }))
  }
} else [
  #block(width: 100%)[#q]
  #block(width: 100%,
    inset: (left: 9pt, top: st.deepqa-a-pad, bottom: st.deepqa-a-pad),
    stroke: (left: 0.6pt + st.rail-body.paint))[
    #set par(first-line-indent: (amount: 1em, all: false))
    #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold",
          size: 0.98em, fill: rgb("#3a3a3a"))[#_L.a.] #h(3pt) #a
  ]
])

// 3.3 오개념 블록: 그럴듯한 생각 → 왜 그럴듯한가 → 실제로는 → 확인
// 3.3 오개념 — "그럴듯한 생각"이 한눈에 들어와야 교정이 일어난다.
// 제목(오개념 문장)을 인용부호와 함께 크게·굵게 세우고, 굵은 테두리로 감싼다.
#let misconception(title, body) = if _html {
  html.elem("div", attrs: (class: "dev misconception"), {
    html.elem("p", attrs: (class: "dev-label"), {
      _L.misc + ". "
      html.elem("strong", title)
    })
    html.elem("div", attrs: (class: "dev-body"), body)
  })
} else {
  block(width: 100%, above: 1.25em, below: 1.25em, breakable: true)[
    #set par(first-line-indent: 0em)
    #block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
           inset: (left: _rail-pad, top: st.dev-label-top,
                   bottom: st.dev-label-bottom),
           stroke: (left: _rail-head))[
      #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold",
            size: 1em, fill: black)[#_L.misc.]
      #h(4pt)
      #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold", size: 1.0em)[#title]
    ]
    #block(width: 100%, above: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[#_indent-body(body)]
  ]
}

// 3.4 실제 사례 블록
#let realcase(title, body) = _device(title, body, (left: _side-rule), _L.real)

// 반례 블록 (RFC-0004 §3): 독자의 생각이 아니라 *코드*가 틀린 경우.
// 오개념 블록(⚠)과 구별한다.
#let antipattern(title, body) = _device(title, body,
  (left: (thickness: 2.2pt, paint: black, dash: "dotted")), _L.anti)

// 3.5 수학 기반 박스 (건너뛰어도 본문이 이어지게 쓴다)
#let mathbox(title, body) = _device(title, body, (left: 1pt + black), _L.math)

// (선택) 복습 정리 — 허용되는 유일한 복습 형태 (R17)
#let recap(body) = _device(_L.recap, body, (left: _rail-head), none)

// 3.6 코드 시연: 소스와 "실제 실행 결과"를 함께 인쇄한다.
// 출력은 scripts/verify-examples.sh 가 남긴 캡처 파일에서 읽는다 (수작업 전사 금지, R15).
//
// 로우엔트 책의 예제 트리는 **하나**다(`examples/`) — 코드와 출력 문자열이 영어라 두 판이 같이 쓴다.
// 캡처는 scripts/verify-examples.sh 가 `build/examples-out/<장>/<이름>.low.out` 에 남긴다.
#let _out-dir(path) = "/build/examples-out/"
#let _rel(path) = path.replace("examples/", "")

// 실행 결과·표준 입력의 긴 줄은 강제로 접는다 — 조판에서 raw 는 스스로 줄을
// 바꾸지 않아서 긴 한 줄이 상자를 뚫고 나간다(95장의 깊이 200 출력이 그랬다).
// 글자 단위(클러스터)로 잘라 UTF-8 을 깨뜨리지 않는다.
// ★ 소스 코드에는 쓰지 않는다 — 낱말 한복판에서 잘리면 읽기 나쁘다.
//   대신 원고의 예제 줄 길이를 scripts/check-example-width.py 가 지킨다.
#let _wrap-cols = 88
// ★ 한 칸이 아니라 *폭*으로 센다(저자 지시 2026-08-11). 한글·한중일 글자는
//   고정폭 글꼴에서 라틴의 두 배를 차지한다. 글자 수로 세면 한글 주석이 든
//   줄이 상자를 뚫고 나간다 --- 실제로 예제 245줄이 그랬다.
#let _wide(c) = {
  let n = c.to-unicode()
  // ★ Typst 는 줄이 바뀌면 식이 끝난 것으로 본다 --- 여러 줄 논리식은
  //   괄호로 묶어야 한다(묶지 않아 `unexpected operator or` 로 멈췄다).
  ((n >= 0x1100 and n <= 0x115F) or (n >= 0x2E80 and n <= 0xA4CF)
   or (n >= 0xAC00 and n <= 0xD7A3) or (n >= 0xF900 and n <= 0xFAFF)
   or (n >= 0xFE30 and n <= 0xFE6F) or (n >= 0xFF00 and n <= 0xFF60)
   or (n >= 0xFFE0 and n <= 0xFFE6))
}

#let _hardwrap(s, width: _wrap-cols) = {
  let lines = ()
  for line in s.split("\n") {
    let cur = ()
    let w = 0
    for c in line.clusters() {
      let cw = if _wide(c) { 2 } else { 1 }
      if w + cw > width {
        lines.push(cur.join())
        cur = ()
        w = 0
      }
      cur.push(c)
      w = w + cw
    }
    lines.push(cur.join())
  }
  lines.join("\n")
}

// 실행 결과만 따로 내는 상자. 소스를 먼저 보이고 한참 뒤에 결과를 보이는
// 자리(15장의 첫 프로그램)에서 쓴다 — `demo(.., show-output: false)` 의 짝이다.
#let demo-output(path) = {
  let out = read(_out-dir(path) + _rel(path) + ".out")
  if _html {
    html.elem("div", attrs: (class: "demo demo-out"), {
      html.elem("p", attrs: (class: "demo-head"), _L.output)
      html.elem("div", attrs: (class: "demo-body"), raw(out, block: true))
    })
  } else {
    block(breakable: true, width: 100%,
          stroke: st.demo-rule, inset: 0pt)[
      #set par(first-line-indent: 0em)
      #block(width: 100%, inset: (x: 8pt, y: 6pt), above: 0pt, below: 0pt,
             stroke: (bottom: st.demo-rule))[
        #text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 0.96em,
              weight: "bold")[#_L.output]]
      #block(width: 100%, inset: (x: 8pt, y: 7pt), above: 0pt, below: 0pt)[
        #raw(_hardwrap(out), block: true)]
    ]
  }
}

// 도구가 낸 출력을 *그대로* 싣는 상자 (사설 부록 편입, 2026-08-31).
//
// ★ `demo` 와 무엇이 다른가 --- `demo` 는 *이 책이 돌린 예제*의 출력이라 매
//   빌드마다 다시 만들어진다. `capture` 는 `readelf`·`nm`·`objdump` 같은 *남의
//   도구*가 낸 것을 갈무리해 둔 것이다. 매 빌드마다 다시 만들 수 없으므로
//   (도구가 없는 기계도 있다) 파일로 두고 읽는다. 그래서 *언제 어디서 잡은
//   것인지*를 표제 줄에 적는 것이 이 장치의 규율이다.
#let capture(path, head) = {
  let body = read("/" + path)
  if _html {
    html.elem("div", attrs: (class: "demo demo-out"), {
      html.elem("p", attrs: (class: "demo-head"), head)
      html.elem("div", attrs: (class: "demo-body"), raw(body, block: true))
    })
  } else {
    block(breakable: true, width: 100%, stroke: st.demo-rule, inset: 0pt,
          above: 1.1em, below: 1.1em)[
      #set par(first-line-indent: 0em)
      #block(width: 100%, inset: (x: 8pt, y: 5pt), above: 0pt, below: 0pt,
             stroke: (bottom: st.demo-rule))[
        #text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 0.9em,
              weight: "bold")[#head]]
      #block(width: 100%, inset: (x: 8pt, y: 6pt), above: 0pt, below: 0pt)[
        #raw(_hardwrap(body), block: true)]
    ]
  }
}

#let demo(path, show-output: true, stdin: false, highlight: none, src: none) = {
  // 시연 상자는 1×2 다 — 표제 줄(파일 경로 또는 "실행 결과")과 내용을
  // 가로선으로 가른다 (저자 지시 2026-08-06). HTML 도 같은 모양으로 낸다.
  // ★ 스크립트로 도는 예제라도 지면에 싣는 것은 *C 원본*이다(저자 지적 2026-09-02).
  //   `src:` 로 실을 파일을 따로 준다 --- 독자가 읽어야 할 것은 빌드 절차가 아니라
  //   프로그램이다. 빌드 방법(최적화 수준 따위)이 논지에 걸리면 본문이 말로 밝힌다.
  //   파일이 여럿이라 스크립트 자체가 논지인 자리(여러 번역 단위를 잇는 예제)에서는
  //   `src:` 를 주지 않으면 되고, 그러면 예전처럼 스크립트가 실린다.
  let src-path = if src == none { path } else { src }
  let src = read("/" + src-path)
  let inp = if stdin { read("/" + path.replace(".low", ".in")) } else { none }
  let out = if show-output { read(_out-dir(path) + _rel(path) + ".out") } else { none }

  if _html {
    let cell(cls, head, body, lang: none) = html.elem(
      "div", attrs: (class: "demo " + cls), {
        html.elem("p", attrs: (class: "demo-head"), head)
        html.elem("div", attrs: (class: "demo-body"),
          if lang == none { raw(body, block: true) }
          else { raw(body, lang: lang, block: true) })
      })
    cell("demo-src", raw(src-path), src, lang: "lowent")
    if inp != none { cell("demo-in", _L.stdin, inp) }
    if out != none { cell("demo-out", _L.output, out) }
  } else {
    let cell(head, body) = block(
      width: 100%, breakable: true,
      stroke: st.demo-rule,   // 소스·입력·출력 모두 같은 가는 실선
      inset: 0pt,
    )[
      #set par(first-line-indent: 0em)
      #block(width: 100%, inset: (x: 8pt, y: 6pt), above: 0pt, below: 0pt,
             stroke: (bottom: st.demo-rule))[#head]
      #block(width: 100%, inset: (x: 8pt, y: 7pt), above: 0pt, below: 0pt)[#body]
    ]
    block(breakable: true, width: 100%)[
      #cell(text(size: 0.96em, weight: "bold", raw(src-path)),
            raw(_hardwrap(src), lang: "lowent", block: true))
      #if inp != none {
        cell(text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 0.96em,
                  weight: "bold")[#_L.stdin], raw(_hardwrap(inp), block: true))
      }
      #if out != none {
        cell(text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 0.96em,
                  weight: "bold")[#_L.output], raw(_hardwrap(out), block: true))
      }
    ]
  }
}

// 메모리 사물함 도해: 주소 라벨 + 내용 셀 (+ 강조 칸 인덱스)
//
// ★ HTML 로도 반드시 나가야 한다. grid·stack·box 는 조판 전용이라
//   HTML 내보내기에서 아무것도 그리지 않는다 — 3장의 그림들이 캡션만 남고
//   사라졌던 원인이다(저자 지적 2026-08-06). HTML 에서는 표로 낸다.
#let memrow(start, cells, highlight: (), caption: none, id: none) = {
  if _html {
    html.elem("div", attrs: (class: "memrow"), {
      html.elem("table", attrs: (class: "mem"), {
        html.elem("tr", {
          for (i, c) in cells.enumerate() {
            html.elem("td",
              attrs: (class: if i in highlight { "cell hi" } else { "cell" }),
              raw(c))
          }
        })
        html.elem("tr", {
          for (i, _) in cells.enumerate() {
            html.elem("td", attrs: (class: "addr"), raw(str(start + i)))
          }
        })
      })
    })
  } else {
    align(center, block(inset: (y: 6pt))[
      #grid(
        columns: cells.len(),
        column-gutter: 0pt,
        ..cells.enumerate().map(((i, c)) => {
          let w = if i in highlight { 1.6pt } else { 0.5pt }
          stack(
            box(width: 3.2em, inset: 4pt, stroke: w + black,
              align(center, raw(c))),
            box(width: 3.2em, inset: (top: 3pt),
              align(center, text(size: 0.96em, raw(str(start + i))))),
          )
        })
      )
    ])
  }
  _float-caption(_L.fig, _fig-no, caption, id: id)
}

// ── 표·그림 목록 (저자 지시 2026-08-16) ──────────────────
// 부록이 쓰는 장치. 번호도 제목도 쪽수도 사람이 적지 않는다 --- 본문에 이미
// 심어 둔 자리 표시(`<proven-float>`)를 훑어 만든다.
#let float-list(figures: false) = context {
  let want = if figures { _L.fig } else { _L.tbl }
  let hits = query(_float-key).filter(m => m.value.kind == want)
  if hits.len() == 0 { return }
  // ★ 항목마다 블록(또는 `<p>`)을 따로 뿜으면 그 사이가 한 줄씩 벌어진다.
  //   목록은 목록으로 --- 한 덩어리로 묶어 준다 (저자 지시 2026-08-31).
  if _html {
    html.elem("ul", attrs: (class: "float-list"), {
      for m in hits {
        let v = m.value
        let title = if v.cap == none { [] } else { v.cap }
        html.elem("li", link(m.location())[#v.kind #v.no] + [ — ] + title)
      }
    })
  } else {
    // 쪽 번호를 오른쪽에 세우려면 칸이 필요하다. 표 하나로 적으면 항목
    // 사이의 여백이 사라지고 번호·쪽수가 저절로 줄 맞는다.
    grid(
      columns: (4.6em, 1fr, 2.4em), column-gutter: 0.4em, row-gutter: 0.32em,
      ..hits.map(m => {
        let v = m.value
        let title = if v.cap == none { [] } else { v.cap }
        (text(weight: "bold", link(m.location())[#v.no]),
         title,
         align(right, text(fill: rgb("#555"),
           [#counter(page).at(m.location()).first()])))
      }).flatten()
    )
  }
}

// 플랫폼 의존 격리 절: 특정 OS/도구에 묶인 내용은 반드시 이 상자 안에 둔다.
// 본문 일반론은 이 상자를 건너뛰어도 성립해야 한다.
#let platform(title, body) = if _html {
  _html-box("platform", _L.platform + ". " + title, body)
} else {
  // 다른 장치와 똑같은 꼴로 --- 굵은 표지선 + 가는 본문선, 사이에 여백
  // (저자 지시 2026-08-10: 점선·겹선으로 따로 놀지 않게 한다)
  block(width: 100%, above: 1.05em, below: 1.05em, breakable: true)[
    #set par(first-line-indent: 0em)
    #block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
           inset: (left: _rail-pad, top: st.dev-label-top,
                   bottom: st.dev-label-bottom),
           stroke: (left: _rail-head))[
      #text(font: ("Noto Sans CJK KR", "Noto Sans"), weight: "bold",
            size: 0.98em, fill: black)[#_L.platform. #h(3pt)#title]
    ]
    #block(width: 100%, above: 0pt, below: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[#_indent-body(body)]
  ]
}

// ③ 이 장이 끝나면
// 3.6b 이 장의 필요성과 맥락 (저자 지시 2026-08-10) — 이 장이 왜 필요하고, 왜 책의
// 이 순서·이 자리에서 설명되는가. 바로 뒤의 「이 장이 끝나면」과 짝을 이룬다:
// 여기서 자리를 대고, 거기서 얻을 것을 약속한다.
#let why(body, name: none) = _open-block(_L.why, body, name: name)

#let organizer(body, name: none) = _open-block(_L.organizer, body, name: name)

// 서두를 닫는 굵은 규칙선 — 마지막 칸이 그린다
// 서두의 끝 --- 전에는 굵은 가로선을 그었으나, 이제 왼쪽 세로선이 영역을
// 말하므로 선 대신 숨만 둔다(저자 지시 2026-08-10).
#let _open-close = block(width: 100%, above: 0pt, below: 1.3em)[]

// 3.7 기댄 것 (RFC-0006 §3.1) — 이 장이 어느 장의 무슨 개념 위에 서는가.
// 번호만 쓰지 않고 개념 이름을 함께 적는다. 항목은 (참조, 개념) 쌍이다.
// ① 먼저 알아야 할 것 — 항목당 한 줄로 압축한다(재평가 §7.3: 서두가 길다)
#let prereq(..items) = _open-block(_L.prereq, first: true,
  name: items.named().at("name", default: none))[
  #set par(first-line-indent: 0em)
  #for (where, what) in items.pos() {
    block(width: 100%)[
      #text(weight: "bold")[#where]
      #h(5pt) #text(fill: rgb("#555555"))[·] #h(5pt)
      #text(fill: rgb("#333333"))[#what]
    ]
  }
]

// 3.8 이 장에서 답할 질문 (RFC-0006 §3.3) — 목록을 손으로 적지 않는다.
// 이 자리 뒤부터 다음 1단계 제목 전까지의 문답(qa)에서 질문만 모은다.
// 답은 싣지 않는다 — 독자가 잠시 생각할 자리를 만드는 것이 목적이다.
// 3.8b 이 장의 차례 (저자 지시 2026-08-10) — 장 제목 바로 밑, 서두 상자 앞.
// 이 장의 절(2단계)과 그 아래 항(3단계)을 쪽 번호와 함께 늘어놓는다. 긴 장에서
// 「무엇이 어디 있는가」를 먼저 보여 주려는 것이다.
//
// ★ HTML 에서는 내지 않는다 — 웹 판에는 상단 바의 「이 장의 차례」 패널과
//   상세 차례 페이지가 따로 있어서 같은 것이 두 번 나오게 된다.
#let chapter-toc(min: 2) = context {
  if _html { return }
  let nexts = query(heading.where(level: 1).after(here()))
  let sel = heading.where(level: 2).or(heading.where(level: 3)).after(here())
  let sel = if nexts.len() > 0 { sel.before(nexts.first().location()) } else { sel }
  let hs = query(sel)
  if hs.len() < min { return }
  // ★ 「이 장의 차례」도 다른 장치와 같은 꼴이다(저자 지시 2026-08-10) --- 표지
  //   줄은 굵은 세로선 위에 서두 라벨과 같은 글씨로, 내용은 가는 세로선 위에
  //   본문과 같은 크기·행간으로. 예전에는 이 칸만 한 덩어리였다.
  block(width: 100%, above: 1.1em, below: 1.2em, breakable: true)[
    #set par(first-line-indent: 0em, leading: st.body-leading, spacing: st.body-leading)
    #block(width: 100%, above: 0pt, below: _rail-gap, sticky: true,
           inset: (left: _rail-pad, top: st.open-label-top,
                   bottom: st.open-label-bottom),
           stroke: (left: _rail-head))[#_open-label(_L.chtoc)]
    #block(width: 100%, above: 0pt, below: 0pt,
           inset: (left: _rail-pad, top: st.body-pad-y, bottom: st.body-pad-y),
           stroke: (left: _rail-body))[
    #for h in hs {
      let no = numbering("1.1", ..counter(heading).at(h.location()))
      let lvl2 = h.level == 2
      block(width: 100%, above: st.chtoc-row-gap, below: 0pt,
            inset: (left: if lvl2 { 0pt } else { 12pt }))[
        // 본문과 같은 크기·같은 색 (저자 지시 2026-08-10) --- 점선만 흐리게
        #text(size: 1em, fill: black)[
          #box(width: 34pt)[#no]
          #h.body
          #box(width: 1fr, repeat(text(fill: st.toc-dot)[.]))
          #counter(page).at(h.location()).first()
        ]
      ]
    }
    ]
  ]
}

#let chapter-questions(min: 1, name: none) = context {
  let nexts = query(heading.where(level: 1).after(here()))
  let sel = selector(<qa-q>).after(here())
  let sel = if nexts.len() > 0 { sel.before(nexts.first().location()) } else { sel }
  let qs = query(sel)
  if qs.len() < min {
    // 질문이 없으면 서두를 여기서 닫는다
    _open-close
  } else {
    _open-block(_L.questions, name: name, if _html {
      // HTML 에서도 번호가 보이도록 진짜 목록으로 낸다 (저자 지시 2026-08-06)
      html.elem("ol", attrs: (class: "qlist"), {
        for m in qs { html.elem("li", m.value) }
      })
    } else [
      #set par(first-line-indent: 0em, leading: st.open-body-leading,
               spacing: st.open-body-spacing)
      #for (i, m) in qs.enumerate() {
        block(width: 100%, inset: (left: 14pt))[
          #place(left, dx: -14pt, text(fill: rgb("#777777"), weight: "bold")[#(i + 1)])
          #m.value
        ]
      }
    ])
    _open-close
  }
}

// 3.9 개념도 — 공간 관계가 본질인 자리에만 쓴다(생성기: scripts/make-figures.py).
// 그림 파일은 판마다 따로다: book/figures/{ko,en}/<이름>.svg
#let figure-svg(name, caption: none, id: auto, width: 100%) = {
  let id = if id == auto { name } else { id }
  if _html {
    // Typst 의 HTML 내보내기는 이미지를 아직 내지 않는다 — 직접 <figure> 를 낸다.
    // 그림 파일은 wrap-html.py 가 docs/<판>/figures/ 로 복사한다.
    html.elem("figure", attrs: (class: "fig"), {
      html.elem("img", attrs: (src: "figures/" + name + ".svg", alt: name, loading: "lazy"))
      _float-caption(_L.fig, _fig-no, caption, id: id)
    })
  } else {
    block(width: 100%, above: 1.3em, below: 1.3em, breakable: false)[
      #align(center, image("/book/figures/" + _lang + "/" + name + ".svg", width: width))
      #_float-caption(_L.fig, _fig-no, caption, id: id)
    ]
  }
}

// ── 이름표–값 목록 (판권의 지은이·연락·저장소 …) ─────
// ★ grid 는 조판 전용이라 HTML 에서 통째로 사라진다(memrow 와 같은 함정).
//   여기서는 HTML 이면 <dl> 로 낸다.
#let metalist(..pairs) = {
  let items = pairs.pos()
  if _html {
    html.elem("dl", attrs: (class: "metalist"), {
      for (k, v) in items {
        html.elem("dt", k)
        html.elem("dd", v)
      }
    })
  } else {
    grid(
      columns: (auto, 1fr),
      row-gutter: 0.62em,
      column-gutter: 0.9em,
      ..items.map(((k, v)) => (text(fill: rgb("#555555"))[#k], v)).flatten(),
    )
  }
}

// ── 색인 ─────────────────────────────────────────────
// #idx("용어") 를 본문에 두면 그 자리의 쪽 번호가 색인에 실린다.
// 화면에는 아무것도 그리지 않는다(metadata).
#let idx(term) = [#metadata(term)<idx-entry>]

// 책 끝에서 호출한다. 모든 표시를 모아 가나다순으로 정리한다.
#let make-index(pages: true) = context {
  let entries = query(<idx-entry>)
  let terms = ()
  let hits = ()          // 표제어별 (쪽번호, 위치) 목록
  for e in entries {
    let term = e.value
    let loc = e.location()
    let p = if pages { counter(page).at(loc).first() } else { 0 }
    let i = terms.position(x => x == term)
    if i == none {
      terms.push(term)
      hits.push(((page: p, loc: loc),))
    } else if not pages or hits.at(i).filter(h => h.page == p).len() == 0 {
      hits.at(i).push((page: p, loc: loc))
    }
  }
  if not pages {
    // HTML 판: 레이아웃 함수(columns/grid)는 HTML 내보내기에서 버려지므로
    // 단순 목록으로 낸다. 표제어를 누르면 본문의 그 자리로 간다.
    return list(..terms.sorted().map(t => {
      let hs = hits.at(terms.position(x => x == t))
      link(hs.first().loc, t)
    }))
  }
  set par(justify: false, first-line-indent: 0em, leading: 0.62em)
  set text(size: 1.0em)
  // 쪽 번호를 누르면 본문의 그 자리로 간다.
  // (2026-08-05: 태그 PDF 가 링크마다 쪽을 늘린다고 보고 `--no-pdf-tags` 를
  //  썼으나, 재측정 결과 사실이 아니었다 — 태그판과 무태그판의 쪽 수는
  //  417 쪽으로 같다. 상류 이슈 typst/typst#8722 참고. 태그를 다시 켠다.)
  columns(2, gutter: 1.4em, {
    for t in terms.sorted() {
      let hs = hits.at(terms.position(x => x == t))
      // 표제어와 쪽 번호를 점선으로 잇는다 — 차례와 같은 모양
      // (저자 지시 2026-08-06). repeat 은 남는 자리를 점으로 채운다.
      block(width: 100%, below: 0.42em)[
        #t
        #box(width: 1fr, inset: (x: 0.4em),
          text(fill: rgb("#888888"), tracking: 0.3em, repeat[.]))
        #hs.map(h => link(h.loc, str(h.page))).join(", ")
      ]
    }
  })
}

// ── 표제면 ───────────────────────────────────────────
// ★ 두 판(한국어·영어)과 조판 견본이 같은 표제면을 쓴다. 예전에는 main.typ
//   안에 통째로 적혀 있어, 견본에서 표지를 볼 방법이 없었다.
// ★ 웹판에서는 wrap-html.py 가 index.html 의 표지를 직접 짓는다. 견본이
//   그 모양을 그대로 보이려면 *같은 클래스*를 내야 한다 --- 그렇지 않으면
//   견본의 표지만 아무 서식 없이 밋밋하게 나온다.
#let cover-body(title: "", subtitle: "", status: "", version: "",
                updated: "", author: "", contact: none, repo: none,
                blurb: none) = if _html {
  html.elem("header", attrs: (class: "cover"), {
    html.elem("h1", title)
    html.elem("p", attrs: (class: "cover-sub"), subtitle)
    html.elem("p", html.elem("span", attrs: (class: "badge"), status))
    html.elem("p", attrs: (class: "cover-meta"), [#version · #updated])
    html.elem("p", attrs: (class: "cover-author"), author)
    if contact != none or repo != none {
      html.elem("p", attrs: (class: "cover-links"), [#contact #repo])
    }
    if blurb != none {
      html.elem("div", attrs: (class: "cover-blurb"), blurb)
    }
  })
} else [
  #v(3.2cm)
  #align(center)[
    #text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 26pt, weight: "bold")[#title]
    #v(0.6cm)
    #text(size: 13pt)[#subtitle]
    #v(0.9cm)
    #box(inset: (x: 10pt, y: 5pt), stroke: 1pt + black)[
      #text(size: 10pt, weight: "bold")[#status]
    ]
    #v(0.5cm)
    #text(size: 10.5pt)[#version #sym.dot.c #updated]
    #v(1.0cm)
    #text(size: 11pt)[#author]
    #v(0.25cm)
    #text(size: 10pt)[#contact #h(0.8em) #repo]
  ]
  #if blurb != none [
    #v(1.4cm)
    #align(center, block(width: 76%)[
      #set text(size: 10.5pt)
      #set par(justify: false, first-line-indent: 0em, leading: 0.85em)
      #align(center)[#blurb]
    ])
  ]
]

// ── 차례의 한 줄 ─────────────────────────────────────
// 값은 style.typ 에서 오고, 모양은 여기 하나뿐이다 --- 앞차례(main.typ)와
// 조판 견본이 같은 것을 쓴다.
// 웹판에서는 차례가 <a> 의 목록이다(쪽 번호가 없다) --- wrap-html.py 가 짓는
// index.html 과 같은 뼈대를 견본에서도 보이려고 클래스를 맞춘다.
#let toc-block(body) = if _html {
  html.elem("div", attrs: (class: "toc"), body)
} else { block(width: 100%, body) }

#let toc-group(body) = if _html {
  html.elem("div", attrs: (class: "toc-group"), body)
} else { body }

#let toc-row(label, pageno, dest: none) = if _html {
  html.elem("a", label)
} else { block(
  width: 100%, inset: (left: 1.2em),
  above: st.toc-row-above, below: st.toc-row-below)[
  #if dest != none { link(dest)[#label] } else { label }
  #box(width: 1fr, inset: (x: 0.5em),
    text(fill: rgb("#888888"), tracking: 0.35em, repeat[.]))
  #if dest != none { link(dest)[#pageno] } else { pageno }
] }

#let toc-subrow(no, title, pageno, dest: none) = if _html {
  html.elem("a", [#no #title])
} else { block(
  width: 100%, inset: (left: 3.0em),
  above: st.toc-sub-gap, below: st.toc-sub-gap)[
  #set text(size: 1em)
  #if dest != none { link(dest)[#no #title] } else [#no #title]
  #box(width: 1fr, inset: (x: 0.4em),
    text(fill: st.toc-dot, tracking: 0.35em, repeat[.]))
  #if dest != none { link(dest)[#pageno] } else { pageno }
] }

#let toc-part(title) = if _html {
  html.elem("h4", attrs: (class: "toc-part"), title)
} else {
  block(above: 1.5em, below: 0.7em, sticky: true)[
    #text(font: ("Noto Sans CJK KR", "Noto Sans"), size: 10.5pt,
          weight: "bold", title)
  ]
}

// ── 모드 인지 표 ─────────────────────────────────────
// PDF 에서는 Typst table, HTML 에서는 진짜 <table> 로 나간다.
#let dtable(columns: 2, caption: none, id: none, keycol: auto, ..cells) = {
  let items = cells.pos()
  if sys.inputs.at("mode", default: "paged") == "html" {
    let rows = ()
    let i = 0
    while i < items.len() {
      rows.push(items.slice(i, calc.min(i + columns, items.len())))
      i = i + columns
    }
    html.elem("figure", attrs: (class: "tbl"), {
      html.elem("table", {
        let first = true
        for r in rows {
          let tag = if first { "th" } else { "td" }
          html.elem("tr", { for c in r { html.elem(tag, c) } })
          first = false
        }
      })
      _float-caption(_L.tbl, _tbl-no, caption, id: id)
    })
  } else {
    // 굵은 테두리는 표를 감싸는 블록이 아니라 *표 자신의 바깥 선*이어야
    // 한다. 블록으로 감싸면 테두리만 단 너비를 차지하고 표는 제 너비만
    // 차지해 둘이 따로 논다 (저자 지시 2026-08-06).
    //
    // 머리행(1행)은 산세리프 굵은 글씨 + 옅은 음영으로 포인트를 준다.
    // 열이 셋 이상이면 1열도 키 노릇을 하므로 같은 처리를 한다
    // (`keycol: true|false` 로 강제할 수 있다).
    let rows = calc.ceil(items.len() / columns)
    let thick = st.tbl-outer + st.tbl-outer-fill
    let thin = st.tbl-inner + st.tbl-inner-fill
    let key = if keycol == auto { columns >= 3 } else { keycol }
    let sans = ("Noto Sans CJK KR", "Noto Sans")
    align(center, table(
      columns: columns,
      inset: 5pt,
      fill: (col, row) => if row == 0 { rgb("#ececec") }
        else if key and col == 0 { rgb("#f5f5f5") } else { none },
      stroke: (x, y) => (
        left: if x == 0 { thick } else { thin },
        right: if x == columns - 1 { thick } else { none },
        top: if y == 0 { thick } else if y == 1 { st.tbl-head + st.tbl-outer-fill } else { thin },
        bottom: if y == rows - 1 { thick } else { none },
      ),
      ..items.enumerate().map(((i, c)) => if i < columns {
        text(font: sans, weight: "bold", c)
      } else if key and calc.rem(i, columns) == 0 {
        text(font: sans, weight: "bold", size: 0.98em, c)
      } else { c }),
    ))
    _float-caption(_L.tbl, _tbl-no, caption, id: id)
  }
}

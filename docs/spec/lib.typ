// 로우엔트(Lowent) 표준 명세 — 장치 층 (RFC-0097)
//
// ★ `proven_c_book/book/lib.typ` 에서 구조를 차용했다. 그 책은 장치가 956 줄이지만
//   여기서는 **명세에 필요한 것만** 남긴다: 조항 번호 · 용어 병기 · 규범/참고 · 예제 · 표.
//
// ★★ 한 원본에서 **PDF 와 HTML** 을 낸다. HTML 은 쪽 레이아웃을 통째로 무시하므로
//    장치 안에서 갈래를 나눈다(`_html`).

#let _html = sys.inputs.at("mode", default: "paged") == "html"
#let spec-version = sys.inputs.at("specver", default: "v0.0.0")
#let lang-version = sys.inputs.at("langver", default: "?")
#let built-on = sys.inputs.at("date", default: "?")

// ── 조항 번호 ────────────────────────────────────────────────────────────────
// C 표준처럼 **번호가 주소**다. 한 번 발급한 번호의 뜻은 바꾸지 않는다(RFC-0097 D6).
// 번호는 손으로 적는다 — 자동 번호는 조항이 끼어들 때 **주소를 바꿔 버리기** 때문이다.
// ★ 제목과 캡션은 **문자열**로 들어온다. 그 안의 역따옴표(`` `if` ``)와 `**굵게**` 를 읽어 조판한다 —
//   전에는 글자 그대로 찍혀서 제목에 역따옴표와 별표가 보였다(2026-10-09).
#let _inl(s) = {
  let parts = s.split("`")
  for (i, p) in parts.enumerate() {
    if calc.odd(i) { raw(p) } else {
      let bs = p.split("**")
      for (j, b) in bs.enumerate() { if calc.odd(j) { strong(b) } else { b } }
    }
  }
}

// ★★ PDF 에서 조항 제목은 **진짜 제목(heading)** 이다(2026-10-09). 전에는 굵은 글씨 문단이어서 차례(`#outline`)가
//   비어 있었고 PDF 책갈피도 없었다. 번호는 여전히 손으로 적는다 — 제목의 수준만 번호의 마디 수에서 읽는다.
//   이름표(`c6.5.2`)를 붙여 `cref` 가 그 자리로 건너가게 한다.
#let _lvl(no) = calc.min(no.split(".").len(), 4)
#let _head(no, title) = [#heading(level: _lvl(no), numbering: none, outlined: true)[#no #h(0.55em) #_inl(title)] #label("c" + no)]

#let clause(no, title, body) = {
  if _html {
    html.elem("section", attrs: (class: "clause", id: "c" + no), {
      html.elem("h2", [#no #h(0.6em) #_inl(title)])
      body
    })
  } else {
    pagebreak(weak: true)
    _head(no, title)
    body
  }
}

#let sub(no, title, body) = {
  if _html {
    html.elem("section", attrs: (class: "subclause", id: "c" + no), {
      html.elem("h3", [#no #h(0.5em) #_inl(title)])
      body
    })
  } else {
    _head(no, title)
    body
  }
}

#let para(no, body) = if _html {
  html.elem("p", attrs: (class: "para", id: "p" + no), {
    html.elem("span", attrs: (class: "pno"), [#no])
    body
  })
} else {
  block(above: 0.55em, below: 0.55em)[
    #box(width: 2.4em)[#text(size: 0.85em, fill: rgb("#777"))[#no]]#body
  ]
}

#let cref(no) = if _html {
  html.elem("a", attrs: (href: "#c" + no, class: "cref"), [§#no])
} else {
  // PDF 에서도 그 조항으로 건너간다. 이름표가 없는 번호(아직 없는 조항)는 글자로만 둔다.
  context {
    let hit = query(label("c" + no))
    if hit.len() > 0 { link(hit.first().location(), text(fill: rgb("#1a4f8a"))[§#no]) }
    else { text(fill: rgb("#333"))[§#no] }
  }
}

// ── 용어 병기 (D5) ───────────────────────────────────────────────────────────
// **모든 용어는 처음 나올 때 괄호로 영어를 병기하고 뜻을 적는다.**
#let term(ko, en, body) = if _html {
  html.elem("dl", attrs: (class: "term"), {
    html.elem("dt", [#ko (#en)])
    html.elem("dd", body)
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: (left: 0.8em),
        stroke: (left: 2pt + rgb("#111")))[
    #text(weight: "bold")[#ko] #text(fill: rgb("#555"))[(#en)]

    #body
  ]
}

// 본문에서 용어를 처음 쓸 때: #t("계약", "contract")
// ★ «자유 선택 (free choice)» 은 일반 낱말이 아니라 §4.3 이 정의한 용어다 — 본문에서 쓰일 때마다 눈에 띄게 낸다(2026-10-08 소유자).
#let t(ko, en) = if en == "free choice" {
  if _html { html.elem("span", attrs: (class: "t fc"), [#ko (#en)]) }
  else { text(weight: "bold", fill: rgb("#17574a"))[#ko (#text(size: 0.92em)[#en])] }
} else if _html {
  html.elem("span", attrs: (class: "t"), [#ko (#en)])
} else { [#ko (#text(fill: rgb("#555"), size: 0.92em)[#en])] }

// ── 규범과 참고를 가른다 (D7) ────────────────────────────────────────────────
#let note(body) = if _html {
  html.elem("div", attrs: (class: "note"), {
    html.elem("b", [참고 (informative)])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, fill: rgb("#f4f4f4"))[
    #text(size: 0.88em, weight: "bold")[참고 (informative)]

    #text(size: 0.95em)[#body]
  ]
}

#let plain(body) = if _html {
  html.elem("div", attrs: (class: "plain"), {
    html.elem("b", [쉬운 말로])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em,
        stroke: (left: 2.5pt + rgb("#666")))[
    #text(size: 0.88em, weight: "bold", fill: rgb("#444"))[쉬운 말로]

    #text(size: 0.95em)[#body]
  ]
}

// 자유 선택 — 처리기가 범위 안에서 골라도 되고, 무엇을 골랐는지 적지 않아도 되는 자리 (정본 §4.3). 부록 F 가 모은다.
#let freechoice(title, body) = if _html {
  html.elem("div", attrs: (class: "freechoice"), {
    html.elem("b", [자유 선택 (free choice) — #_inl(title)])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: (left: 3pt + rgb("#1f6f5c"), rest: 0.5pt + rgb("#1f6f5c")))[
    #text(size: 0.88em, weight: "bold", fill: rgb("#17574a"))[자유 선택 (free choice) — #_inl(title)]

    #text(size: 0.95em)[#body]
  ]
}

// 모호 — 이 문서만으로는 뜻이 둘 이상으로 읽히는 자리 (RFC-0139 §6). 고르지 않고 풀이를 나란히 적는다. 부록 E 가 모은다.
#let ambig(title, body) = if _html {
  html.elem("div", attrs: (class: "ambig"), {
    html.elem("b", [모호 (open issue) — #_inl(title)])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: (left: 3pt + rgb("#a05a00"), rest: 0.5pt + rgb("#a05a00")))[
    #text(size: 0.88em, weight: "bold", fill: rgb("#7a4300"))[모호 (open issue) — #_inl(title)]

    #text(size: 0.95em)[#body]
  ]
}

#let caution(title, body) = if _html {
  html.elem("div", attrs: (class: "caution"), {
    html.elem("b", [주의 — #_inl(title)])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: 1pt + rgb("#111"))[
    #text(size: 0.88em, weight: "bold")[주의 — #_inl(title)]

    #text(size: 0.95em)[#body]
  ]
}

// ── 예제 ────────────────────────────────────────────────────────────────────
// ★ 명세의 예제는 **실제로 컴파일된다**(`check-spec.py --code`).
#let ex(caption, code, out: none) = {
  if _html {
    html.elem("div", attrs: (class: "ex"), {
      html.elem("b", [예제 (example) — #_inl(caption)])
      html.elem("pre", html.elem("code", code))
      if out != none { html.elem("pre", attrs: (class: "out"), out) }
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[예제 (example) — #_inl(caption)]

      #block(width: 100%, inset: 0.6em, fill: rgb("#fafafa"), stroke: 0.5pt + rgb("#ccc"))[
        #raw(code)
      ]
      #if out != none [
        #block(width: 100%, inset: 0.6em, fill: rgb("#f0f0f0"))[
          #text(size: 0.82em)[결과 (output)]

          #raw(out)
        ]
      ]
    ]
  }
}

// ── 표 (번호는 장치가 붙인다 — 손으로 안 적는다) ─────────────────────────────
#let _tbl-no = counter("spec-table")
#let tbl(caption, body) = {
  _tbl-no.step()
  if _html {
    html.elem("div", attrs: (class: "tbl"), {
      html.elem("b", context [표 #_tbl-no.display() — #_inl(caption)])
      body
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #context text(size: 0.88em, weight: "bold")[표 #_tbl-no.display() — #_inl(caption)]

      #body
    ]
  }
}

// ── 문법 틀 ─────────────────────────────────────────────────────────────────
// ★ `#ex` 와 **갈라 둔다**: 예제는 실제로 컴파일되는 프로그램이고, 틀은 `<…>` 자리를 가진
//   **모양**이다. 갈라 놓지 않으면 검사기가 틀을 프로그램으로 알고 컴파일하려 든다
//   (실제로 그렇게 걸렸다 — 검사기가 옳았고 내 장치 선택이 틀렸다).
// ── 조항의 틀 (RFC-0139 §5.1 · §5.2, 정본 §1.5) ─────────────────────
// 칸 제목 — 구문 · 제약 · 정적 의미 · 동적 의미 · 진단 · 예제 · 참고. 조항 번호가 없고 차례에 들어가지 않는다.
#let part(name) = if _html {
  html.elem("p", attrs: (class: "part"), name)
} else {
  block(above: 1.1em, below: 0.5em)[#text(size: 0.92em, weight: "bold", fill: rgb("#333"))[#name]]
}
// 구문 — EBNF 생성 규칙. 문법틀(#shape)은 한눈에 보는 모양이고, 이것이 문법의 정의다.
#let syntax(caption, code) = {
  if _html {
    html.elem("div", attrs: (class: "syntax"), {
      html.elem("pre", html.elem("code", code))
    })
  } else {
    block(above: 0.7em, below: 0.9em, width: 100%, inset: (left: 0.9em, y: 0.5em), stroke: (left: 2pt + rgb("#111")))[
      #set text(font: ("D2Coding", "Noto Sans Mono"), size: 0.9em)
      #raw(code)
    ]
  }
}

#let shape(caption, code) = {
  if _html {
    html.elem("div", attrs: (class: "shape"), {
      html.elem("b", [문법 틀 (grammar shape) — #_inl(caption)])
      html.elem("pre", html.elem("code", code))
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[문법 틀 (grammar shape) — #_inl(caption)]

      #block(width: 100%, inset: 0.6em, fill: rgb("#f7f7f7"), stroke: (dash: "dashed", paint: rgb("#bbb"), thickness: 0.5pt))[
        #raw(code)
      ]
    ]
  }
}

// ── 도해 ────────────────────────────────────────────────────────────────────
// ★ 2026-09-25 — 규칙을 **그림으로** 보이는 자리. 문법 틀(`<…>` 자리를 가진 모양)과도, 예제(컴파일되는
//   프로그램)와도 다르다: 이것은 규범이 아니라 규범을 읽기 쉽게 하는 그림이고, 검사기는 코드로 보지 않는다.
#let diagram(caption, code) = {
  if _html {
    html.elem("div", attrs: (class: "diagram"), {
      html.elem("b", [도해 (diagram) — #_inl(caption)])
      html.elem("pre", html.elem("code", code))
    })
  } else {
    block(above: 1em, below: 1em, width: 100%, breakable: false)[
      #text(size: 0.88em, weight: "bold")[도해 (diagram) — #_inl(caption)]

      #block(width: 100%, inset: 0.6em, fill: rgb("#f5f8fb"), stroke: 0.5pt + rgb("#9ab"))[
        #raw(code)
      ]
    ]
  }
}

// ── 거부되는 예제 ───────────────────────────────────────────────────────────
// ★ 명세는 **되는 것**만 보이면 절반만 말한 것이다. *"이건 왜 안 되는가"* 가 규칙을
//   가장 또렷하게 가르친다. 이 장치는 그 자리를 따로 표시하고, 어떤 진단이 나오는지 적는다.
//   (컴파일 검사기는 이 블록을 **컴파일하지 않는다** — 거부되는 것이 정상이기 때문이다.)
#let rejected(caption, code, diag) = {
  if _html {
    html.elem("div", attrs: (class: "rejected"), {
      html.elem("b", [거부되는 예제 (rejected) — #_inl(caption)])
      html.elem("pre", html.elem("code", code))
      html.elem("p", attrs: (class: "diag"), [진단: #raw(diag)])
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[거부되는 예제 (rejected) — #_inl(caption)]

      #block(width: 100%, inset: 0.6em, fill: rgb("#faf6f6"), stroke: 0.5pt + rgb("#c99"))[
        #raw(code)
      ]
      #text(size: 0.85em)[진단: #raw(diag)]
    ]
  }
}

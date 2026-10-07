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
#let clause(no, title, body) = {
  if _html {
    html.elem("section", attrs: (class: "clause", id: "c" + no), {
      html.elem("h2", [#no #h(0.6em) #title])
      body
    })
  } else {
    pagebreak(weak: true)
    block(above: 0em, below: 1em)[#text(weight: "bold", size: 1.4em)[#no #h(0.6em) #title]]
    body
  }
}

#let sub(no, title, body) = {
  if _html {
    html.elem("section", attrs: (class: "subclause", id: "c" + no), {
      html.elem("h3", [#no #h(0.5em) #title])
      body
    })
  } else {
    block(above: 1.4em, below: 0.6em)[#text(weight: "bold", size: 1.1em)[#no #h(0.5em) #title]]
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
} else { text(fill: rgb("#333"))[§#no] }

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
    html.elem("b", [자유 선택 (free choice) — #title])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: (left: 3pt + rgb("#1f6f5c"), rest: 0.5pt + rgb("#1f6f5c")))[
    #text(size: 0.88em, weight: "bold", fill: rgb("#17574a"))[자유 선택 (free choice) — #title]

    #text(size: 0.95em)[#body]
  ]
}

// 모호 — 이 문서만으로는 뜻이 둘 이상으로 읽히는 자리 (RFC-0139 §6). 고르지 않고 풀이를 나란히 적는다. 부록 E 가 모은다.
#let ambig(title, body) = if _html {
  html.elem("div", attrs: (class: "ambig"), {
    html.elem("b", [모호 (open issue) — #title])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: (left: 3pt + rgb("#a05a00"), rest: 0.5pt + rgb("#a05a00")))[
    #text(size: 0.88em, weight: "bold", fill: rgb("#7a4300"))[모호 (open issue) — #title]

    #text(size: 0.95em)[#body]
  ]
}

#let caution(title, body) = if _html {
  html.elem("div", attrs: (class: "caution"), {
    html.elem("b", [주의 — #title])
    body
  })
} else {
  block(above: 0.9em, below: 0.9em, width: 100%, inset: 0.7em, stroke: 1pt + rgb("#111"))[
    #text(size: 0.88em, weight: "bold")[주의 — #title]

    #text(size: 0.95em)[#body]
  ]
}

// ── 예제 ────────────────────────────────────────────────────────────────────
// ★ 명세의 예제는 **실제로 컴파일된다**(`check-spec.py --code`).
#let ex(caption, code, out: none) = {
  if _html {
    html.elem("div", attrs: (class: "ex"), {
      html.elem("b", [예제 (example) — #caption])
      html.elem("pre", html.elem("code", code))
      if out != none { html.elem("pre", attrs: (class: "out"), out) }
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[예제 (example) — #caption]

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
      html.elem("b", context [표 #_tbl-no.display() — #caption])
      body
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #context text(size: 0.88em, weight: "bold")[표 #_tbl-no.display() — #caption]

      #body
    ]
  }
}

// ── 문법 틀 ─────────────────────────────────────────────────────────────────
// ★ `#ex` 와 **갈라 둔다**: 예제는 실제로 컴파일되는 프로그램이고, 틀은 `<…>` 자리를 가진
//   **모양**이다. 갈라 놓지 않으면 검사기가 틀을 프로그램으로 알고 컴파일하려 든다
//   (실제로 그렇게 걸렸다 — 검사기가 옳았고 내 장치 선택이 틀렸다).
#let shape(caption, code) = {
  if _html {
    html.elem("div", attrs: (class: "shape"), {
      html.elem("b", [문법 틀 (grammar shape) — #caption])
      html.elem("pre", html.elem("code", code))
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[문법 틀 (grammar shape) — #caption]

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
      html.elem("b", [도해 (diagram) — #caption])
      html.elem("pre", html.elem("code", code))
    })
  } else {
    block(above: 1em, below: 1em, width: 100%, breakable: false)[
      #text(size: 0.88em, weight: "bold")[도해 (diagram) — #caption]

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
      html.elem("b", [거부되는 예제 (rejected) — #caption])
      html.elem("pre", html.elem("code", code))
      html.elem("p", attrs: (class: "diag"), [진단: #raw(diag)])
    })
  } else {
    block(above: 1em, below: 1em, width: 100%)[
      #text(size: 0.88em, weight: "bold")[거부되는 예제 (rejected) — #caption]

      #block(width: 100%, inset: 0.6em, fill: rgb("#faf6f6"), stroke: 0.5pt + rgb("#c99"))[
        #raw(code)
      ]
      #text(size: 0.85em)[진단: #raw(diag)]
    ]
  }
}

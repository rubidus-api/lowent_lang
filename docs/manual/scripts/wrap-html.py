#!/usr/bin/env python3
"""Typst 의 실험적 HTML 출력을 장별 페이지로 나누고 읽기 좋게 꾸민다."""
import sys, re, json, pathlib, html as htmlmod

lang, src, out_dir = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
raw = pathlib.Path(src).read_text(encoding="utf-8")
m = re.search(r"<body[^>]*>(.*)</body>", raw, re.S)
body = m.group(1) if m else raw

STR = {
 "ko": dict(title="Lowent 매뉴얼 — 계약과 효과로 짜는 시스템 프로그래밍",
   main="Lowent 매뉴얼", sub="계약과 효과로 짜는 시스템 프로그래밍",
   updated="최종 수정", author="rubidus",
   blurb=["이 책은 로우엔트 언어의 입문서이자 사용 설명서입니다.",
          "대상 독자는 프로그래밍을 조금 해 본 사람부터,<br>C·Rust 같은 시스템 언어를 쓰는 사람까지입니다."],
   colophon="저작권과 연락처", lbl_author="지은이", lbl_contact="연락",
   col_text=("<p><strong>본문</strong> — 크리에이티브 커먼즈 저작자표시-비영리-"
     "동일조건변경허락 4.0 국제 라이선스(CC BY-NC-SA 4.0). 출처를 밝히면 자유롭게 "
     "공유하고 고칠 수 있으나, 영리 목적 이용은 허용되지 않으며, 고친 결과물에는 "
     "같은 라이선스를 적용해야 합니다.<br>"
     "<a href=\"https://creativecommons.org/licenses/by-nc-sa/4.0/\">"
     "creativecommons.org/licenses/by-nc-sa/4.0/</a></p>"
     "<p><strong>예제 코드</strong> — MIT 라이선스. 자유롭게 가져다 쓰실 수 있습니다. "
     "예제가 쓰는 로우엔트 컴파일러와 표준 라이브러리는 저장소의 MIT 라이선스를 따릅니다.</p>"
     "<p>이 책의 모든 코드 시연은 실제로 lowentc 로 검사·실행해 얻은 출력을 그대로 인쇄한 "
     "것입니다. 조판은 Typst로 했습니다.</p>"
     "<p>이 책은 계속 고쳐집니다. 지금 읽고 계신 것은 위 번호의 판이고, 그 뒤로도 "
     "오류 수정과 내용 보강이 이어집니다. 오류 신고와 수정 제안은 저장소에서 "
     "받습니다.</p>"),
   lbl_repo="저장소", lbl_edition="판", lbl_updated="최종 수정",
   l_home="전체 사이트", l_repos="저장소 목록",
   t_repo="이 책의 GitHub 저장소",
   t_home="rubidus-api.github.io --- 공개된 모든 프로젝트",
   t_repos="github.com/rubidus-api --- 모든 저장소",
   short="Lowent 매뉴얼", other="En", other_href="../html-en/",
   toc="목차", prev="이전", next="다음", top="목차로", extra="부록과 찾아보기",
   toc_full="상세 차례", toc_full_desc="장 아래 절까지 펼친 차례다. 찾는 자리를 바로 짚을 때 쓴다.",
   toc_here="이 장의 차례", toc_close="닫기", skip="본문으로 건너뛰기",
   permalink="이 절의 주소", search="검색", search_ph="제목·용어로 찾기",
   search_none="찾은 것이 없다", search_hint="제목과 색인 표제어에서 찾는다 --- 본문 전체가 아니다",
   menu="메뉴", settings="설정", set_width="가로폭 제한", set_theme="테마",
   th_auto="시스템", th_light="밝게", th_dark="어둡게",
   set_colors="색", c_fg="글자", c_bg="배경", c_link="링크",
   set_note="설정은 이 브라우저에만 저장된다.", pdf="PDF",
   notes="주", note_back="본문으로 돌아가기",
   note=('이 판은 <strong>초안(draft)</strong>이다. 쪽 번호가 붙은 찾아보기와 정확한 조판은 '
         '<a href="{PDFLINK}">PDF</a>를 보라.'),
   qa=('책 내용에 궁금한 것이 있으면 <a href="https://github.com/rubidus-api/'
       'lowent_lang/discussions/categories/q-a"><strong>질문 게시판</strong></a>에 '
       '남겨 주세요. 시간이 되는 대로 아는 범위 안에서 답하겠습니다. '
       '한국어와 영어 어느 쪽이든 괜찮습니다. '
       '오탈자나 틀린 내용은 <a href="https://github.com/rubidus-api/lowent_lang/'
       'issues">이슈</a> 쪽이 낫습니다.'),
   spec=('이 판의 <strong>서식 예시</strong> --- 책에 쓰이는 모든 장치를 한 번씩 모아 둔 '
         '두 쪽이다: <a href="{SPECWEB}">웹</a> · <a href="{SPECPDF}">PDF</a>.')),
 "en": dict(title="Lowent Manual — Systems Programming with Contracts and Effects",
   main="Lowent Manual", sub="Systems Programming with Contracts and Effects",
   updated="last updated", author="rubidus",
   blurb=["An introduction to the Lowent language, and its user manual.",
          "Written for readers who have programmed a little,<br>up to those who already use a systems language such as C or Rust."],
   colophon="Copyright and contact", lbl_author="author", lbl_contact="contact",
   col_text=("<p><strong>The text</strong> — Creative Commons "
     "Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0). "
     "You may share and adapt it freely with attribution; commercial use is not "
     "permitted, and adaptations must carry the same licence.<br>"
     "<a href=\"https://creativecommons.org/licenses/by-nc-sa/4.0/\">"
     "creativecommons.org/licenses/by-nc-sa/4.0/</a></p>"
     "<p><strong>The example code</strong> — MIT licence. Take it and use it. The "
     "Lowent compiler and standard library used by the examples follow the repository's MIT licence."
     "</p>"
     "<p>Every code demonstration in this book is output actually obtained by "
     "checking and running it with lowentc. The typesetting is done with Typst.</p>"
     "<p>This book keeps being revised. What you are reading is the edition "
     "numbered above; corrections and additions follow it. Reports and suggestions "
     "are taken at the repository.</p>"),
   lbl_repo="repository", lbl_edition="edition", lbl_updated="last updated",
   l_home="All projects", l_repos="All repos",
   t_repo="This book on GitHub",
   t_home="rubidus-api.github.io --- every published project",
   t_repos="github.com/rubidus-api --- every repository",
   short="Lowent Manual", other="Ko", other_href="../html-ko/",
   toc="Contents", prev="Prev", next="Next", top="Contents", extra="Front and back matter",
   toc_full="Detailed contents", toc_full_desc="The contents opened out to the section level --- for going straight to a place.",
   toc_here="Contents of this chapter", toc_close="Close", skip="Skip to content",
   permalink="Link to this section", search="Search", search_ph="Find by heading or term",
   search_none="Nothing found", search_hint="Searches headings and index terms --- not the full text",
   menu="Menu", settings="Settings", set_width="Limit line width",
   set_theme="Theme", th_auto="System", th_light="Light", th_dark="Dark",
   set_colors="Colours", c_fg="Text", c_bg="Background", c_link="Link",
   set_note="Settings are kept in this browser only.", pdf="PDF",
   notes="Notes", note_back="back to text",
   note=('This edition is a <strong>draft</strong>. For the paginated index and exact '
         'typesetting, see the '
         '<a href="{PDFLINK}">PDF</a>.'),
   qa=('If anything in the book leaves you wondering, please ask on the '
       '<a href="https://github.com/rubidus-api/lowent_lang/discussions/'
       'categories/q-a"><strong>Q&amp;A board</strong></a>. I answer as time allows '
       'and as far as I know the answer. Korean or English, either is fine. '
       'For typos and mistakes, an <a href="https://github.com/rubidus-api/'
       'lowent_lang/issues">issue</a> is the better place.'),
   spec=('The <strong>style specimen</strong> for this edition --- every device the book '
         'uses, gathered onto two pages: <a href="{SPECWEB}">web</a> · '
         '<a href="{SPECPDF}">PDF</a>.')),
}[lang]

SRC_MAIN = (pathlib.Path(__file__).resolve().parent.parent
            / ("typst-ko" if lang == "ko" else "typst-en") / "main.typ")

def book_meta():
    """판 번호·성격·최종 수정일의 단일 출처는 main.typ 이다 — 여기에 베끼지 않는다."""
    txt = SRC_MAIN.read_text(encoding="utf-8")
    def one(name):
        m = re.search(r'#let\s+' + name + r'\s*=\s*"([^"]*)"', txt)
        return m.group(1) if m else ""
    return one("book-version"), one("book-status"), one("book-updated")

body = re.sub(r'<ol style="list-style-type: none">.*?</ol>\s*(?=<h2)', "", body, count=1, flags=re.S)

# 아이콘이 붙는 장치는 언어와 무관하고, 글자 라벨만 판마다 다르다
# (라벨의 단일 출처는 book/lib.typ 의 _L 이다).
DEVICES = [("이 장이 끝나면","organizer"),("BY THE END OF THIS CHAPTER","organizer"),
           ("By the end of this chapter","organizer"),
           ("먼저 알아야 할 것","prereq"),("WHAT THIS CHAPTER BUILDS ON","prereq"),
           ("이 장의 필요성과 맥락","why"),
           ("The need for this chapter, and its context","why"),
           ("THE NEED FOR THIS CHAPTER, AND ITS CONTEXT","why"),
           ("이 장에서 답할 질문","questions"),("THE QUESTIONS THIS CHAPTER ANSWERS","questions"),
           ("돌아보기","deepqa"),("LOOKING BACK","deepqa"),("Looking back","deepqa"),
           ("흔한 오해","misconception"),("A common misconception","misconception"),
           # 아이콘 대신 낱말 접두어를 쓰는 서식 (2026-08-06). 옛 아이콘도 함께 받는다.
           ("실제 사례","realcase"),("In practice","realcase"),
           ("반례","antipattern"),("Counter-example","antipattern"),
           ("수학","mathbox"),("The mathematics","mathbox"),
           ("복습 정리","recap"),("Recap","recap"),
           ("플랫폼 노트","platform"),("Platform note","platform"),
           ("⚠","misconception"),("◉","realcase"),("✗","antipattern"),
           ("∑","mathbox"),("☰","recap"),("⊞","platform"),
           ("문","qa-q"),("답","qa-a"),("Q","qa-q"),("A","qa-a")]

def _is_label(text, prefix):
    """라벨 판정: 정확히 같거나, 라벨 뒤에 공백이나 마침표가 와야 한다.
    접두 일치만 보면 `문`이 "문자열…"을, `A` 가 "A bundle…"을 물어 버린다.
    아이콘(⚠·◉ 등)은 글자가 아니므로 뒤에 무엇이 오든 라벨로 본다.
    ★ "문."·"답." 처럼 표지에 마침표가 붙는 서식(2026-08-06)을 함께 받는다."""
    if text == prefix:
        return True
    if not text.startswith(prefix):
        return False
    last = prefix[-1]
    if not (last.isalnum() or "가" <= last <= "힣"):
        return True
    return text[len(prefix):len(prefix) + 1] in (" ", "\u00a0", ".")


def _split_label(first_html, prefix):
    """첫 문단이 "답. 본문…" 처럼 표지와 본문을 함께 담은 경우, 표지만
    떼어 `<span class="dev-label">` 으로 감싸고 나머지는 그대로 둔다.
    표지만 있는 문단이면 None 을 돌려준다(기존 경로를 쓴다)."""
    plain = re.sub(r"<[^>]+>", "", first_html).strip()
    if plain == prefix or plain == prefix + ".":
        return None
    rest = first_html.lstrip()
    if not rest.startswith(prefix):
        return None
    rest = rest[len(prefix):]
    mark = prefix
    if rest.startswith("."):
        rest, mark = rest[1:], prefix + "."
    return f'<p><span class="dev-label">{mark}</span> {rest.lstrip()}</p>'


def tag_devices(text):
    """장치 상자에 클래스를 붙인다.

    ★ 상자 안에 <div> 가 중첩될 수 있다(본문 들여쓰기를 위해 블록을 하나 더
      감싼다). 그래서 단순 정규식이 아니라 여는/닫는 <div> 의 짝을 센다 —
      2026-08-06 에 이 중첩 때문에 72장의 실제 사례·플랫폼 노트·복습 정리가
      통째로 서식을 잃었다."""
    out, i = [], 0
    while True:
        k = text.find("<div>", i)
        if k == -1:
            out.append(text[i:]); break
        inner_start = k + 5
        end = _div_end(text, inner_start)          # 짝이 맞는 </div> 뒤
        inner = text[inner_start:end - 6]
        out.append(text[i:k])

        first = re.search(r"<p>(.*?)</p>", inner, re.S)
        # 표제는 *직계* 문단이어야 한다. 안쪽 <div> 속의 문단을 표제로 오인하면
        # 바깥 상자가 통째로 잘못 분류된다(문답이 통째로 질문이 되는 사고).
        nested = inner.find("<div")
        if first and nested != -1 and nested < first.start():
            first = None
        tagged = None
        if first:
            label = re.sub(r"<[^>]+>", "", first.group(1)).strip()
            for prefix, cls in DEVICES:
                if _is_label(label, prefix):
                    merged = _split_label(first.group(1), prefix)
                    if merged is not None:
                        head = inner[:first.start()] + merged + inner[first.end():]
                    else:
                        head = re.sub(r"<p>(.*?)</p>", r'<p class="dev-label">\1</p>',
                                      inner, count=1, flags=re.S)
                    tagged = f'<div class="dev {cls}">{head}</div>'
                    break
        else:
            plain = re.sub(r"<[^>]+>", "", inner).strip()
            for prefix, cls in DEVICES:
                if plain.startswith(prefix):
                    rest = inner.replace(prefix, "", 1).lstrip()
                    tagged = (f'<div class="dev {cls}">'
                              f'<p class="dev-label">{prefix}</p><p>{rest}</p></div>')
                    break

        if tagged is None:
            # 이 <div> 는 장치가 아니다 — 안쪽을 다시 훑는다
            out.append("<div>")
            i = inner_start
        else:
            out.append(tagged)
            i = end
    return "".join(out)


# ── 구문 강조 — 조판 테마(book/theme-print.tmTheme)를 켠 뒤로 Typst 의 HTML
#    내보내기가 <strong>·<em>·인라인 색을 직접 낸다. 여기서는 그 인라인 색만
#    CSS 변수 클래스로 바꿔 다크 모드에서도 읽히게 한다.
_TOK_BY_COLOR = {
    "#3f5b4a": "tok-c",   # 주석
    "#123c87": "tok-k",   # 키워드
    "#0f5b63": "tok-t",   # 타입 이름
    "#7a3b0a": "tok-s",   # 문자열
    "#8a2222": "tok-n",   # 수·상수
    "#6b2d8a": "tok-p",   # 전처리기
    "#555555": "tok-x",   # 구두점·연산자
    "#5f5f5f": "tok-c",   # (옛 테마 잔재)
    "#1f4d7a": "tok-s",
    "#141414": None,
}

def _map_colors(mo):
    block = mo.group(0)
    def one(m):
        cls = _TOK_BY_COLOR.get(m.group(1).lower(), None)
        return f'<span class="{cls}">' if cls else "<span>"
    return re.sub(r'<span style="color: (#[0-9a-fA-F]{6})">', one, block)

body = re.sub(r"<pre>.*?</pre>", _map_colors, body, flags=re.S)

# ── 장 서두 5종은 중첩 <div> 를 담으므로 짝을 세어 잡는다 (RFC-0008 §2.4)
_OPEN_LABELS = {
    "먼저 알아야 할 것": "prereq", "WHAT THIS CHAPTER BUILDS ON": "prereq",
    "돌아보기": "deepqa", "LOOKING BACK": "deepqa",
    "이 장의 필요성과 맥락": "why",
    "The need for this chapter, and its context": "why",
    "이 장이 끝나면": "organizer", "BY THE END OF THIS CHAPTER": "organizer",
    "이 장에서 답할 질문": "questions", "THE QUESTIONS THIS CHAPTER ANSWERS": "questions",
}

def tag_openings(text):
    out, i = [], 0
    while True:
        m = re.compile(r"<div><p>([^<]{1,60})</p>").search(text, i)
        if not m:
            out.append(text[i:]); break
        cls = _OPEN_LABELS.get(htmlmod.unescape(m.group(1)).strip())
        if cls is None:
            out.append(text[i:m.end()]); i = m.end(); continue
        # 여는 <div> 의 짝을 센다
        depth, j = 1, m.end()
        while depth and j < len(text):
            nxt_open = text.find("<div", j)
            nxt_close = text.find("</div>", j)
            if nxt_close == -1: break
            if nxt_open != -1 and nxt_open < nxt_close:
                depth += 1; j = nxt_open + 4
            else:
                depth -= 1; j = nxt_close + 6
        inner = text[m.end():j - 6]
        out.append(text[i:m.start()])
        out.append(f'<div class="dev {cls}"><p class="dev-label">{m.group(1)}</p>{inner}</div>')
        i = j
    return "".join(out)

# ── 묶기: 서두 4종은 하나의 테두리를 공유하고, 문답은 한 상자 안에서
#    가로선으로 갈린다 (저자 지시 2026-08-06, HTML 전용).
def _div_end(text, start):
    """`start`(여는 <div ...> 다음 위치)에서 짝이 맞는 </div> 뒤 위치."""
    depth, j = 1, start
    while depth and j < len(text):
        o = text.find("<div", j)
        c = text.find("</div>", j)
        if c == -1:
            return len(text)
        if o != -1 and o < c:
            depth += 1; j = o + 4
        else:
            depth -= 1; j = c + 6
    return j


def _wrap_runs(text, classes, wrapper, outer=False, pair_only=False):
    """연속한 장치들을 하나의 <div class=wrapper> 로 감싼다.

    `outer=True` 면 장치를 감싸고 있는 바깥 <div> 단위로 묶는다 — 서두 4종은
    표지 div 와 내용 div 가 바깥 div 하나에 함께 들어 있기 때문이다."""
    body_re = r'<div class="dev (' + "|".join(classes) + r')">'
    open_re = re.compile((r"<div>\s*" if outer else "") + body_re)
    out, i = [], 0
    while True:
        m = open_re.search(text, i)
        if not m:
            out.append(text[i:]); break
        out.append(text[i:m.start()])
        run_start = m.start()
        end = _div_end(text, m.start() + 5 if outer else m.end())
        seen = [m.group(1)]
        while True:
            gap = re.match(r"\s*", text[end:]).end()
            n = open_re.match(text, end + gap)
            if not n:
                break
            end = _div_end(text, n.start() + 5 if outer else n.end())
            seen.append(n.group(1))
        if pair_only and len(seen) < 2:
            out.append(text[run_start:end]); i = end; continue
        out.append(f'<div class="{wrapper}">' + text[run_start:end] + "</div>")
        i = end
    return "".join(out)


_DEMO_SRC = re.compile(r"<div><p><code>examples(?:-en)?/")
_DEMO_OUT_LABELS = ("실행 결과", "Output", "표준 입력으로 준 것",
                    "Given on standard input")


def tag_demos(text):
    """시연 상자(소스·입력·출력)에 클래스를 붙인다 — 선명한 테두리를 주려고."""
    out, i = [], 0
    while True:
        k = text.find("<div>", i)
        if k == -1:
            out.append(text[i:]); break
        body_start = k + 5
        head = text[body_start:body_start + 120]
        cls = None
        if _DEMO_SRC.match(text[k:k + 130]):
            cls = "demo-src"
        else:
            plain = re.sub(r"<[^>]+>", "", head).strip()
            if any(plain.startswith(lbl) for lbl in _DEMO_OUT_LABELS):
                cls = "demo-out"
        out.append(text[i:k])
        out.append(f'<div class="{cls}">' if cls else "<div>")
        i = body_start
    return "".join(out)

body = tag_openings(body)

body = tag_devices(body)
body = tag_demos(body)
# 서두 표지 뒤에 남는 빈 문단 제거 — 헛여백의 원인이다 (빈 문단 제거)
body = re.sub(r"<p>\s*</p>", "", body)
# 표는 제 폭만 쓰고 가운데 정렬한다. 넘칠 때만 감싼 상자가 가로로 스크롤한다.
body = re.sub(r"(<figure class=\"tbl\">)(<table>.*?</table>)",
              r'\1<div class="tblwrap">\2</div>', body, flags=re.S)

parts = re.split(r'(<h2[^>]*>.*?</h2>)', body, flags=re.S)
front = parts[0]
chapters = []
for i in range(1, len(parts), 2):
    head = parts[i]; content = parts[i+1] if i+1 < len(parts) else ""
    # 제목은 순수 텍스트로 보관한다. 내보낸 HTML 에서 뽑은 것이라 이미
    # 이스케이프되어 있으므로 한 번 되돌린다 — 그러지 않으면 목차와 <title>
    # 에서 `<stdio.h>` 가 `&lt;stdio.h>` 로 이중 이스케이프된다.
    chapters.append((htmlmod.unescape(re.sub(r"<[^>]+>", "", head)).strip(), head + content))

# ★ 조판은 styles/book.css 에 있다 --- 여기에 CSS 를 적지 않는다(RFC-0027).
CSS = (pathlib.Path(__file__).resolve().parent.parent
       / "styles" / "book.css").read_text(encoding="utf-8")

# ── 글꼴 — 판마다 다르다. 한국어판은 Noto CJK KR + D2Coding,
#    영어판은 라틴 Noto + Noto Sans Mono (PDF 와 같은 선택).
FONTS = {
 "ko": ('"PCB Serif","Noto Serif CJK KR","Noto Serif KR",Georgia,serif',
        '"PCB Sans","Noto Sans CJK KR","Noto Sans KR",system-ui,sans-serif',
        '"PCB Mono","D2Coding",ui-monospace,SFMono-Regular,Menlo,monospace'),
 "en": ('"PCB Serif","Noto Serif",Georgia,"Noto Serif CJK KR",serif',
        '"Noto Sans",system-ui,"Noto Sans CJK KR",sans-serif',
        '"Noto Sans Mono",ui-monospace,SFMono-Regular,Menlo,monospace'),
}[lang]
CSS += f"""
body {{ font-family:{FONTS[0]}; }}
h1,h2,h3,h4,.bar,.dev-label,.toc-part,.cover-meta,.badge,
.colophon-meta dt {{ font-family:{FONTS[1]}; }}
pre,code,kbd {{ font-family:{FONTS[2]}; }}
"""

def collect_notes(inner, page_id):
    """본문에 흩어진 출처 주석을 *장 끝*으로 모은다 (저자 지시 2026-08-09).

    PDF 에서는 `#footnote` 가 페이지 하단에 자리를 잡는다. 그런데 Typst 0.15.1 의
    HTML 내보내기는 각주 *본문*을 버리므로(참조 13개에 본문 1개), lib.typ 의
    `note()` 는 HTML 에서 일단 본문 안에 `<span class="src-note">` 로 펴 둔다.
    그대로 두면 출처가 문장 한가운데 끼어들어 읽기를 끊는다 — 그래서 여기서
    번호를 매겨 위첨자 링크로 바꾸고, 본문은 페이지(=장) 끝의 「주」로 옮긴다.
    """
    OPEN = '<span class="src-note">'
    out, notes, pos, n = [], [], 0, 0
    while True:
        i = inner.find(OPEN, pos)
        if i < 0:
            out.append(inner[pos:])
            break
        out.append(inner[pos:i])
        # 여는 태그부터 짝이 맞는 </span> 까지 — 안에 다른 span 이 들어 있어도 센다
        j, depth = i + len(OPEN), 1
        while depth and j < len(inner):
            a = inner.find("<span", j)
            b = inner.find("</span>", j)
            if b < 0:
                break
            if 0 <= a < b:
                depth += 1
                j = a + 5
            else:
                depth -= 1
                j = b + 7
        body = inner[i + len(OPEN): j - 7]
        n += 1
        ref, tgt = f"{page_id}-fnref-{n}", f"{page_id}-fn-{n}"
        out.append(f'<sup class="fnref" id="{ref}">'
                   f'<a href="#{tgt}" aria-label="{STR["notes"]} {n}">{n}</a></sup>')
        notes.append((tgt, ref, body))
        pos = j
    if not notes:
        return inner
    items = "".join(
        f'<li id="{tgt}">{body} '
        f'<a class="fnback" href="#{ref}" aria-label="{STR["note_back"]}"'
        f' title="{STR["note_back"]}">&#8617;</a></li>'
        for tgt, ref, body in notes)
    return ("".join(out)
            + f'<section class="chapter-notes" role="doc-endnotes">'
              f'<h2>{STR["notes"]}</h2><ol>{items}</ol></section>')


# ── 읽기 설정과 패널 (저자 지시 2026-08-11) ────────────────────────────
#   ★ 설정은 브라우저에만 남긴다(localStorage). 서버도 계정도 없는 책이다.
#   ★ 적용 스크립트는 <head> 에서 먼저 돈다 --- 본문이 그려진 뒤에 색을 바꾸면
#     흰 화면이 한 번 번쩍인다(flash of unstyled content).
APPLY_JS = (
    "(function(){try{var s=JSON.parse(localStorage.getItem('pcb-read')||'{}');"
    "var r=document.documentElement;"
    "if(s.theme&&s.theme!=='auto')r.setAttribute('data-theme',s.theme);"
    "if(s.fg)r.style.setProperty('--fg',s.fg);"
    "if(s.bg)r.style.setProperty('--bg',s.bg);"
    "if(s.link)r.style.setProperty('--link',s.link);"
    "if(s.width)r.setAttribute('data-measure','on');"
    "}catch(e){}})();"
)

# ── 검색 --- 색인은 *쓸 때만* 내려받는다(첫 글자를 칠 때 한 번).
#    찾는 것은 제목과 색인 표제어다. 본문 전문이 아니라는 것을 화면에 적어 둔다.
SEARCH_JS = """
(function(){
  var q=document.getElementById('q'), out=document.getElementById('qr');
  if(!q||!out) return;
  var data=null, NONE=__NONE__;
  function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML;}
  function run(){
    var v=q.value.trim().toLowerCase();
    out.innerHTML='';
    if(!v||!data) return;
    var hits=[];
    for(var i=0;i<data.length && hits.length<40;i++){
      if(data[i][0].toLowerCase().indexOf(v)>=0) hits.push(data[i]);
    }
    if(!hits.length){ out.innerHTML='<li>'+NONE+'</li>'; return; }
    out.innerHTML=hits.map(function(h){
      return '<li><a href="'+h[1]+'">'+esc(h[0])+'</a>'+
             (h[2]?'<span class="where">'+esc(h[2])+'</span>':'')+'</li>';
    }).join('');
  }
  q.addEventListener('input',function(){
    if(data){ run(); return; }
    fetch('search-index.json').then(function(r){return r.json();})
      .then(function(j){ data=j; run(); })
      .catch(function(){ out.innerHTML='<li>'+NONE+'</li>'; });
  });
})();
"""
# ★ f-문자열로 쓰면 JS 의 중괄호를 전부 겹쳐 적어야 해서 읽기 어렵다.
#   자리표 하나만 바꾼다.
SEARCH_JS = SEARCH_JS.replace("__NONE__", json.dumps(STR["search_none"]))

PANEL_JS = (
    "(function(){"
    "var b=document.querySelector('.here-btn'),p=document.getElementById('here-panel');"
    "function open(o){if(!p)return;p.hidden=!o;if(b)b.setAttribute('aria-expanded',o);}"
    "if(b)b.addEventListener('click',function(){open(p.hidden);});"
    "if(p)p.addEventListener('click',function(e){"
    "if(e.target.tagName==='A'&&e.target.getAttribute('href').charAt(0)==='#')open(false);});"
    "document.addEventListener('keydown',function(e){if(e.key==='Escape')open(false);});"
    # ── 읽기 설정 ──
    "var box=document.getElementById('setbox'),so=document.getElementById('set-open');"
    "if(!box||!so)return;"
    "var r=document.documentElement;"
    "function load(){try{return JSON.parse(localStorage.getItem('pcb-read')||'{}');}"
    "catch(e){return {};}}"
    "function save(s){try{localStorage.setItem('pcb-read',JSON.stringify(s));}catch(e){}}"
    "function css(n){return getComputedStyle(r).getPropertyValue(n).trim();}"
    "function paint(){var s=load();"
    "document.getElementById('s-width').checked=!!s.width;"
    "var t=s.theme||'auto';"
    "[].forEach.call(box.querySelectorAll('.segb'),function(x){"
    "x.setAttribute('aria-pressed',x.dataset.theme===t);});"
    "document.getElementById('s-fg').value=s.fg||hex(css('--fg'));"
    "document.getElementById('s-bg').value=s.bg||hex(css('--bg'));"
    "document.getElementById('s-link').value=s.link||hex(css('--link'));}"
    # 색 입력칸은 #rrggbb 만 받는다 --- 계산된 값이 rgb() 로 올 때가 있다
    "function hex(v){var m=v.match(/^rgba?\\((\\d+)[ ,]+(\\d+)[ ,]+(\\d+)/);"
    "if(!m)return v.charAt(0)==='#'?v:'#000000';"
    "return '#'+[1,2,3].map(function(i){"
    "return ('0'+parseInt(m[i],10).toString(16)).slice(-2);}).join('');}"
    "so.addEventListener('click',function(){box.hidden=!box.hidden;"
    "so.setAttribute('aria-expanded',!box.hidden);if(!box.hidden)paint();});"
    "document.getElementById('s-width').addEventListener('change',function(){"
    "var s=load();s.width=this.checked;save(s);"
    "if(s.width)r.setAttribute('data-measure','on');else r.removeAttribute('data-measure');});"
    "[].forEach.call(box.querySelectorAll('.segb'),function(x){"
    "x.addEventListener('click',function(){var s=load();s.theme=x.dataset.theme;"
    # ★ 테마를 고르는 것은 색을 *놓아 주는* 일이기도 하다. 직접 고른 색이
    #   남아 있으면 그것이 테마를 덮어, 단추를 눌러도 색이 그대로다 ---
    #   저자 지적 2026-08-11. 그래서 테마 전환이 곧 되돌리기다.
    "delete s.fg;delete s.bg;delete s.link;save(s);"
    "['--fg','--bg','--link'].forEach(function(n){r.style.removeProperty(n);});"
    "if(s.theme==='auto')r.removeAttribute('data-theme');"
    "else r.setAttribute('data-theme',s.theme);"
    "setTimeout(paint,0);});});"
    "[['s-fg','--fg','fg'],['s-bg','--bg','bg'],['s-link','--link','link']]"
    ".forEach(function(a){document.getElementById(a[0]).addEventListener('input',"
    "function(){var s=load();s[a[2]]=this.value;save(s);"
    "r.style.setProperty(a[1],this.value);});});"
    "})();"
)

def page(title, inner, prev=None, nxt=None, is_index=False, self_name=None,
         sections=None, label=None):
    # 위 고정 바에도 이전·목차·다음을 둔다 (저자 지시 2026-08-06) — 긴 장에서
    # 바닥까지 내려가지 않고도 옮겨 다닐 수 있어야 한다.
    bar_nav = ""
    if not is_index:
        bar_nav = '<span class="bar-nav">'
        bar_nav += (f'<a href="{prev[0]}" title="{STR["prev"]}"'
                    f' aria-label="{STR["prev"]}">←</a>' if prev
                    else f'<span class="off" aria-hidden="true">←</span>')
        bar_nav += (f'<a href="index.html" title="{STR["top"]}"'
                    f' aria-label="{STR["top"]}">↑</a>')
        bar_nav += (f'<a href="{nxt[0]}" title="{STR["next"]}"'
                    f' aria-label="{STR["next"]}">→</a>' if nxt
                    else f'<span class="off" aria-hidden="true">→</span>')
        bar_nav += "</span>"
    # 언어 전환은 *같은 장*으로 보낸다 (저자 지시 2026-08-07).
    # 장 페이지의 이름(chNN.html)은 두 판에서 같다. 번호 없는 앞·뒷부속은
    # 판마다 쪽수가 달라 이름이 어긋나므로 그쪽 차례로 보낸다.
    other_href = STR["other_href"]
    if self_name and self_name.startswith("ch"):
        other_href = STR["other_href"] + self_name

    nav = ""
    if not is_index:
        left = f'<a href="{prev[0]}">← {STR["prev"]}</a>' if prev else "<span></span>"
        right = f'<a href="{nxt[0]}">{STR["next"]} →</a>' if nxt else "<span></span>"
        nav = (f'<div class="nav">{left}<span class="sp"></span>'
               f'<a href="index.html">{STR["top"]}</a><span class="sp"></span>{right}</div>')
    # ── 상단 바와 그 아래 펼침 패널 (저자 지시 2026-08-11) ──────────────
    #   바에는 넷만 둔다: 책 이름 · 지금 어디인가(단추) · 이전/목차/다음.
    #   나머지(설정·상세 차례·저장소·다른 판·PDF)와 이 장의 차례는 단추를
    #   누르면 그 아래로 펼쳐진다. 늘 보이던 것을 접어 두어 읽는 자리를 넓힌다.
    btn_label = label or STR["menu"]
    here_btn = (f'<button class="here-btn" type="button" aria-expanded="false"'
                f' aria-controls="here-panel">'
                f'{htmlmod.escape(btn_label)} <span class="caret">▾</span></button>')

    pdf_url = f"../pdf-{lang}/lowent-manual-{lang}.pdf"
    # 판 번호는 도구 줄 맨 왼쪽에 둔다(저자 지시 2026-08-11) --- 「지금 보는
    # 것이 어느 판인가」는 오류를 신고할 때 가장 먼저 필요한 사실이다.
    tools = (f'<span class="ver">{version}</span>'
             f'<button class="tool" type="button" id="set-open">⚙ {STR["settings"]}</button>'
             f'<a class="tool" href="toc.html">▤ {STR["toc_full"]}</a>'
             f'<a class="tool" href="{other_href}">{STR["other"]}</a>'
             f'<a class="tool" href="{pdf_url}">⤓ {STR["pdf"]}</a>'
             f'<a class="tool" href="https://github.com/rubidus-api/lowent_lang"'
             f' title="{STR["t_repo"]}">Git</a>'
             # 이 책 밖으로 나가는 두 자리 --- 공개된 다른 것들이 어디 있는지
             # (저자 지시 2026-09-02). 자주 누르는 것이 아니라 바가 아니라
             # 이 패널에 둔다.
             f'<a class="tool" href="https://rubidus-api.github.io/"'
             f' title="{STR["t_home"]}">⌂ {STR["l_home"]}</a>'
             f'<a class="tool" href="https://github.com/rubidus-api"'
             f' title="{STR["t_repos"]}">≡ {STR["l_repos"]}</a>')

    # 읽기 설정 --- 값은 브라우저(localStorage)에만 남는다. 서버도 계정도 없다.
    settings = (
        f'<div class="setbox" id="setbox" hidden>'
        f'<label class="setrow"><input type="checkbox" id="s-width">'
        f'<span>{STR["set_width"]}</span></label>'
        f'<div class="setrow"><span class="setlbl">{STR["set_theme"]}</span>'
        f'<span class="seg">'
        f'<button type="button" class="segb" data-theme="auto">{STR["th_auto"]}</button>'
        f'<button type="button" class="segb" data-theme="light">{STR["th_light"]}</button>'
        f'<button type="button" class="segb" data-theme="dark">{STR["th_dark"]}</button>'
        f'</span></div>'
        f'<div class="setrow"><span class="setlbl">{STR["set_colors"]}</span>'
        f'<label class="col"><span>{STR["c_fg"]}</span>'
        f'<input type="color" id="s-fg"></label>'
        f'<label class="col"><span>{STR["c_bg"]}</span>'
        f'<input type="color" id="s-bg"></label>'
        f'<label class="col"><span>{STR["c_link"]}</span>'
        f'<input type="color" id="s-link"></label>'
        f'</div>'
        f'<p class="setnote">{STR["set_note"]}</p></div>')

    rows = ""
    if sections:
        rows = "".join(
            f'<a class="lv{lv}" href="#{sid}"><span class="tf-no">{num}</span>'
            f'{htmlmod.escape(t)}</a>' for lv, num, t, sid in sections)
        rows = (f'<div class="here-head"><strong>{STR["toc_here"]}</strong></div>'
                f'<div class="here-body">{rows}</div>')
    here_panel = (f'<div id="here-panel" class="here-panel" hidden>'
                  f'<div class="tools">{tools}</div>{settings}{rows}</div>')

    return f"""<!doctype html>
<html lang="{lang}">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{htmlmod.escape(title)}</title><style>{CSS}</style>
<script>{APPLY_JS}</script></head>
<body>
<a class="skip" href="#content">{STR['skip']}</a>
<div class="bar"><strong><a href="index.html">{STR['short']}</a></strong>{here_btn}<span class="sp"></span>{bar_nav}</div>{here_panel}
<main id="content"><div class="wrap">
{inner}
{nav}
</div></main>
<script>{PANEL_JS}</script><script>{SEARCH_JS}</script></body></html>
"""


# ── 절 단위 차례 (저자 지시 2026-08-10) ──────────────────────────
# 조판된 h3·h4 에는 번호가 붙어 있으나 id 가 없어 링크할 수 없었다.
# 여기서 번호를 열쇠로 id 를 달고, 그 목록을 두 곳에 쓴다.
#   ① 상세 차례 페이지(toc.html) --- 머리말 다음 자리
#   ② 장 페이지 상단 바의 「이 장의 차례」 패널
# 번호가 붙은 절(본문)과 붙지 않은 절(부록)을 함께 받는다 --- 부록도 상세
# 차례에 절까지 나와야 한다(저자 지시 2026-08-10).
_HEAD = re.compile(r'<(h[34])>\s*((?:\d+\.)+\d+)?\s*(.*?)</\1>', re.S)


def _plain(html_text):
    """제목 속 태그를 걷어 낸 글자만."""
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", html_text)).strip()


def short_label(title, lang):
    """상단 바 단추에 쓸 *짧은* 이름 (저자 지시 2026-08-10).

    「이 장의 차례」라고 길게 쓰는 대신 지금 보고 있는 자리를 그대로 적는다 ---
    「12장」·「부록 A」·「머리말」. 바가 좁아도 한 줄에 들어가고, 지금 어디인지도
    함께 알려 준다.
    """
    t = " ".join(title.split())
    m = re.match(r"(\d+)\s", t)
    if m:
        return f"{m.group(1)}장" if lang == "ko" else f"ch. {m.group(1)}"
    m = re.match(r"부록\s*([A-Z])", t)
    if m:
        return f"부록 {m.group(1)}"
    m = re.match(r"Appendix\s+([A-Z])", t)
    if m:
        return f"app. {m.group(1)}"
    # 그 밖(머리말·찾아보기 …)은 제목의 앞머리를 그대로 쓴다
    head = re.split(r"\s+[—-]\s+", t)[0]
    return head if len(head) <= 12 else head[:11] + "…"


def number_sections(content):
    """h3·h4 에 id 를 달고, (수준, 번호, 제목, id) 목록을 함께 돌려준다."""
    items = []

    seq = [0]

    def add(mo):
        tag, num, body = mo.group(1), mo.group(2), mo.group(3)
        text = _plain(body)
        if not text:
            return mo.group(0)          # 빈 제목은 건드리지 않는다
        if num:
            sid = "s" + num.replace(".", "-")
            shown = f"{num} "
        else:
            seq[0] += 1                 # 부록처럼 번호가 없으면 일련번호로 id 를 만든다
            sid = f"sx{seq[0]}"
            shown = ""
        items.append((3 if tag == "h3" else 4, num or "", text, sid))
        # ★ 절마다 *그 절을 가리키는 주소*를 준다(저자 백로그 P2). 종이 책에는
        #   쪽수가 있지만 웹 판에는 없어서, 남에게 「거기」를 알려 줄 방법이
        #   없었다. 닻은 평소에 흐리게 두고 제목에 닿았을 때만 드러낸다.
        link = (f'<a class="plink" href="#{sid}" aria-label="{STR["permalink"]}"'
                f' title="{STR["permalink"]}">#</a>')
        return f'<{tag} id="{sid}">{shown}{body}{link}</{tag}>'

    return _HEAD.sub(add, content), items


out_dir.mkdir(parents=True, exist_ok=True)
for f in out_dir.glob("*.html"): f.unlink()
names = []
for i, (t, _) in enumerate(chapters):
    mnum = re.match(r"\s*(\d+)\s", t)
    names.append(f"ch{int(mnum.group(1)):02d}.html" if mnum else f"sec{i+1:02d}.html")
# ── 목차를 *부 단위로* 묶는다 ──────────────────────────────────
# 부 구성의 단일 출처는 main.typ 이다 (여기에 목록을 베끼지 않는다).
def read_parts(lang):
    """부 구성의 단일 출처는 `book/registry.typ` 이다 (RFC-0028).

    ★ 2026-08-16: 전에는 `main.typ` 의 `#let parts = (…)` 문자열을 정규식으로
    읽었다. 그 목록이 등록부에서 계산되도록 바뀌자 여기서 *아무것도 못 읽어*
    HTML 차례에서 98개 장이 통째로 사라졌다(표지·상세 차례가 앞부속과 부록만
    남았다). 같은 사고를 막으려 이제 등록부를 직접 읽고, 하나도 못 읽으면
    조용히 넘어가지 않고 멈춘다.
    """
    reg = pathlib.Path(__file__).resolve().parent.parent / "typst-ko" / "registry.typ"
    text = reg.read_text(encoding="utf-8")
    ids, order = [], []
    for m in re.finditer(r"chapters:\s*\(([^)]*)\)", text):
        order.append(re.findall(r'"([^"]+)"', m.group(1)))
    for chunk in order:
        ids += chunk
    no = {cid: i + 1 for i, cid in enumerate(ids)}
    key = "ko" if lang == "ko" else "en"
    titles = re.findall(r'%s: "([^"]+)"' % key, text)
    out = []
    for title, chunk in zip(titles, order):
        out.append((title, [no[c] for c in chunk]))
    if not out:
        raise SystemExit("wrap-html: 등록부에서 부 구성을 읽지 못했다 --- 차례가 비게 된다")
    return out

by_num = {}
for i, (t, _) in enumerate(chapters):
    mn = re.match(r"\s*(\d+)\s", t)
    if mn:
        by_num[int(mn.group(1))] = i

# 앞부속(머리말·번역 노트)과 뒷부속(부록·찾아보기)을 가른다.
# 문서 순서에서 첫 번호 장보다 앞에 있으면 앞부속이고, 그것은 부 목록보다
# *앞*에 놓는다 — 뒷부속 묶음("부록과 찾아보기")에 섞이면 안 된다.
first_ch = min(by_num.values()) if by_num else len(chapters)
last_ch = max(by_num.values()) if by_num else -1

toc_parts = []
placed = set()
front_rows = [f'<a href="{names[i]}">{htmlmod.escape(chapters[i][0])}</a>'
              for i in range(len(chapters)) if i < first_ch]
# ★ 머리말 바로 다음에 상세 차례로 가는 줄을 둔다 (저자 지시 2026-08-10).
front_rows.append(f'<a class="toc-detail" href="toc.html">{STR["toc_full"]} →</a>')
if front_rows:
    toc_parts.append(f'<div class="toc-group toc-front">{"".join(front_rows)}</div>')
for part_title, nums in read_parts(lang):
    rows = []
    for n in nums:
        i = by_num.get(n)
        if i is None:
            continue
        placed.add(i)
        rows.append(f'<a href="{names[i]}">{htmlmod.escape(chapters[i][0])}</a>')
    if rows:
        toc_parts.append(f'<h4 class="toc-part">{htmlmod.escape(part_title)}</h4>'
                         f'<div class="toc-group">{"".join(rows)}</div>')
rest = [f'<a href="{names[i]}">{htmlmod.escape(chapters[i][0])}</a>'
        for i in range(len(chapters))
        if i not in placed and i > last_ch]
if rest:
    toc_parts.append(f'<h4 class="toc-part">{STR["extra"]}</h4>'
                     f'<div class="toc-group">{"".join(rest)}</div>')
toc = "".join(toc_parts)
# ── 표지 — PDF 표제면의 내용을 목차 앞에 그대로 둔다 ─────────────
version, status, updated = book_meta()
repo = "https://github.com/rubidus-api/lowent_lang"
cover = (f'<header class="cover"><h1>{htmlmod.escape(STR["main"])}</h1>'
         f'<p class="cover-sub">{htmlmod.escape(STR["sub"])}</p>'
         f'<p><span class="badge">{htmlmod.escape(status)}</span></p>'
         f'<p class="cover-meta">{htmlmod.escape(version)} · '
         f'{STR["updated"]} {htmlmod.escape(updated)}</p>'
         f'<p class="cover-author">{htmlmod.escape(STR["author"])}</p>'
         f'<p class="cover-links"><a href="mailto:rubidus@gmail.com">rubidus@gmail.com</a>'
         f'<a href="{repo}">github.com/rubidus-api/lowent_lang</a></p>'
         + '<div class="cover-blurb">'
         + "".join(f'<p>{t}</p>' for t in STR["blurb"])
         + '</div></header>')
# ── 머리말은 제 페이지가 따로 있으므로 index 에 옮겨 싣지 않는다
#    (저자 지시 2026-08-07: 같은 글이 두 번 나올 이유가 없다).
#    차례의 앞부속 묶음 첫 줄이 그 페이지로 가는 링크다.
# ★ 「정확한 조판은 PDF 를 보라」의 링크는 *그 판의 PDF 를 바로 내려받는* 주소로
#   잇는다(저자 지시 2026-08-10). 전에는 dist 폴더를 가리켜 한 번 더 들어가야 했다.
pdf_href = f"../pdf-{lang}/lowent-manual-{lang}.pdf"
note_html = STR["note"].replace("{PDFLINK}", pdf_href)
# ── 판권 --- 표지 페이지 맨 아래 (저자 지시 2026-08-11) ─────────────
#   텍스트 판은 2쪽 아래에 둔다. 웹판에는 그런 「쪽」이 없으므로 표지
#   페이지의 끝에 둔다 --- 어느 판이든 *맨 처음 자리에서 한 번* 만난다.
#   ★ 문구가 typst-ko/main.typ 과 두 자리에 있다. scripts/check-colophon.py 가
#     둘이 어긋나지 않는지 대조한다.
colophon = (
    f'<section class="colophon" id="colophon">'
    f'<h3>{STR["colophon"]}</h3>'
    f'<dl class="colophon-meta">'
    f'<dt>{STR["lbl_author"]}</dt><dd>{STR["author"]}</dd>'
    f'<dt>{STR["lbl_contact"]}</dt><dd>'
    f'<a href="mailto:rubidus@gmail.com">rubidus@gmail.com</a></dd>'
    f'<dt>{STR["lbl_repo"]}</dt><dd>'
    f'<a href="{repo}">github.com/rubidus-api/lowent_lang</a></dd>'
    f'<dt>{STR["lbl_edition"]}</dt><dd>{version} — {status}</dd>'
    f'<dt>{STR["lbl_updated"]}</dt><dd>{updated}</dd></dl>'
    f'<div>{STR["col_text"]}</div></section>')

index_inner = (f'{cover}<div class="note">{note_html}</div>'
               f'<div class="note">{STR["qa"]}</div>'
               f'<h3>{STR["toc"]}</h3><div class="toc">{toc}</div>'
               f'{colophon}')
(out_dir/"index.html").write_text(page(STR["title"], index_inner, is_index=True, self_name="index.html"), encoding="utf-8")
chapter_sections = {}          # 파일이름 → [(수준, 번호, 제목, id)]
for i,(title,content) in enumerate(chapters):
    prev = ("index.html", STR["toc"]) if i==0 else (names[i-1], "")
    nxt = (names[i+1], "") if i+1 < len(chapters) else None
    content = collect_notes(content, names[i].removesuffix(".html"))
    content, sections = number_sections(content)
    chapter_sections[names[i]] = sections
    (out_dir/names[i]).write_text(page(f"{title} — {STR['short']}", content, prev, nxt,
                                      self_name=names[i], sections=sections,
                                      label=short_label(title, lang)), encoding="utf-8")

# ── 상세 차례 페이지 ────────────────────────────────────────────
rows = []
for part_title, nums in read_parts(lang):
    rows.append(f'<h4 class="toc-part">{htmlmod.escape(part_title)}</h4>')
    for n in nums:
        i = by_num.get(n)
        if i is None:
            continue
        f = names[i]
        rows.append(f'<div class="tf-ch"><a href="{f}">'
                    f'{htmlmod.escape(chapters[i][0])}</a></div>')
        secs = chapter_sections.get(f, [])
        if secs:
            rows.append('<div class="tf-secs">' + "".join(
                f'<a class="lv{lv}" href="{f}#{sid}">'
                f'<span class="tf-no">{num}</span>{htmlmod.escape(t)}</a>'
                for lv, num, t, sid in secs) + '</div>')
extra = [i for i in range(len(chapters)) if i not in placed and i > last_ch]
if extra:
    rows.append(f'<h4 class="toc-part">{STR["extra"]}</h4>')
    for i in extra:
        f = names[i]
        rows.append(f'<div class="tf-ch"><a href="{f}">'
                    f'{htmlmod.escape(chapters[i][0])}</a></div>')
        secs = chapter_sections.get(f, [])
        if secs:
            rows.append('<div class="tf-secs">' + "".join(
                f'<a class="lv{lv}" href="{f}#{sid}">'
                f'<span class="tf-no">{num}</span>{htmlmod.escape(t)}</a>'
                for lv, num, t, sid in secs) + '</div>')
# ── 검색 (저자 백로그 P2) ─────────────────────────────────────────
# ★ 무엇을 찾는지 화면에 적는다 --- *제목과 색인 표제어*다. 본문 전문 색인은
#   판마다 여러 MB 라, 내려받게 하는 값이 얻는 것보다 크다. 「검색」이라고만
#   써 두고 본문이 안 걸리면 독자는 *책에 없다*고 오해한다.
search_items = []
for f, secs in chapter_sections.items():
    ch = next((c[0] for c, n in zip(chapters, names) if n == f), "")
    for lv, num, txt_, sid in secs:
        search_items.append([txt_, f"{f}#{sid}", (num + " " + ch).strip()])
for i, (ct, _) in enumerate(chapters):
    search_items.append([ct, names[i], ""])
search_box = (
    f'<div class="searchbox">'
    f'<input type="search" id="q" placeholder="{STR["search_ph"]}"'
    f' aria-label="{STR["search"]}" autocomplete="off">'
    f'<p class="search-hint">{STR["search_hint"]}</p>'
    f'<ul class="search-results" id="qr"></ul></div>')

toc_full_inner = (f'<h2>{STR["toc_full"]}</h2>'
                  f'<p class="note">{STR["toc_full_desc"]}</p>'
                  f'{search_box}'
                  f'<div class="toc-full">{"".join(rows)}</div>')
(out_dir/"toc.html").write_text(
    page(f'{STR["toc_full"]} — {STR["short"]}', toc_full_inner,
         ("index.html", STR["toc"]), None, self_name="toc.html",
         label=STR["toc_full"]), encoding="utf-8")
# 분할로 어긋난 내부 앵커(#loc-N)를 "파일#앵커" 로 고친다
anchor_home = {}
for f in out_dir.glob("*.html"):
    for aid in re.findall(r'id="([^"]+)"', f.read_text(encoding="utf-8")):
        anchor_home[aid] = f.name
for f in out_dir.glob("*.html"):
    txt = f.read_text(encoding="utf-8")
    # ★ X-0068 — 번호 없는 절의 id(`sx1`·`sx2`…)는 **쪽마다** 다시 센다. 아래 지도는 id 하나에 쪽
    #   하나만 기억하므로(마지막으로 본 쪽), 그 id 를 가진 쪽이 여럿이면 링크가 **남의 쪽**으로 갔다
    #   (utf16 쪽의 절 자기 링크가 strings 쪽으로). ⇒ 이 쪽 안에 있는 id 는 이 쪽을 가리킨다.
    own = set(re.findall(r'id="([^"]+)"', txt))
    def fix(mo):
        aid = mo.group(1)
        if aid in own:
            return mo.group(0)
        # ★ 쪽마다 있는 자리표(#content)는 *그 쪽 안*을 가리킨다. 아래의 지도는
        #   「이 id 가 어느 쪽에 사는가」를 담는데, 모든 쪽에 있는 id 는 그중
        #   한 쪽으로 잘못 이어진다 --- 건너뛰기 링크가 1장으로 가 버렸다.
        if aid == "content":
            return mo.group(0)
        home = anchor_home.get(aid)
        if home is None:
            return mo.group(0)
        return f'href="#{aid}"' if home == f.name else f'href="{home}#{aid}"'
    new = re.sub(r'href="#([^"]+)"', fix, txt)
    if new != txt:
        f.write_text(new, encoding="utf-8")

total = sum(f.stat().st_size for f in out_dir.glob("*.html"))
print(f"wrap-html: {out_dir}/ — {len(chapters)} pages + index ({total//1024} KB)")


# ── 검색 색인은 *앵커를 고친 뒤에* 만든다 ─────────────────────────
# ★ 먼저 만들었더니 찾아보기의 링크가 아직 `#idx-entry-20` 이라 파일 이름이
#   빠졌다 --- 눌러도 그 쪽 안에서만 찾는다. 순서가 곧 정확성이다.
index_terms = {}
for f in sorted(out_dir.glob("sec*.html")):
    for m in re.finditer(r'<a href="([^"]*#idx-entry-\d+)">([^<]{1,40})</a>',
                         f.read_text(encoding="utf-8")):
        index_terms.setdefault(m.group(2).strip(), m.group(1))
for term, href in index_terms.items():
    if term and "#" in href and not href.startswith("#"):
        search_items.append([term, href, "색인" if lang == "ko" else "index"])
(out_dir / "search-index.json").write_text(
    json.dumps(search_items, ensure_ascii=False, separators=(",", ":")),
    encoding="utf-8")
print(f"wrap-html: 검색 색인 {len(search_items)}항목 "
      f"(제목·장 이름 + 색인 표제어 {len(index_terms)}) → {out_dir.name}/search-index.json")

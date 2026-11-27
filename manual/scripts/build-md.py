#!/usr/bin/env python3
"""Markdown 판: html-ko/ · html-en/ 의 쪽마다 md-ko/ · md-en/ 에 같은 이름의 .md 를 만든다.

원본은 Typst 이고 Markdown 은 *내보낸 결과*다 --- 손으로 고치지 않는다.
Typst 는 Markdown 을 직접 내지 못하므로, Typst 가 낸 HTML(wrap-html.py 가 쪽으로 나눈 것)을
읽어 옮긴다. 그래서 build-html.sh 뒤에 돌린다.

  - 장치(먼저 알아야 할 것 · 문답 · 흔한 오해 …)는 제목 붙은 인용 블록이 된다.
  - 시연은 파일 이름 줄 + 코드 블록, 실행 결과는 그 뒤 코드 블록이다.
  - 표는 GFM 표로, 표 이름은 표 아래 기울임 한 줄로 옮긴다.
  - 쪽 사이 링크(chNN.html)는 .md 로 바꾼다. 첫 쪽(index.html)은 README.md 가 된다.
"""
import html
import pathlib
import re
import shutil
import sys
from html.parser import HTMLParser

ROOT = pathlib.Path(__file__).resolve().parent.parent
VOID = {"br", "img", "hr", "meta", "link", "input", "data"}


class Node:
    def __init__(self, tag, attrs, parent=None):
        self.tag, self.attrs, self.parent, self.kids = tag, dict(attrs), parent, []

    def cls(self):
        return self.attrs.get("class", "") or ""


class Tree(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("root", [])
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.kids.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_startendtag(self, tag, attrs):
        self.cur.kids.append(Node(tag, attrs, self.cur))

    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root and n.tag != tag:
            n = n.parent
        if n is not self.root:
            self.cur = n.parent

    def handle_data(self, data):
        self.cur.kids.append(data)


def text_of(n):
    if isinstance(n, str):
        return n
    return "".join(text_of(k) for k in n.kids)


def fix_href(h):
    if h.startswith(("http:", "https:", "mailto:", "#")):
        return h
    h = re.sub(r"^index\.html", "README.md", h)
    return re.sub(r"\.html(#|$)", r".md\1", h)


def inline(n):
    """글자 수준 요소를 한 줄 Markdown 으로."""
    if isinstance(n, str):
        return re.sub(r"\s+", " ", n)
    t = n.tag
    if t in ("script", "style") or "plink" in n.cls():
        return ""
    if t == "br":
        return "  \n"
    if t == "code":
        s = text_of(n)
        tick = "``" if "`" in s else "`"
        pad = " " if s.startswith("`") or s.endswith("`") else ""
        return f"{tick}{pad}{s}{pad}{tick}"
    inner = "".join(inline(k) for k in n.kids)
    if "tf-no" in n.cls():
        return inner.strip() + " "
    if t in ("strong", "b"):
        return f"**{inner.strip()}**" if inner.strip() else ""
    if t in ("em", "i"):
        return f"*{inner.strip()}*" if inner.strip() else ""
    if t == "sup":
        return f"^{inner}"
    if t == "a":
        h = n.attrs.get("href")
        return f"[{inner}]({fix_href(h)})" if h else inner
    if t == "img":
        return f"![{n.attrs.get('alt', '')}]({n.attrs.get('src', '')})"
    return inner


BLOCK = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "ul", "ol", "li", "table",
         "figure", "blockquote", "section", "header", "nav", "dl", "dt", "dd", "hr", "details", "summary"}


def has_block(n):
    return any(not isinstance(k, str) and (k.tag in BLOCK or has_block(k)) for k in n.kids)


def table(n):
    rows = []
    for tr in iter_tags(n, "tr"):
        cells = [k for k in tr.kids if not isinstance(k, str) and k.tag in ("td", "th")]
        rows.append([cell_text(c) for c in cells])
    if not rows:
        return []
    width = max(len(r) for r in rows)
    rows = [r + [""] * (width - len(r)) for r in rows]
    out = ["| " + " | ".join(rows[0]) + " |", "|" + "---|" * width]
    out += ["| " + " | ".join(r) + " |" for r in rows[1:]]
    return out


def cell_text(c):
    s = " ".join(line.strip() for line in "\n".join(blocks(c)).splitlines() if line.strip())
    return s.replace("|", "\\|")


def iter_tags(n, tag):
    for k in n.kids:
        if isinstance(k, str):
            continue
        if k.tag == tag:
            yield k
        else:
            yield from iter_tags(k, tag)


def blocks(n):
    """블록 요소를 Markdown 줄들로. 빈 줄로 문단을 가른다."""
    out, buf = [], []

    def flush():
        s = "".join(buf).strip()
        if s:
            out.extend([s, ""])
        buf.clear()

    for k in n.kids:
        if isinstance(k, str) or (k.tag not in BLOCK and not has_block(k)):
            buf.append(inline(k))
            continue
        flush()
        out.extend(block(k))
    flush()
    return out


def quote(lines):
    return [("> " + l).rstrip() for l in lines]


def link_run(n):
    """글자 없이 링크만 둘 이상 늘어선 덩어리(차례·표지 링크)는 목록으로 편다."""
    els = [k for k in n.kids if not isinstance(k, str)]
    bare = "".join(k for k in n.kids if isinstance(k, str)).strip()
    return len(els) >= 2 and not bare and all(k.tag == "a" for k in els)


def block(n):
    t, c = n.tag, n.cls()
    if t in ("script", "style"):
        return []
    if t in ("div", "p", "nav") and link_run(n):
        return ["- " + inline(k).strip() for k in n.kids if not isinstance(k, str)] + [""]
    if re.fullmatch(r"h[1-6]", t):
        level = max(1, int(t[1]) - 1)
        anchor = f'<a id="{n.attrs["id"]}"></a>' if n.attrs.get("id") else ""
        return ["#" * level + " " + anchor + "".join(inline(k) for k in n.kids).strip(), ""]
    if t == "pre":
        code = n.kids[0] if len(n.kids) == 1 and not isinstance(n.kids[0], str) else n
        lang = code.attrs.get("data-lang", "") if isinstance(code, Node) else ""
        body = text_of(n).rstrip("\n")
        fence = "````" if "```" in body else "```"
        return [fence + lang, *body.split("\n"), fence, ""]
    if t in ("ul", "ol"):
        out, i = [], 0
        for li in n.kids:
            if isinstance(li, str) or li.tag != "li":
                continue
            i += 1
            mark = f"{i}. " if t == "ol" else "- "
            lines = [l for l in blocks(li)]
            while lines and not lines[-1]:
                lines.pop()
            lines = [l for l in lines if l != ""] or [""]
            out.append(mark + lines[0])
            out += ["   " + l for l in lines[1:]]
        return out + [""]
    if t == "table":
        return table(n) + [""]
    if t == "figure":
        cap = [k for k in iter_tags(n, "p") if "float-caption" in k.cls()]
        body = []
        for k in n.kids:
            if isinstance(k, str) or "float-caption" in k.cls():
                continue
            body += block(k)
        if cap:
            body += [f"*{''.join(inline(x) for x in cap[0].kids).strip()}*", ""]
        return body
    if "demo-head" in c:
        return [f"**{''.join(inline(k) for k in n.kids).strip()}**", ""]
    if t == "div" and c.startswith("dev"):
        label = next((k for k in n.kids if not isinstance(k, str) and "dev-label" in k.cls()), None)
        rest = Node("div", [])
        rest.kids = [k for k in n.kids if k is not label]
        head = [f"**{''.join(inline(x) for x in label.kids).strip()}**", ""] if label else []
        body = head + blocks(rest)
        while body and not body[-1]:
            body.pop()
        return quote(body) + [""]
    if t == "div" and "qa-box" in c:
        return blocks(n)
    if t == "hr":
        return ["---", ""]
    return blocks(n)


def page_md(src):
    s = src.read_text(encoding="utf-8")
    a = s.find('<div class="wrap">')
    b = s.rfind('<div class="nav">')
    body = s[a + len('<div class="wrap">'): b if b > a else s.rfind("</main>")]
    tree = Tree()
    tree.feed(body)
    lines = blocks(tree.root)
    nav = []
    if b > a:
        t2 = Tree()
        t2.feed(s[b: s.find("</div>", b) + 6])
        links = list(iter_tags(t2.root, "a"))
        if links:
            nav = ["---", "", " · ".join(inline(x) for x in links), ""]
    md = "\n".join(lines + nav)
    md = re.sub(r"\n{3,}", "\n\n", md).strip() + "\n"
    return md


def main():
    langs = sys.argv[1:] or ["ko", "en"]
    for lang in langs:
        src, dst = ROOT / f"html-{lang}", ROOT / f"md-{lang}"
        if not src.exists():
            sys.exit(f"build-md: {src.name}/ 이 없다 --- build-html.sh 를 먼저 돌린다")
        if dst.exists():
            shutil.rmtree(dst)
        dst.mkdir()
        n = 0
        for page in sorted(src.glob("*.html")):
            name = "README.md" if page.name == "index.html" else page.stem + ".md"
            (dst / name).write_text(page_md(page), encoding="utf-8")
            n += 1
        print(f"build-md: md-{lang}/ — {n} 쪽")


if __name__ == "__main__":
    main()

#!/bin/sh
# 영어판 PDF → build/book-en.pdf (예제는 한국어판과 같은 트리를 쓴다)
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
[ -x "$typst" ] || { echo "build-book-en: typst 를 못 찾았다: $typst" >&2; exit 1; }
[ -d "$root/build/examples-out" ] || sh "$root/scripts/verify-examples.sh"
python3 "$root/scripts/check-headings.py"
python3 "$root/scripts/check-chapter-openings.py"
mkdir -p "$root/build"
log="$root/build/typst-en.log"
"$typst" compile --input lang=en --root "$root" --font-path "$fonts" \
    "$root/book-en/main.typ" "$root/build/book-en.pdf" >"$log" 2>&1 || { cat "$log" >&2; exit 1; }
cat "$log"
if grep -q "unknown font family" "$log"; then
    echo "build-book-en: 글꼴을 찾지 못했다 ($fonts) — 대체 글꼴 PDF 는 버린다" >&2
    rm -f "$root/build/book-en.pdf"; exit 1
fi
echo "build-book-en: build/book-en.pdf"

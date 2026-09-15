#!/bin/sh
# 웹판: web/ko/, web/en/ (장마다 한 쪽). GitHub Pages 가 저장소의 docs/ 를 내보내면
# 주소는 …/lowent_lang/book/web/ko/ 이다.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
[ -d "$root/build/examples-out" ] || sh "$root/scripts/verify-examples.sh"
build_one() {
    lang=$1; src=$2
    out="$root/web/$lang"
    mkdir -p "$out"
    "$typst" compile --root "$root" --font-path "$fonts" \
        --features html --format html \
        --input mode=html --input "lang=$lang" \
        "$src" "$out/book.html" 2>"$root/build/typst-html-$lang.log" || { cat "$root/build/typst-html-$lang.log" >&2; exit 1; }
    python3 "$root/scripts/wrap-html.py" "$lang" "$out/book.html" "$out"
    rm -f "$out/book.html"
}
mkdir -p "$root/build"
build_one ko "$root/book/main.typ"
build_one en "$root/book-en/main.typ"
python3 "$root/scripts/check-editions.py"
if [ "${WEBFONTS:-1}" = 1 ]; then bash "$root/scripts/make-webfonts.sh"; fi

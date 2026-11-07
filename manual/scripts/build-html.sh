#!/bin/sh
# 웹판: html-ko/ · html-en/ (장마다 한 쪽, 글꼴은 각 폴더의 fonts/ 에 함께 싣는다).
# GitHub Pages 가 저장소의 docs/ 를 내보내면 주소는 …/lowent_lang/manual/html-ko/ 이다.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
[ -d "$root/build/examples-out" ] || sh "$root/scripts/verify-examples.sh"
build_one() {
    lang=$1
    out="$root/html-$lang"
    rm -rf "$out"; mkdir -p "$out"
    "$typst" compile --root "$root" --font-path "$fonts" \
        --features html --format html \
        --input mode=html --input "lang=$lang" \
        "$root/typst-$lang/main.typ" "$root/build/manual-$lang.html" 2>"$root/build/typst-html-$lang.log" || { cat "$root/build/typst-html-$lang.log" >&2; exit 1; }
    python3 "$root/scripts/wrap-html.py" "$lang" "$root/build/manual-$lang.html" "$out"
}
mkdir -p "$root/build"
build_one ko
build_one en
python3 "$root/scripts/check-editions.py"
if [ "${WEBFONTS:-1}" = 1 ]; then bash "$root/scripts/make-webfonts.sh"; fi

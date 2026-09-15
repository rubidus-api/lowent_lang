#!/usr/bin/env bash
# GitHub Pages 를 다시 굽고, 웹 판이 그 판 번호를 내주는지 확인한다.
#
#   scripts/pages-build.sh [--version vX.Y.Z]
#
# ★ Pages 는 저장소 main 가지의 docs/ 폴더를 내보낸다(docs/index.html · docs/.nojekyll).
#   책은 그 아래 book/web/ko/ · book/web/en/ 이다. 웹 판은 커밋되어 있어야 하므로
#   원고를 고치면 build-html.sh(글꼴까지)를 돌려 web/ 을 함께 커밋한 뒤 푸시한다.
#
# ★ 푸시만으로 Pages 가 다시 구워진다고 믿지 않는다 --- proven_c_book 에서 빌드가
#   옛 커밋에 멈춘 채 새 판을 내주지 않은 일이 여러 번 있었다. 그래서 빌드를 청하고,
#   끝날 때까지 기다리고, *살아 있는 페이지*에서 판 번호를 읽는다.
#
# 인증서는 저장소 밖에 둔다. 자리는 GH_TOKEN_FILE 로 준다 --- 이 기계의 절대 경로를
# 공개 저장소에 적지 않는다.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
repo=rubidus-api/lowent_lang
site=https://rubidus-api.github.io/lowent_lang/book/web
ver=""

while [ $# -gt 0 ]; do
  case "$1" in
    --version) shift; [ $# -gt 0 ] || { echo "pages-build: --version 뒤에 판 번호" >&2; exit 2; }; ver=$1 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) printf 'pages-build: 모르는 선택지: %s\n' "$1" >&2; exit 2 ;;
  esac
  shift
done

command -v curl >/dev/null 2>&1 || { echo "pages-build: curl 이 필요하다" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "pages-build: python3 이 필요하다" >&2; exit 1; }

# 판 번호를 주지 않으면 원고에서 읽는다 --- 단일 소스는 book/main.typ 이다.
if [ -z "$ver" ]; then
  ver=$(sed -n 's/^#let book-version = "\([^"]*\)".*$/\1/p' "$root/book/main.typ")
fi
[ -n "$ver" ] || { echo "pages-build: 판 번호를 알 수 없다" >&2; exit 1; }

tokfile=${GH_TOKEN_FILE:-}
[ -n "$tokfile" ] && [ -f "$tokfile" ] || { echo "pages-build: 인증서 파일을 GH_TOKEN_FILE 로 준다" >&2; exit 1; }
tok=$(tr -d '\n\r ' < "$tokfile")

api() { curl -sS -H "Authorization: token $tok" -H "Accept: application/vnd.github+json" "$@"; }
field() { python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('$1') or '')"; }

echo "pages-build: $ver --- 빌드를 청한다"
api -X POST "https://api.github.com/repos/$repo/pages/builds" >/dev/null

i=0
while [ "$i" -lt 60 ]; do
  status=$(api "https://api.github.com/repos/$repo/pages/builds/latest" | field status)
  case "$status" in
    built|errored) break ;;
  esac
  i=$((i + 1))
  sleep 5
done

latest=$(api "https://api.github.com/repos/$repo/pages/builds/latest")
status=$(printf '%s' "$latest" | field status)
commit=$(printf '%s' "$latest" | field commit)
head=$(cd "$root" && git rev-parse HEAD)

if [ "$status" != "built" ]; then
  echo "pages-build: 빌드가 끝나지 않았다 (status=$status) --- Pages 가 켜져 있는지 본다" >&2
  exit 1
fi
printf 'pages-build: 빌드 완료 (commit %s)\n' "$(printf '%s' "$commit" | cut -c1-8)"
if [ "$commit" != "$head" ]; then
  echo "pages-build: ★ 구운 커밋이 HEAD 가 아니다 --- 먼저 푸시했는지 본다" >&2
  exit 1
fi

tries=${PAGES_POLL:-40}
ok=0
for lang in ko en; do
  j=0
  while [ "$j" -lt "$tries" ]; do
    # 받아 두고 나서 본다(파이프로 grep -q 에 넘기면 curl 이 23 으로 끝난다).
    page=$(curl -sS "$site/$lang/index.html?cachebust=$$-$j" || true)
    case "$page" in
      *"$ver"*)
        printf '  %s : %s\n' "$lang" "$ver"
        ok=$((ok + 1))
        break ;;
    esac
    j=$((j + 1))
    sleep 5
  done
done

if [ "$ok" -ne 2 ]; then
  echo "pages-build: ★ 웹 판이 아직 $ver 을 내주지 않는다" >&2
  exit 1
fi
echo "pages-build: 웹 판 두 쪽이 $ver 을 내준다"

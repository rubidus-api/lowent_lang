#!/bin/sh
# ★ 공개 저장소만으로 도는 짧은 회귀 시험 (`make check`).
#   큰 회귀 시험(골든)은 개발 저장소에 있고, 이 목록이 거기서도 돈다 — 목록이 어긋나면 개발 쪽이 먼저 실패한다.
#   ① 표준 라이브러리 lib/*.low 전부가 `--check` 를 통과한다
#   ② tests/smoke/run.tsv 의 op 마다 VM 과 네이티브가 **같은 기대값**을 낸다
#   ③ tests/smoke/err_*.low 가 첫 `rem expect:` 줄의 진단 코드로 거절되고, 그 줄이 `파일.low:줄:열` 로 시작한다
# 실행: impl/ 에서 `make check` (또는 `sh tests/smoke.sh`). 필요한 것: 지어진 build/lowentc 와 C 컴파일러(CC, 기본 cc).
set -u
BIN=build/lowentc
CC=${CC:-cc}
[ -x "$BIN" ] || { echo "smoke: $BIN 이 없다 — 먼저 make"; exit 2; }
W=$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/lowent-smoke.$$")
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT INT TERM
pass=0; fail=0
ok()  { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "  [FAIL] $*"; }

# ① 표준 라이브러리
for f in ../lib/*.low; do
  if "$BIN" --check "$f" </dev/null 2>&1 | grep -q 'check: ok'; then ok; else bad "lib: $f 가 --check 를 통과하지 않는다"; fi
done

# ② VM ≡ 네이티브 ≡ 기대값
TAB=$(printf '\t')
while IFS="$TAB" read -r file op args want; do
  case "$file" in ''|'#'*) continue ;; esac
  # 인자는 공백으로 가른다(목록에 따옴표가 필요한 인자는 없다)
  # shellcheck disable=SC2086
  # ★ 이 표는 **VM 과 네이티브가 같은 답을 내는가**를 잰다. 네이티브는 검사를 거치지 않으므로
  #   VM 쪽도 같은 조건에서 돌린다(`--unchecked`). 2026-09-16 부터 `--run` 은 기본으로
  #   `--check` 와 같은 검사를 돌려 거절하므로(결함 노트 #86), 일부러 거절당하는 픽스처가
  #   이 표에 있으면 그 문을 열어야 한다. 2026-10-04 부터 `--emit-c` 도 같은 검사를 돌리므로
  #   C 를 내는 쪽도 같은 문을 연다.
  got=$("$BIN" --run "$op" --unchecked "$file" $args </dev/null 2>&1 | tail -n 1 | sed -n 's/.*= \(-\{0,1\}[0-9][0-9]*\)$/\1/p')
  [ "$got" = "$want" ] || { bad "vm: $file $op $args → '$got' (기대 $want)"; continue; }
  exe="$W/$(echo "$file" | tr '/.' '__')"
  if [ ! -x "$exe" ]; then
    "$BIN" --emit-c --unchecked "$file" > "$exe.c" 2>/dev/null && "$CC" -O2 -w -o "$exe" "$exe.c" -lm -lpthread 2>/dev/null \
      || { bad "native: $file 을 C 로 내거나 컴파일하지 못했다"; continue; }
  fi
  # shellcheck disable=SC2086
  got=$("$exe" "$op" $args </dev/null 2>&1 | tail -n 1 | sed -n 's/.*= \(-\{0,1\}[0-9][0-9]*\)$/\1/p')
  [ "$got" = "$want" ] && ok || bad "native: $file $op $args → '$got' (기대 $want)"
done < tests/smoke/run.tsv

# ③ 거절되어야 하는 프로그램
for f in tests/smoke/err_*.low; do
  want=$(sed -n 's/^rem expect: \([A-Z0-9-]*\).*/\1/p' "$f" | head -n 1)
  out=$("$BIN" --check "$f" </dev/null 2>&1)
  base=$(basename "$f")
  if echo "$out" | grep -q "$base:[0-9][0-9]*:[0-9][0-9]* $want:"; then ok; else bad "error: $f 가 $want 로 거절되지 않는다: $(echo "$out" | grep -m1 -oE '[EW]-[A-Z0-9-]+')"; fi
done

echo "smoke: 통과 $pass · 실패 $fail"
[ "$fail" -eq 0 ]

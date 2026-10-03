# 예제 — 이진 탐색 (pure · 무할당 · 술어 계약)

RPN(stateful·region·alloc)과 대비되는 **순수 알고리즘**: region/스택/effect 없음, `option` 반환,
**계약이 사용자 술어 op 를 참조**(`requires sorted xs`)하는 패턴을 보여준다. O(log n)·`effects none`. (MVP 문법)

```lowent
module sorted_search .

rem 오름차순 정렬 여부 — 계약용 술어 op(순수).
fn sorted
  input xs slice u32 .
  output bool .
  access xs sequential .
do
  var i be u64 1 .
  while lt i len xs . do
    if gt (idx xs (sub i 1)) (idx xs i) . do
      return false .
    end
    set i add i 1 .
  end
  return true .
end

rem 정렬된 slice 에서 target 의 인덱스(없으면 none). O(log n)·무할당.
rem 비용 가시: effects none(순수) · access random(이진 탐색은 비순차 접근).
fn bsearch
  input xs slice u32 .
  input target u32 .
  output option u64 .
  access xs random .
  requires sorted xs . rem ★ 술어 op 를 계약으로 — debug 검사 / release assume(RFC-0008 등급)
do
  var lo be u64 0 .
  var hi be u64 len xs . rem 반개구간 [lo, hi)  (슬라이스 규약과 동일, G4)
  while lt lo hi . do
    let mid be u64 add lo div sub hi lo . 2 . rem overflow-안전 중점
    let v be u32 idx xs mid .
    if eq v target . do return some mid . end
    if lt v target . do
      set lo add mid 1 .
    end
    else do
      set hi mid .
    end
  end
  return none .
end
```

## RPN 예제와의 대비 (다른 성격)

```text
                 rpn(eval)                     bsearch
op kind          proc                          calc(순수)
메모리           scratch region + stack(alloc) 없음 — 무할당(effects none)
실패             result eval_error             option(없음=none) — 오류 아님
계약             requires(len>0)               requires(sorted xs) = ★술어 op 참조
접근             access sequential             access random
비용             O(n)                          O(log n)
제어             for + if/else + guard         while + if/else + return some/none
```

## 보이는 Lowent 특징

```text
계약=술어 op      `requires sorted xs` — 사용자 fn 를 전제로. debug 빌드는 호출해 검사(O(n)),
                 release 는 assume(제거) — *계약 등급*(RFC-0008 static/debug/assume)의 실전 모습.
탈출이 곧 답      찾으면 그 자리에서 `return some mid`, 구간이 비면 `return none` — 값을 내는 loop 는 없다(2026-09-13).
무할당 가시      effects none ⟹ 시그니처만으로 "힙 0·순수" 보증. access random 으로 패턴도 노출(P2).
option           "찾음/없음" 은 *오류가 아니라 부재* → result 아닌 option(RFC-0006 채널 분업).
반개구간 일관     [lo, hi) — range·subslice·슬라이스 인덱싱과 같은 규약(G4).
```

> 두 예제(rpn=stateful/alloc/result, bsearch=pure/무할당/option)가 Lowent 의 두 결을 보여준다 —
> *명시 자원·계약*은 공통, *비용·실패 채널*은 작업 성격에 맞게 달라진다("비용 균일성 거부").

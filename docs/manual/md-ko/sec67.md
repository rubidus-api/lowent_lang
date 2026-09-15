# <a id="mod-sortlib"></a>`sortlib` — 제자리 quicksort

소스

`lib/sort.low`

층

L0 — 순수 계산

권한

없음

`u64` 슬라이스를 **제자리에서** 오름차순으로 정렬한다. 이진 탐색 전에([`searchlib`](sec69.md#mod-searchlib)), 결과를 순서대로 보여 줄 때 쓴다. 공개 op 은 `sort` 하나다.

```lowent
use sortlib .

sortlib.sort s .
```

> **성숙도 `standard` — 이 저장소의 첫 `standard`**
>
> > 지금 있는 표면(`sort` 하나)을 **지킨다**. 이름 · 인자 · 의미가 바뀌려면 호환성 결정이 따로 든다. 더 자라지 않는다는 말은 아니다 — 비교자와 다른 원소 타입은 아직 없고, 생긴다면 **더하기**로 생긴다. 그리고 **성능은 약속하지 않는다** — 지금은 quicksort 다. 다른 원소 타입은 [`sortgen`](sec68.md#mod-sortgen) 이 맡는다.

정렬은 거의 모든 도구가 필요로 하는 **순수 계산**이라 세상에 닿지 않는다. 그러므로 빌트인이 아니라 라이브러리다 — Lowent 로 쓸 수 있으면 라이브러리다. 빌트인 증가 0 으로 지어졌다.

| **op** | **모양** | **실패** |
|---|---|---|
| `sort` | `proc (s mut slice u64) → void`, `effects none` — 제자리 오름차순 | 없음(빈 슬라이스 · 한 원소도 무동작) |
| `qsort`(내부) | `proc (s mut slice u64, lo0 u64, hi0 u64) → void` | export 가 아니다 — 밖에서 부르면 `E-VISIBILITY` |

*표 50.1 — `sortlib` 의 op*

**깊이가 O(log n) 으로 묶인다 — 이것이 이 구현의 요점이다.** 순진한 Lomuto 분할은 이미 정렬된 입력에서 피벗이 늘 끝이라 재귀가 O(n) 깊어지고, 네이티브 깊이 상한을 넘길 수 있다. 그래서 **작은 쪽을 재귀하고 큰 쪽을 루프한다**. 재귀에는 늘 절반 이하로 줄어드는 쪽만 쌓이므로 log n 을 넘지 못한다. 정확성이 아니라 자원 안전을 위한 것이다 — 느려질 수는 있어도 터지지는 않는다. 내부 `qsort` 는 반열린 구간 `[lo0, hi0)` 를 맡고, `hi0 > len s` 이거나 `lo0 >= hi0` 이면 아무것도 하지 않는다 — 뒤집힌 구간을 줘도 경계 밖을 만지지 않으므로 VM 과 네이티브가 같은 답(무동작)을 낸다.

**새 배열을 돌려주지 않는 이유.** 그러면 할당이 필요하고, 할당은 이 계층이 하지 않는 일이다. 비교자를 받지 않는 것도 같은 이유다 — 다른 순서가 필요하면 값을 미리 변환해 넣는다.

정렬 결과를 스스로 검증하는 모양 — 오름차순인가 **그리고** 합이 보존됐는가. 순서 결함과 원소 소실 · 복제 결함을 함께 잡는다.

```lowent
proc sort_verify input s mut slice u64 . . output u64 . effects none . do
  var pre u64 be 0 .
  var i u64 be 0 .
  while lt i (len s) . do
    set pre (wrap_add pre (index s i)) .
    set i (add i 1) .
  end
  sortlib.sort s .
  var post u64 be 0 .
  var sorted u64 be 1 .
  var j u64 be 0 .
  while lt j (len s) . do
    set post (wrap_add post (index s j)) .
    if gt j 0 . do
      if gt (index s (sub j 1)) (index s j) . do set sorted 0 . end
    end
    set j (add j 1) .
  end
  if ne pre post . do set sorted 0 . end
  return sorted .
end
```

> **반례. 부분 구간을 내부 op 으로 정렬한다**
>
> > `sortlib.qsort s 0 (len s)` 는 `E-VISIBILITY` 다. 부분 구간은 `subslice` 로 잘라 `sort` 에 준다 — `sortlib.sort (subslice s 2 7) .`

> **반례. 불변 슬라이스를 넘기거나 반환값을 기대한다**
>
> > `input s slice u64` 를 넘기면 타입 오류다 — 제자리 정렬이라 `mut slice u64` 여야 한다. 원본을 남겨야 하면 복사본을 정렬한다. `let r … be sortlib.sort s` 도 컴파일 에러다 — `output void` 이고 결과는 `s` 자신이다.

**주의.** 입력이 파괴된다. 안정 정렬이 아니다(같은 값끼리의 원래 순서를 보장하지 않는다 — `u64` 라 지금은 관측할 방법이 없지만 키 추출이 생기면 달라진다). 시간은 평균 O(n log n), 최악 O(n²)이고, 추가 메모리는 O(log n) 재귀 프레임뿐이다. **확인하는 것** — 정렬됐는가와 순열인가(시험), 임의 입력에서 VM · 네이티브 일치(차등 시험).

---

[← 이전](sec66.md) · [목차로](README.md) · [다음 →](sec68.md)

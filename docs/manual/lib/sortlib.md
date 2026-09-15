# sortlib — 제자리 quicksort

소스: `lib/sort.low` · 모듈명 `sortlib` (열두 번째 표준 라이브러리)

> ## ★ 성숙도: **`standard`** — 이 저장소의 **첫 `standard`** (2026-08-16, RFC-0075 부록 A)
>
> **뜻하는 것**: 지금 있는 표면(`sort` 하나)을 **지킨다**. 이름·인자·의미가 바뀌려면 호환성
> 결정이 따로 든다(RFC-0075 §9).
>
> **뜻하지 않는 것**: *더 안 자란다*는 말이 아니다 — 비교자(내림차순·키 추출)와 다른 원소
> 타입은 **아직 없고**, 생긴다면 **더하기**로 생긴다. 그리고 **성능은 약속하지 않는다**:
> 지금은 quicksort 이며, 그 사실과 깊이 논증은 아래에 있다.

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** u64 슬라이스를 **제자리에서 정렬**한다.

**언제 쓰나.** 이진 탐색 전에 정렬해야 할 때, 결과를 순서대로 보여 줄 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use sortlib .

sortlib.sort s .                            rem s 를 오름차순으로 바꾼다(새 배열을 만들지 않는다)
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **`u64` 슬라이스를 오름차순으로 줄 세울 때** 쓴다. op 은 사실상 `sortlib.sort`
하나다 — 슬라이스를 주면 그 자리에서 정렬돼 돌아온다.

거의 모든 도구가 정렬을 필요로 한다. 그리고 정렬은 **순수 계산**이라 세상(파일·시계·난수)에
안 닿는다 — 그러므로 리프(빌트인)가 아니라 **라이브러리**다(리프 규칙, SPEC-002 부록 P):
Lowent 로 쓸 수 있으면 라이브러리다. 빌트인 op 증가 **0** 으로 지어졌다.

알고리즘은 quicksort 다: 기준값(피벗) 하나를 골라 "피벗 이하 / 초과" 로 가르고, 갈라진 두
쪽을 각각 다시 정렬하는 재귀 알고리즘이다. 평균적으로 빠르고 추가 메모리가 거의 없어
제자리 정렬의 표준 선택지다.

## 설계 의도와 경계

핵심은 "제자리" 와 "재귀 깊이 상한" 두 가지다.

- **제자리(in-place) 오름차순** quicksort 하나다. 제자리란 입력 슬라이스 안에서 원소를
  맞바꿔 가며 정렬한다는 뜻이다 — 복사본을 만들지 않는 대신 원본 순서가 사라진다.
- **깊이가 O(log n) 으로 묶인다** — 이것이 이 구현의 요점이다. 순진한 Lomuto 분할은 이미
  정렬된 입력에서 재귀가 O(n) 깊어져(피벗이 늘 끝) 네이티브 깊이 상한(RFC-0073 D4)을
  넘길 수 있다 — 재귀 호출 하나하나가 스택을 먹기 때문이다. 그래서 **작은 쪽을 재귀하고
  큰 쪽을 루프한다**(꼬리재귀 제거). 재귀에는 늘 절반 이하로 줄어드는 쪽만 쌓이므로
  log n 을 넘지 못한다. 정확성이 아니라 **자원 안전**을 위한 것이다.
- 정직하게 안 하는 것(소스가 적어 둔 후속): 지금은 `u64` 오름차순 **하나**다. 비교자
  (내림차순·키 추출)도, 다른 원소 타입도 없다 — 그것이 생기는 날 언어의 고차 op 인자
  전달이 얼마나 매끄러운지 드러난다.
- 안정(stable) 정렬이 아니다. 안정 정렬이란 같은 값끼리의 원래 상대 순서를 보존하는
  정렬인데, quicksort 는 그것을 보장하지 않는다(`u64` 라 지금은 관측할 방법도 없지만,
  키 추출이 생기면 달라진다).

## 자료구조

선언된 자료구조가 없다 — 상태를 들 필요가 없는 순수 계산이기 때문이다.

struct·actor 선언이 없다. 입력은 호출자의 `mut slice u64` 하나다.

## op 한눈에

공개된 것은 `sort` 하나이고, `qsort` 는 그 속을 이루는 내부 재귀다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `sort` | proc (export) | `s mut slice u64` → `void` — 제자리 오름차순 정렬 | 실패 없음 |
| `qsort` | proc (내부, export 아님) | `s mut slice u64`, `lo0 u64`, `hi0 u64` → `void` | 잘못된 구간이면 무동작 |

둘 다 `effects none .` — 순수 계산이다.

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** `s` 는 **제자리에서 바뀌는** 슬라이스라 `mut` 이다.
새 배열을 만들어 돌려주지 않는 이유는 그러면 할당이 필요하고, 할당은 이 계층(L0)이 하지 않는
일이기 때문이다. 비교자(comparator)를 받지 않는 것도 같은 이유다 — u64 오름차순 하나만 한다.
다른 순서가 필요하면 값을 미리 변환해 넣는 것이 이 모듈의 답이다.


두 op 을 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
정렬은 남의 슬라이스를 제자리에서 바꾸는 일이라 인자가 곧 작업 대상이다.

### sort

공개 진입이다. 슬라이스 전체를 제자리에서 오름차순으로 정렬한다.

```lowent
export proc sort input s mut slice u64 . . output void . effects none .
```

- `s` — 정렬할 슬라이스. 제자리 정렬이라 원소를 맞바꿔야 하므로 `mut` 이어야 한다.
- 반환값 없음(`void`) — 결과는 `s` 자신이다. 빈 슬라이스·한 원소 슬라이스도 그대로
  받는다(무동작).
- 내부적으로 `qsort s 0 (len s)` 를 부른다.

### qsort (내부)

재귀 본체다. 직접 부를 일은 없지만 동작을 알아 두면 성질이 보인다.

```lowent
proc qsort input s mut slice u64 . input lo0 u64 . input hi0 u64 . output void . effects none .
```

- `lo0`·`hi0` — 정렬할 `[lo0, hi0)` 반열린 구간(시작은 포함, 끝은 제외). 재귀가 "이
  구간만 맡는다" 를 표현하려면 경계 두 개가 필요하다.
- Lomuto 분할, 마지막 원소가 피벗. 분할 후 작은 쪽만 재귀하고 큰 쪽은 루프로 잇는다.
- 방어: `hi0 > len s` 이거나 `lo0 >= hi0` 이면 **아무것도 안 한다.** 뒤집힌·범위 밖
  구간을 줘도 경계 밖을 안 만진다(그래야 VM 과 네이티브가 같은 답 — 무동작 — 을 내고
  diff-sweep 이 지켜진다).
- `export` 가 아니므로 다른 모듈에서는 부를 수 없다. 공개 진입은 `sort` 하나다.

## 사용법과 예제

기본 사용은 한 줄이다: `sortlib.sort s .` — 그 뒤 `s` 를 읽으면 정렬돼 있다.

```lowent
use sortlib .           rem from-생략 std 해소
use sortlib as srt .    rem 별칭 관례
```

정렬하고 k 번째 원소를 읽는다(`impl/tests/vm_sort.low` 의 `sorted_at`):

```lowent
proc sorted_at input s mut slice u64 . . input k u64 . output u64 . effects none . do
  sortlib.sort s .                     rem 제자리 정렬 — s 가 이 줄에서 바뀐다
  guard lt k (len s) . else return 0 . rem k 가 범위 안인지 먼저 확인
  return index s k .                   rem 정렬된 s 의 k 번째 = k+1 번째로 작은 값
end
```

정렬 결과를 스스로 검증하는 패턴 — 오름차순인가 **그리고** 합이 보존됐는가(순서 버그와
원소 소실·복제 버그를 함께 잡는다). `vm_sort.low` 의 `sort_verify` 그대로다:

```lowent
proc sort_verify input s mut slice u64 . . output u64 . effects none . do
  var pre u64 be 0 .
  var i u64 be 0 .
  while lt i (len s) . do              rem ① 정렬 전 합을 기억해 둔다
    set pre (wrap_add pre (index s i)) .
    set i (add i 1) .
  end
  sortlib.sort s .                     rem ② 정렬
  var post u64 be 0 .
  var sorted u64 be 1 .
  var j u64 be 0 .
  while lt j (len s) . do              rem ③ 정렬 후 합과 오름차순을 함께 확인
    set post (wrap_add post (index s j)) .
    if gt j 0 . do
      if gt (index s (sub j 1)) (index s j) . do set sorted 0 . end   rem 앞이 크면 실패
    end
    set j (add j 1) .
  end
  if ne pre post . do set sorted 0 . end   rem 합이 변했다 = 원소가 사라지거나 복제됐다
  return sorted .                          rem 1 = 통과
end
```

VM 으로 바로 확인할 수 있다(슬라이스 인자는 `[a,b,c]`):

```sh
$ build/lowentc --run sorted_at demo.low [3,1,2] 0
1
```

## 반례 — 이렇게 쓰면 안 된다

각 반례에 **증상**을 적었다. 이 모듈의 반례는 셋 다 컴파일 단계에서 걸리므로 실행까지
가지 않는다 — 정렬 자체는 실패하지 않기 때문이다.

```lowent
rem ✗ 내부 op 을 밖에서 부른다
module bad_reach .
use sortlib from "../lib/sort.low" .
proc f input s mut slice u64 . . effects none . do
  sortlib.qsort s 0 (len s) .
end
```

증상: **컴파일 에러 E-VISIBILITY** — `qsort` 는 export 가 아니다. 부분 구간 정렬이
필요하면 `subslice` 로 잘라 `sort` 에 준다:

```lowent
sortlib.sort (subslice s 2 7) .    rem [2, 7) 만 정렬
```

```lowent
rem ✗ 불변 슬라이스를 넘긴다
fn g input s slice u64 . output u64 . do
  sortlib.sort s .
```

증상: **컴파일 타입 오류** — 제자리 정렬이라 `mut slice u64` 여야 한다. `ref` 로 감싼
값을 넘겨도 같은 부류다(E-TYPE-ARGMUT). 원본을 남겨야 하면 복사본을 만들어 그쪽을
정렬한다 — 이 라이브러리는 복사를 안 해 준다.

```lowent
rem ✗ 반환값을 기대한다
let r ??? be sortlib.sort s .
```

증상: **컴파일 에러** — `output void` 라 값 자리에 놓을 것이 없다. 정렬 결과는 `s`
자신이다: `sortlib.sort s .` 를 문장으로 부르고 그 뒤 `s` 를 읽는다.

## 주의사항

전부 "제자리 quicksort" 라는 선택에서 따라 나오는 성질들이다.

- **입력이 파괴된다.** 정렬 전 순서가 필요하면 먼저 복사한다.
- 안정 정렬이 아니다(위 설계 절 참조).
- 시간은 평균 O(n log n), 최악 O(n²)(quicksort 의 본성). 다만 **재귀 깊이는 최악에도
  O(log n)** 이다 — 이미 정렬된 입력·같은 값 반복 입력을 줘도 깊이 상한(E-VM-DEPTH 부류)
  걱정 없이 돈다. 느려질 수는 있어도 터지지는 않는다.
- 추가 메모리 0(제자리 + O(log n) 재귀 프레임).
- 오라클 안이다: 순수 slice 조작이라 VM 과 네이티브가 같은 바이트를 낸다. cap 이 없는
  순수 op 이라 차등 퍼저(diff-sweep)가 임의 입력에서 VM≡native 를 지키고, 골든이 정확성
  (실제로 정렬됐는가 + 순열인가)을 지킨다. `sort_verify` 패턴을 그대로 가져다 자기
  코드의 자기검증에 써도 된다.

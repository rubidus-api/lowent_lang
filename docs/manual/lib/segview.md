# segview — 조각 뷰의 커서·총길이·펴기

소스: `lib/segview.low` · 모듈명 `segview` (RFC-0104 §8-8, 소유자 서명 2026-08-29 · WO-0142)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 언어가 든 것은 셋뿐이다 — `view_segments`(짓기) · `segs`(조각 수) ·
`seg`(i 번째 조각). 이 모듈은 그 위에 **커서 · 총길이 · 펴기(coalesce)** 를 얹는다. 셋 다
앞의 셋으로 쓰이므로 내장 연산이 될 이유가 없다 — 그래서 **표면은 하나도 안 늘었다**.

**왜 `segments` 가 언어에 있나.** 오늘 `slice (slice u8)` 은 선언만 되고 **원소를 못 꺼낸다**
(`E-TYPE-LET`). 조각 여럿을 한 값으로 못 들면, 밖으로 내보낼 때 **조각마다 한 번씩** 나가야
한다. 64 KiB 를 64 조각으로 내보낼 때 그 차이가 **28.3 배**다(`writev` 1 회 11.98 ms 대
`write` 64 회 339.38 ms).

**최소 예제.**

```lowent
use segview .

rem 서술자는 평평한 `slice u64` 에 (at, n) 쌍이다 — 새 구조체 타입은 없다.
set (index d 0) 0 .  set (index d 1) 4 .      rem 조각 ① back[0..4]
set (index d 2) 8 .  set (index d 3) 4 .      rem 조각 ② back[8..12]
let ss be view_segments back d .

let n u64 be segview.total_len ss .                rem 총길이 = 8 (서술자만 읽는다)
let b option u8 . be segview.byte_at ss 5 .    rem 평평한 자리 5 = 둘째 조각의 1 번
let w option u64 . be segview.coalesce ss out . rem 한 덩어리로 편다
```

## 비용이 op 이름에 적혀 있다

| 부르는 것 | 무엇을 만지나 | 값 |
|---|---|---|
| `segs` · `seg` | 서술자만 | 조각당 약 **0.24 ns**, **복사 0** |
| `segview.total_len` | 서술자만 | O(조각 수) |
| `segview.seek` · `byte_at` | 서술자만 | O(조각 수) — 조각을 훑어 자리를 찾는다 |
| `segview.coalesce` | **바이트 전부** | O(n) — **여기서만 복사가 일어난다** |

★ 그것이 이 설계의 요점이다: 순회는 조각을 **빌리기만** 하고, 한 덩어리로 펴는 값은
**부를 때만** 치른다. 비용이 부르는 자리에 보인다 — 이 저장소가 `slice` 에 대해 지켜 온
규율 그대로다.

## 알아 둘 것

- **저장은 호출자 것이다.** `coalesce` 는 받은 버퍼에 쓰고 **쓴 길이**를 답한다. 버퍼가
  모자라면 `none` 이다 — 잘라 쓰지 않는다.
- **없는 자리를 가리키는 커서를 만들지 않는다.** `seek` 이 끝을 넘어가면 `none` 이다.
- **조각은 곧 `iovec` 이다.** 방출 C 의 슬라이스가 `struct iovec` 과 크기도 두 오프셋도
  같다(골든이 매 실행 대조한다) ⇒ `extern` 경계에서 조각 배열을 넘기는 값은 **캐스트**이지
  복사가 아니다.

## ★ 이름 하나가 남의 모듈을 아프게 했다

처음에 이 op 의 이름은 `total` 이었다. 그러자 `check-lib-pairs` 가 **아픈 쌍 2**(segview+tls13 ·
segview+tlssrv)를 냈다 — `tls13` 안의 **지역 변수** `let total u64 be …` 와 부딪혀, 엉뚱하게도
`segview` 쪽 `guard` 가 *"else 가 안 떠난다"*(`E-GUARD-FALLTHROUGH`)로 잡혔다.

☞ 그래서 `total_len` 으로 바꿨다. **흔한 낱말을 export 하지 않는다** — 라이브러리는 혼자
초록인 것으로 충분하지 않고 **조합되어야** 쓸 수 있다. (진단이 이름 충돌을 이름 충돌로
말하지 않은 것은 별개의 문제이고, 백로그로 넘겼다.)

## 안 지은 것

가변 원소 폭(`segments T` 의 T 는 오늘 스칼라 슬라이스다) · `coalesce` 의 무복사 최적화 ·
`readv` 쪽 짝(§8-8 은 내보내기만 쟀다).

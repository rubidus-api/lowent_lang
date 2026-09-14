# lifemode · lifeatom — 값이 언제 끝나는가

소스: `lib/lifemode.low` · `lib/lifeatom.low` (RFC-0104 §8-9, 소유자 서명 2026-08-29 · WO-0143)

## 처음 쓰는 사람에게

**무엇을 고르는 모듈인가.** 값의 끝을 정하는 방법은 넷이고, 넷 다 값이 다르다.

| 모드 | 무엇으로 쓰나 | 값 |
|---|---|---|
| ① 주인이 하나(unique) | **`owned`** — 낱말 하나면 끝이다 | 셈이 **없다** |
| ② 스레드-국한 refcount | `lifemode.near_*` — **그냥 더하기** | 원자 명령 **0** |
| ③ 원자 공유 refcount | `lifeatom.rc_*` + `cap atomic` | 원자 명령 **있다**(실측 4) |
| ④ 외부 완료 | 완료권을 **`owned` 표**로 | 셈이 **없다**, 대신 컴파일러가 센다 |

★ **새 낱말도 새 내장 연산도 하나도 안 늘었다.** 언어가 이미 셋을 알고 있었고, 이 모듈은
그것을 쓰기 좋게 묶을 뿐이다.

**최소 예제 — ② 국한.**

```lowent
use lifemode .

guard lifemode.near_open c 0 . else return 1 .      rem 주인 하나(1)로 연다
let a option u64 . be lifemode.near_share c 0 .     rem 2
let b option u64 . be lifemode.near_drop c 0 .      rem 1
rem 0 을 답하면 마지막이었다 — 그때 부른 쪽이 저장을 거둔다.
```

**최소 예제 — ③ 원자.** 모듈이 다르다. 그것이 요점이다(아래 참조).

```lowent
use lifeatom .

let n u64 be lifeatom.rc_share k c .                rem k = cap atomic
rem rc_release 가 0 을 답하면 마지막이었다 — 그 자리에서 acquire 담을 친다.
```

**최소 예제 — ④ 외부 완료.**

```lowent
rem 완료권이 값이다. 완료는 그것을 **먹는다**.
export fn finish output result u64 mymod.late . input t owned mymod.ticket . do … end .
```

두 번 완료하면 `E-OWN-MOVED`, 완료를 잊으면 `E-OWN-INCOMPLETE` — **정확히 한 번**이
라이브러리가 아니라 **언어에서** 나온다. (뒤쪽 규칙은 완료가 **실패할 수 있을 때** 선다.
실패할 수 없는 완료는 자동 해제가 삼킬 것이 없으므로 그냥 버려도 된다.)

## ★ 왜 모듈이 둘인가 — `use` 는 부르는 것이 아니라 내보내는 것만큼 치른다

처음에는 넷을 한 모듈에 넣었다. 그랬더니 **국한 refcount 만 쓰는 프로그램**의 바이너리에
`lock` 접두 명령이 **2 개** 남았다. 까닭: `export` 인 op 은 C ABI 래퍼로 **전역 심볼**을
얻으므로 링커가 버리지 못한다.

| | `lock` 명령 | 크기 |
|---|---:|---:|
| 넷을 한 모듈에 (국한만 쓰는 프로그램) | **2** | 16,520 B |
| 원자를 `lifeatom` 으로 가른 뒤 | **0** | 16,416 B |

⇒ 원자를 들이는 값이 **`use lifeatom` 이라는 한 줄에 보인다**. 비용을 이름에 적는 이
저장소의 규율 그대로다.

## ★★ 넘침 — 포화하고, 그 객체는 영원히 산다

그냥 `atomic_add` 로 세면 상한에서 **조용히 0 으로 감긴다**(네이티브 실행으로 확인했다).
감긴 수는 이르게 0 에 닿아 **살아 있는 객체를 해제**한다 — use-after-free 로 가는 길이다.

그래서 이 모듈은 **포화한다**(Linux `refcount_t` 가 고른 답과 같다): 상한에 닿으면 더
늘리지 않고, 줄이지도 않는다. 그 객체는 안 죽지만 **메모리 안전은 안 깨진다**.

- 값: 명령 **27 → 32**(+19%). `lock` 수는 시도당 **같다**(1) — CAS 고리라 경합할 때만 돈다.
- 국한 쪽도 같은 규칙을 쓴다. 두 모드가 **같은 상한**(`lifemode.ceiling`)을 본다.

## 안 지은 것

진짜 비동기 완료(reactor 는 아직 A1 동기 폴백이다 — **뒷받침 없는 표면은 만들지 않는다**) ·
완료 콜백(호출 시점과 효과를 감춘다) · 약한 참조(weak) · 순환 수집(§8-11 소관).

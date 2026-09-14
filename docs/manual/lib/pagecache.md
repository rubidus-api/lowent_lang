# pagecache — 페이지 id와 고정 커서

소스: `lib/pagecache.low` · 모듈명 `pagecache` (RFC-0104 §8-10, 소유자 서명 2026-08-29 · §5.16)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 페이지를 담아 두는 캐시에서 **오래 사는 이름**(페이지 id)과
**짧게 닿는 권한**(pin)을 가른다. 밖에 저장하는 것은 언제나 id 이고, 바이트를 만지는 동안만
pin 을 든다. SQLite 의 페이지 캐시가 그 모양이다.

**왜 그렇게 나누나.** 페이지는 **옮겨진다**(축출·재배치). 옮겨질 때 고쳐야 할 것이 id→자리 표
하나뿐이면 되지, 여기저기 흩어진 포인터를 찾아다녀야 한다면 그 캐시는 못 쓴다.

**최소 예제.**

```lowent
use pagecache .

newtype db u8 .                                          rem 저장소 브랜드(§8-2)

var c owned pagecache.cache db . be pagecache.open db 16 .
var p pagecache.pin db . be pagecache.acquire db c 42 .   rem 42 번 페이지를 고정한다
rem  이 사이에는 evict 를 부를 수 없다 — 컴파일이 안 된다
var c2 owned pagecache.cache db . be pagecache.release db p .
var c3 owned pagecache.cache db . be pagecache.evict db c2 .
```

> ### ⚠ 고정이 살아 있는 동안 축출은 **컴파일 에러**다
>
> `acquire` 는 캐시 토큰을 **삼킨다**. 그래서 pin 이 사는 동안에는 `evict`·`reset` 에 건넬
> 것이 손에 없다 — `E-OWN-MOVED`(음성 픽스처 `impl/tests/vm_pagecache_pinned.low`).
>
> ★ 그것을 막는 것은 **이 모듈이 아니라 언어**다: `owned` 가 *"토큰은 하나이고 넘기면 내 손을
> 떠난다"* 를 이미 강제한다. 브랜드로 저장소를 봉하고(§8-2), 토큰으로 접근 단위를 가르고(§8-6),
> 여기서 세 번째로 같은 규칙이 새 규약을 **공짜로** 만든다.
>
> ☞ 아래에서 «계약»은 *op 이 자기 입력에 요구하는 조건*(`requires`)이고, «음성 픽스처»는
> **거절되어야** 하는 예제 프로그램이다.

## 상한과 순서 — 그 수가 어디서 왔나

| 무엇 | 정한 것 | 근거(측정) |
|---|---|---|
| 기본 pin 상한 | `machine.cache_line / 8` — x86_64·arm64 **8** · mips_be **4** · cortex_m **1** | PostgreSQL 이 `REFCOUNT_ARRAY_ENTRIES 8` 을 두고 *"64 bytes, about the size of a cache line"* 이라 적었다(`bufmgr.c:144-145`) |
| release 순서 | **LIFO** — 겹친 pin 은 앞선 것을 삼킨다 | SQLite 커서가 장수를 스택으로 든다(`apPage[]`, `BTCURSOR_MAX_DEPTH 20`) |

★ **수 대신 이유를 적었다.** 8 을 박아 두면 캐시라인이 다른 기계에서 틀린다 — 그래서
`machine.cache_line`(§8-7)으로 나눈다. 이 모듈이 그 질의의 **첫 사용자**다.

## op 한눈에

| op | 하는 일 | 실패 시 |
|---|---|---|
| `open` | 캐시 토큰을 만든다 | — |
| `acquire` | 페이지를 고정한다 — **캐시 토큰을 삼킨다** | — |
| `acquire_more` | 겹쳐 고정한다 — 앞선 pin 을 삼킨다(깊이 +1) | — |
| `release` / `release_more` | 놓는다 — 토큰을 돌려준다 | — |
| `evict` / `reset` | 축출·비움. **캐시 토큰을 요구한다** | 고정 중이면 **컴파일 에러** |
| `slot_of` | id 의 현재 자리 | 빈 캐시면 `none` |
| `pin_limit` / `within_limit` | 이 기계의 상한과 그 판정 | — |
| `epoch_of` / `depth_of` / `depth2_of` | 관측 | — |

## 반례 — 이렇게 쓰면 안 된다

```lowent
rem ✗ 고정한 채로 축출한다
var p pagecache.pin db . be pagecache.acquire db c 42 .
var c2 owned pagecache.cache db . be pagecache.evict db c .
```

증상: **컴파일 에러 `E-OWN-MOVED`** — `c` 는 위에서 pin 이 삼켰다. 실행까지 가지 않는다.

```lowent
rem ✗ 겹친 고정을 안쪽부터 놓으려 한다
var a pagecache.pin db . be pagecache.acquire db c 7 .
var b pagecache.pin2 db . be pagecache.acquire_more db a 9 .
var c2 owned pagecache.cache db . be pagecache.release db a .
```

증상: **컴파일 에러** — `a` 는 `b` 안에 있다. 순서를 어길 **길이 없다**(그것이 LIFO 다).

```lowent
rem ✗ 자리를 저장해 두고 나중에 쓴다
let slot u64 be some_value (pagecache.slot_of db c 42) .
rem … 축출이 지나간 뒤 …
rem slot 은 이제 엉뚱한 자리다
```

증상: 에러는 없다 — **틀린 자리를 읽을 뿐이다**. 밖에 저장하는 것은 **id** 여야 한다.
자리는 물어볼 때마다 새로 얻는다(그것이 이 모듈이 id 와 자리를 가른 이유다).

## 주의사항

- **밖에 저장하는 것은 id 다.** 자리도 pin 도 저장하지 않는다 — 둘 다 지금 이 순간의 것이다.
- **상한은 규약이지 강제가 아니다.** `pin_limit` 을 넘겨 겹치면 타입은 통과한다(깊이는 `pin2`
  까지만 짓는다). 그 이상 필요하면 그때 층을 더 쌓되, **그 수를 시험에 드러내라**.
- **IO 완결은 여기 없다.** 페이지를 읽어 오는 일(그리고 그 실패)은 별도 계약이다(RFC-0104 §5.7).
- **축출 정책도 없다.** LRU 같은 것은 정책이고 이 모듈은 규약이다 — 정책은 그 위에 얹는다.

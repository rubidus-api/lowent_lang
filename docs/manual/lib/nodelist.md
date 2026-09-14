# nodelist — 고정 intrusive 목록

소스: `lib/nodelist.low` (RFC-0104 §8-4 · WO-0144, 2026-08-29)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 노드가 링크를 **자기 안에** 든다(intrusive). 목록 쪽에 따로
칸을 잡지 않으므로 넣고 빼는 데 할당이 없다 — Linux 커널의 `list_head` 가 그 모양이다.

위험한 것은 둘뿐이다:

1. 링크된 노드를 **옮기면** 이웃이 가리키던 자리가 썩는다.
2. 링크된 채 **회수하면** 목록이 썩는다.

**★ 그 둘을 언어가 막는다.** 노드에는 *이동권*이 있고, 목록에 넣으면 목록이 그것을
**먹는다**. 그러면 링크된 채 옮기려는 프로그램은 건넬 값이 없어 **컴파일이 거절한다**.

```lowent
use nodelist .
newtype lru u32 .

var s owned nodelist.site lru . be nodelist.nl_open lru 3 .
var h owned nodelist.held lru . be nodelist.nl_link lru s .     rem 이동권을 먹었다
var s2 owned nodelist.site lru . be nodelist.nl_relocate lru s 9 .   rem ✗ E-OWN-MOVED
```

빼면 돌아온다:

```lowent
var s3 owned nodelist.site lru . be nodelist.nl_unlink lru h .
var s4 owned nodelist.site lru . be nodelist.nl_relocate lru s3 1 .  rem ✔
```

## 여러 목록에 동시에 드는 노드

LRU 목록과 해시 버킷에 같은 노드가 든다면 이동권이 하나뿐이라 두 번째 링크가 못 든다.
⇒ **`shard` 토큰으로 쪼갠다**(§8-6): role 마다 조각 하나, **전부 `rejoin` 해야** 이동권이
다시 선다.

```lowent
use shard .
newtype roleset u32 .          rem ★ 노드 브랜드와 **다른** 저장소다 (아래 참조)

var root owned shard.token roleset . be shard.open roleset 2 .
let hs shard.halves roleset . be shard.split_at roleset root 1 .
rem field hs low  = role 0(LRU) · field hs high = role 1(버킷)
```

그것이 곧 RFC 가 요구한 *"모든 role 의 unlink-before-reclaim"* 이다 — 새 규칙이 아니라
**이미 있던 규칙이 그 문장을 말하게** 한 것이다.

> ### ⚠ 브랜드 하나는 저장소 **하나**를 이름한다
> 노드의 자리(`site`)와 role 토큰에 같은 브랜드를 쓰면 `E-BRAND-REUSED` 가 거절한다(§8-2).
> 이 매뉴얼을 쓰다가 실제로 걸렸다 — 컴파일러가 옳았다.

## 목록은 **고리**다

널이 없으므로 끝을 표시할 값이 없다. 대신 **머리로 돌아오면** 한 바퀴다. 혼자인 노드는
자기 자신을 가리키는 고리 하나다.

★ 처음엔 *"자기 자신을 가리키면 끝"* 으로 셌다가 틀렸다: 둘 이상인 고리에서는 아무도
자기를 안 가리키므로 그 조건이 **영영 안 온다**. 규약을 반만 정하면 그런 일이 난다.

## mutable iterator — 순회하면서 뺀다

```lowent
let n u64 be nodelist.nl_walk_cut nx pv 0 2 .   rem 0 부터 돌면서 2 를 뺀다
```

요령 하나: **다음을 먼저 읽는다.** 지금 노드를 빼면 그 `next` 가 자기 자신이 되므로,
빼기 전에 읽어 두지 않으면 순회가 그 자리에서 멈춘다.

## 저장은 호출자 것이다

`nx`(다음) · `pv`(이전) 두 슬라이스를 밖에서 받는다. 링크는 **인덱스**다 — 이 언어에
생 포인터가 없고, 인덱스는 §8-5 의 세그먼트 아레나처럼 **안 움직이는 저장**과 맞물린다.

## 안 지은 것

자동 회수(첫 회수 정책은 **bulk** 다) · 순환 검출 · 동시 순회(§8-9 의 원자 모드가
필요하다) · 노드 재배치(고정이 그 반대다).

# <a id="mod-pool"></a>`pool` — 세대 핸들 블록 풀

소스

`lib/pool.low`

층

L1 — 호출자의 저장

권한

없음

고정 크기 블록을 빌려 주고 돌려받는 풀이다. 핸들에 **세대 번호**가 붙어 옛 핸들을 알아본다. 객체를 만들고 지우기를 되풀이할 때, 이미 지운 것을 실수로 다시 쓰는 결함(use-after-free)을 막고 싶을 때 쓴다. “필요할 때 4 KiB 씩 받아 쓰고, 원하는 시점에 특정 블록을 해제한다” 는 해제 시점이 어휘적이지 않으므로 `region` 으로도 범프로도 안 된다 — 그래서 정적 검사 대신 세대 핸들로 간다(18장, 35장).

```lowent
newtype pa u8 .
let po option (pool.block_pool pa) . be pool.init pa mem gens 4096 .
guard is_some po . else return 1 .
var p pool.block_pool pa . be some_value po .
let h option (pool.handle pa) . be pool.take pa p .
```

> **무엇을 막아 주고, 무엇을 안 막아 주나**
>
> > **막아 준다 ①** — 돌려준 블록에 **옛 핸들로 접근**하는 것. 세대가 달라 거절된다(실행 중 값으로). **막아 준다 ②** — **풀을 섞는 것**. 핸들과 풀이 **브랜드**를 타입으로 들기 때문에 `handle pa` 를 `block_pool pb` 에 넣으면 컴파일 에러 `E-TYPE-INSTANCE` 다. 그리고 `mem` · `gens` 는 `init` 이 봉해 들어 op 이 더 이상 받지 않는다 — 엉뚱한 배열을 건네는 길이 표면에서 사라졌다. 브랜드는 값이 아니라 타입이라 핸들 칸은 늘지 않는다. **막아 주지 않는다** — 브랜드는 선언마다 하나다. 브랜드를 comptime 인자로 받아 `init` 하는 op 을 두 번 부르면 한 브랜드가 풀 둘을 덮는다. 풀 하나에 브랜드 하나를 지키는 것은 당신의 몫이다. 이것은 언어에 내장된 세대 핸들이 아니다 — 평범한 라이브러리이고, 안전은 이 모듈의 규율에서 나온다.

**원리.** 블록마다 세대 번호를 두고, 받을 때 핸들에도 그 번호를 적어 준다. 해제하면 블록의 세대가 올라가고, 그 순간 옛 핸들은 번호가 맞지 않아 자동으로 무효가 된다. **고정 크기**라 해제가 자유 목록에 넣는 것이 전부이고 단편화가 0 이다. 세대는 나란한 배열 `gens` 에 살고(SoA), 검사 비용은 `bytes` 라는 문을 지나는 코드에만 붙는다. **자유 목록 링크는 블록 자기 바이트 안에 산다** — 해제된 블록은 세대가 올라 아무도 닿지 못하므로 그 앞 8 바이트를 장부로 쓰는 것이 공짜다. 풀은 actor 가 아니라 struct 다(actor state 는 슬라이스를 들 수 없다).

| **op** | **모양** | **실패** |
|---|---|---|
| `handle b` · `block_pool b` | 구조체 — 핸들은 `blk` · `len` · `gen`, 풀은 봉인된 `mem` · `gens` 와 커서들 | — |
| `init` | `comptime b, mem mut slice u8, g mut slice u64, bs u64 → option (block_pool b)` | `bs < 8` 이면 `none` |
| `blocks` | `fn (comptime b, p) → u64` — `min(len mem / bs, len g)` | 없음 |
| `take` | `(comptime b, p mut block_pool b) → option (handle b)` | 블록이 없으면 `none` |
| `release` | `(comptime b, p mut, h handle b) → bool` | 낡은 · 범위 밖 핸들(이중 해제 포함)이면 `false` |
| `bytes` | `(comptime b, p, h) → option mut slice u8` — 바이트에 닿는 유일한 문 | 낡은 · 범위 밖이면 `none` |
| `alive` | `(comptime b, p, h) → bool` | 없음(거짓이 답) |
| `used` · `outstanding` | `→ u64` — 순차로 꺼내 본 블록 수 · 지금 밖에 나가 있는 블록 수 | 없음 |

*표 50.1 — `pool` 의 op — 첫 인자가 브랜드, 모두 `effects none`*

`take` 는 **놓은 것을 먼저 준다**(자유 목록은 LIFO) — 받고 놓기를 되풀이해도 풀이 마르지 않는다. 재사용된 블록의 핸들은 세대가 올라간 새 핸들이다. 브랜드를 매번 적는 이유 — 이 언어에는 추론되는 타입 파라미터가 없고, 그 한 낱말이 “이 핸들은 저 풀의 것” 이라는 계약이다.

**남는 창 하나** — `bytes` 로 빌린 슬라이스를 **든 채** `release` 하면 그 슬라이스는 여전히 쓸 수 있다. 그 창은 `borrow <이름> be <식> do … end` 블록이 어휘적으로 닫는다 — 빌림 안에서만 바이트를 만지고, 해제는 빌림 밖에서 한다(12장).

```lowent
newtype demo_brand u8 .

proc demo input mem mut slice u8 . . input gens mut slice u64 . . output u64 . effects none .
do
  let po option (pool.block_pool demo_brand) . be pool.init demo_brand mem gens 16 .
  guard is_some po . else return 89 .
  var p pool.block_pool demo_brand . be some_value po .
  let h option (pool.handle demo_brand) . be pool.take demo_brand p .
  guard is_some h . else return 91 .
  let hh pool.handle demo_brand . be some_value h .
  let b option mut slice u8 . . be pool.bytes demo_brand p hh .
  guard is_some b . else return 92 .
  let bv mut slice u8 . be some_value b .
  var total u64 be 0 .
  borrow v be bv do
    set (index v 8) 3 .
    set (index v 9) 4 .
    set total (add (narrow u64 (index v 8)) (narrow u64 (index v 9))) .
  end
  let rel bool be pool.release demo_brand p hh .
  guard eq rel true . else return 94 .
  let dead option mut slice u8 . . be pool.bytes demo_brand p hh .
  guard eq (is_some dead) false . else return 95 .
  return total .
end
```

> **반례. 해제한 핸들로 다시 닿는다 · 이중 해제**
>
> > 해제 뒤 `bytes` 는 `none` 이고, 검사 없이 `some_value` 를 부르면 그 줄에서 `E-VM-NONE` 으로 멈춘다. 두 번째 `release` 는 조용히 `false` 를 돌려줄 뿐이라, 반환값을 보지 않으면 “놓았다고 믿었는데 놓이지 않은” 결함이 숨는다. `guard eq rel true .` 로 받는다.

> **반례. 블록 앞 8 바이트에 남아야 할 값을 두고 해제한다**
>
> > 해제가 그 자리를 자유 목록 링크로 덮는다. 세대가 올라 아무도 닿지 못하므로 안전하지만, “해제 뒤에도 메모리에 남아 있겠지” 라는 기대는 앞 8 바이트에서 틀린다. 자료는 8 번 바이트부터 둔다.

> **반례. 빌린 이름을 블록 밖으로 내보낸다**
>
> > `borrow v be bv do set out v . end` 는 컴파일 에러 `E-BORROW-ESCAPE` 다 — 빌림은 블록 끝에서 끝난다.

**주의.** 핸들은 값이라 복사해 들고 다닐 수 있지만, 어느 복사본으로든 `release` 하면 전부 낡는다. 받고 놓기를 아무리 되풀이해도 `outstanding` 이 늘지 않는 것이 자유 목록이 사는 증거다(`used` 는 처음 몇 라운드만 는다). `outstanding` 은 자유 목록을 걸으므로 O(자유 블록 수)다 — 뜨거운 경로에서 매번 부르지 않는다. 한 핸들 = 한 블록이다. 순차 배달 전제다. op 이름이 `live` 가 아니라 `outstanding` 인 이유 — 흔한 낱말은 처리기가 지역 변수와 op 머리를 가르지 못했다. 핸들의 비트 폭 설계는 [`budget`](sec98.md#mod-budget) 이 돕는다.

---

[← 이전](sec95.md) · [목차로](README.md) · [다음 →](sec97.md)

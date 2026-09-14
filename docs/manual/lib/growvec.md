# growvec — 자가성장 바이트 벡터 (`vec u8` 의 별칭)

`lib/growvec.low` · 모듈 이름 `growvec` · 계층 L1(저장 — 할당 권한이 필요하다)

> **2026-07-26 — 이 모듈은 이제 [vecgen](vecgen.md) 위의 얇은 층이다.**
> `growvec.gvec` 는 `vecgen.vec u8` 의 **별칭**이고, op 들은 그리로 위임한다.
> **쓰던 코드는 한 줄도 안 바뀐다**(골든이 그것을 지킨다) — 바뀐 것은 구현뿐이다.
> (2026-09-13 예외 하나: `open` 이 얼로케이터를 **`using` 절**로 받는다 — `growvec.open bump 16` 이
> `using bump be growvec.open 16` 이 됐다. RFC-0112 D8 · WO-0214.)
> 원소가 바이트가 아니면 [vecgen](vecgen.md) 을 직접 쓴다. 바이트만 밀 때는 타입 인자를
> 안 적어도 되는 이쪽이 짧다 — **이름이 짧은 것도 인터페이스**다.

## 처음 쓰는 사람에게

**"자리가 모자라면 알아서 더 받아 오는 바이트 배열"** 이다.

보통 배열은 크기를 미리 정한다. `alloc_bytes stack capacity 64` 라고 쓰면 64바이트고, 65번째를
넣을 자리는 없다. 그런데 얼마나 들어올지 모르는 경우가 많다 — 파일을 다 읽을 때까지, 사용자가
Enter 를 누를 때까지. 그럴 때 쓰는 것이 이 모듈이다.

```lowent
use growvec .
use allocs .

let g option growvec.gvec . using bump be growvec.open 16 .   rem 16바이트로 시작
guard is_some g . else return 1 .
var v growvec.gvec be some_value g .
let a bool be growvec.add_all v "hello" .               rem 자리 부족하면 알아서 늘어난다
let b bool be growvec.add_all v " world" .
rem 담긴 바이트만 보는 뷰
let s slice u8 be growvec.view_of v .                   rem "hello world"
```

[vecs](vecs.md) 와 무엇이 다른가? `vecs.push_byte` 는 **부를 때마다** 얼로케이터와 버퍼와 길이를
전부 넘겨야 하고, 늘어난 새 버퍼를 **받아서 다시 넣는 것도 호출부의 일**이다. 한 번 빠뜨리면
옛 버퍼를 계속 쓰게 된다. `growvec` 은 그 셋을 **한 값 안에** 넣는다. 그것이 "자가성장" 의 뜻이다.

**그래도 마법은 없다.** 얼로케이터는 여전히 누군가 넣어 준 것이고(`open` 이 `using` 으로 받는다),
`cap allocator` 를 든 op 만 그것을 만들 수 있다. 컨테이너가 권한을 **만들어 내지 않는다** —
다만 **들고 다닐** 뿐이다.

## 왜 있는가

RFC-0075 §2.2 가 적어 둔 한계를 푼다: *"컨테이너가 얼로케이터를 안 들고 있어서 호출부가
다섯 조각을 들고 다닌다."*

`vecs` 로 바이트를 모으는 코드는 이렇게 생긴다:

```lowent
rem vecs — 부를 때마다 다섯 조각
let r option mut slice u8 . using bump be vecs.push_byte v buf x .
guard is_some r . else return false .
set buf (some_value r) .    rem ← 이걸 빠뜨리면 옛 버퍼를 계속 쓴다
```

마지막 줄이 **잊기 쉬운 곳**이다. 그리고 잊었을 때 컴파일러가 못 잡는다 — 옛 버퍼도 유효한
슬라이스이기 때문이다. 늘어나기 전까지는 잘 돌아가다가, 늘어나는 순간부터 조용히 옛 자리에
쓴다. 이런 종류의 버그를 **구조로 없애는** 것이 이 모듈의 존재 이유다.

## 설계 의도와 경계

**① 얼로케이터·저장소·길이를 한 값에 넣는다.** 그게 `gvec` 구조체다. 호출부는 하나만 들고 다닌다.

**② 그래도 성장은 비용이 보이는 자리에서 일어난다.** `add_byte` 가 `false` 를 돌려줄 수 있고
(OOM 은 값이다 — RFC-0043 D5), 재할당이 일어나는 지점은 소스에 그대로 적혀 있다. 감춘 것은
"호출부가 버퍼를 다시 넣는 일" 뿐이지 **비용이 아니다**.

**③ 성장률은 두 배 + 8 이다.** 상수 8 은 아주 작은 벡터가 1→2→4 로 여러 번 옮겨 다니지 않게
한다. 두 배는 `n` 번 push 의 총 복사 비용을 `O(n)` 으로 만드는 표준 선택이다.

**④ `add_all` 은 전량-아니면-무를 약속하지 않는다.** 도중에 실패하면 **앞부분은 이미 들어가
있다**. 약속하려면 먼저 자리를 확보해야 하고, 그 비용을 낼지는 호출자가 고를 일이라
`reserve_more` 를 따로 뒀다.

**⑤ 원소 타입 제네릭은 이제 있다** — [vecgen](vecgen.md) 이 그것이고, 이 모듈은 그 위의 얇은
층이다. 바이트가 아닌 원소가 필요하면 그쪽을 직접 쓴다.

**안 지은 것(정직히)**: 축소(shrink) · 개별 반납(범프 얼로케이터가 그걸 안 한다).

## 자료구조

```lowent
export type gvec vecgen.vec u8 . .   rem ← 별칭이다. 실체는 vecgen 의 vec#u8
```

즉 [vecgen](vecgen.md) 의 자료구조 그대로다(얼로케이터 · 소유하는 바이트 저장소 · 담긴 개수).

| 불변식 | |
|---|---|
| `n * 1 <= len store` | 담긴 개수는 용량을 넘지 않는다 |
| 원소 `0 .. n` | 유효한 데이터 |
| 그 뒤 | **쓰레기다** — 읽지 않는다 |
| 얼로케이터 | 열린 뒤로 안 바뀐다 |

저장소는 성장할 때마다 **다른 버퍼로 바뀐다**. 그래서 예전에 꺼내 둔 `view_of` 결과는
성장 후에 **옛 버퍼를 가리킨다**(아래 반례 참고).

## op 한눈에

| op | effects | 하는 일 |
|---|---|---|
| `open` | state | 빈 벡터를 연다(첫 자리를 받는다) |
| `count_of` | none | 담긴 개수 |
| `cap_of` | none | 지금 용량 |
| `add_byte` | state | 바이트 하나 — 모자라면 스스로 늘린다 |
| `add_all` | state | 바이트열 통째로 |
| `reserve_more` | state | 앞으로 n 개 더 들어갈 자리를 미리 확보 |
| `view_of` | none | 담긴 만큼만 보는 뷰 |

## op 상세

### `open` — 연다

```lowent
proc open using al allocs.bump_bytes . input cap0 u64 . output option gvec . effects state .
```

- `al` — **이미 초기화된** 범프 얼로케이터(`using` 절 — 위치 인자가 아니다). 부르는 쪽은
  `let g … using bump be growvec.open 16 .` 으로 건넨다. 누가 만들었는지가 권한의 자리다.
- `cap0` — 처음 받을 용량. 0 도 된다(첫 push 에서 8 로 늘어난다).
- 반환 — 첫 자리를 못 받으면 `none`.

### `count_of` · `cap_of`

```lowent
fn count_of input g gvec . output u64 .   rem = g.n
fn cap_of   input g gvec . output u64 .   rem = len g.buf
```

`count_of` 가 "담긴 것", `cap_of` 가 "들어갈 수 있는 것" 이다. 둘을 헷갈리면 쓰레기를 읽는다.

### `add_byte` — 하나 민다

```lowent
proc add_byte input g mut gvec . input b u8 . output bool . effects state .
```

- `g` 는 **`mut`** 다 — 이 op 이 `g` 를 고친다(`n` 이 늘고, 필요하면 `buf` 가 바뀐다).
- 반환 — 새 자리를 못 받으면 `false`. **트랩이 아니라 값이다.**

자리가 있으면 `O(1)`. 없으면 새 버퍼(현재의 두 배 + 8)를 받아 **전부 복사**한다 — `O(n)`.
연속 push `n` 번의 총 비용은 `O(n)` 이다(분할상환).

`false` 를 받은 뒤 벡터는 **여전히 유효하다** — 그 바이트만 안 들어갔다.

### `add_all` — 통째로 민다

```lowent
proc add_all input g mut gvec . input s slice u8 . output bool . effects state .
```

`add_byte` 를 반복한다. **중간에 실패하면 앞부분은 들어가 있다** — 전량-아니면-무가 필요하면
아래처럼 `reserve_more` 를 먼저 부른다.

### `reserve_more` — 미리 확보

```lowent
proc reserve_more input g mut gvec . input more u64 . output bool . effects state .
```

`n + more` 가 들어갈 자리를 확보한다. 이미 충분하면 아무 것도 안 하고 `true`.

**이걸 먼저 부르면 `add_all` 이 도중에 실패하지 않는다.** 성장을 한 번으로 몰아 복사도 줄인다 —

★★★ **그리고 메모리 쪽의 값이 더 크다 — 4배다**(2026-08-18 실측).
얼로케이터가 **되돌리지 않는 범프**라(`lib/alloc.low`: `free` 가 없다) 벡터가 자랄 때마다
**옛 버퍼가 아레나에 버려진 채 남는다**. 그래서 고수위는 최종 용량이 아니라 **모든 세대의
합**이다:

| 담은 바이트 | 자라며 채우면 아레나 | 미리 잡고 채우면 |
|---:|---:|---:|
| 512 | **1,016** (×1.98) | 512 (×1.00) |
| 2,048 | **4,088** (×2.00) | 2,048 (×1.00) |

★ **한때 4배였다** — 범프는 되돌리지 않으므로 자랄 때마다 **옛 버퍼가 아레나에 버려진 채
남았다**(2,048 B 담는 데 8,104 B). 2026-08-18 에 얼로케이터에 **제자리 성장**(`grow`)을 두어
그 세대를 없앴다: 마지막 할당이 우리 버퍼면 그 자리를 늘린다. 이제 `자라며 == 최종 cap` 이다.

남은 2배는 **성장 정책의 여유**(`room*2 + 8` ⇒ cap ≈ 2n)이고 그것은 시간과의 정상적인 교환이다.
⇒ `reserve_more` 는 여전히 값을 한다 — **여유까지 없애 정확히 필요한 만큼**만 잡는다.
상세: 사설 `RESULTS-2026-08-18-HEAP.md`.
넣을 크기를 미리 아는 경우에는 항상 이 순서가 낫다.

```lowent
guard growvec.reserve_more v (len s) . else return false .
let ok bool be growvec.add_all v s .        rem 이제 실패하지 않는다
```

### `view_of` — 담긴 만큼

```lowent
proc view_of input g gvec . output slice u8 .   rem 담긴 만큼만
```

**버퍼 전체가 아니라 쓴 만큼**이다. `cap_of` 만큼 읽으면 뒤는 쓰레기다.

## 사용법과 예제

### ① 크기를 모르는 입력 모으기

```lowent
module collect .

use growvec .
use allocs .

rem 조각들을 하나로 잇는다. 총 길이를 미리 모르는 자리다.
export proc join_parts input al allocs.bump_bytes . input a slice u8 . input b slice u8 . input c slice u8 . output option slice u8 . effects state . do
  let g option growvec.gvec . using al be growvec.open 16 .
  guard is_some g . else return none .
  var v growvec.gvec be some_value g .
  guard growvec.add_all v a . else return none .
  guard growvec.add_all v b . else return none .
  guard growvec.add_all v c . else return none .
  return some (growvec.view_of v) .
end
```

### ② 전량-아니면-무가 필요할 때

```lowent
rem 레코드는 통째로 들어가거나 아예 안 들어가야 한다.
proc put_record input v mut growvec.gvec . input rec slice u8 . output bool . effects state . do
  guard growvec.reserve_more v (add (len rec) 1) . else return false .
  guard growvec.add_all v rec . else return false .
  return growvec.add_byte v 10 .
end
```

`reserve_more` 가 성공한 뒤로는 `add_*` 가 실패할 수 없다 — 자리가 이미 있기 때문이다.

### ③ 바이트가 아니면 vecgen 으로

```lowent
rem 같은 자료구조, 원소 타입만 고른다
let g option (vecgen.vec u32 allocs.bump_bytes) . using bump be vecgen.open u32 8 .
guard is_some g . else return 1 .
var v vecgen.vec u32 allocs.bump_bytes . be some_value g .
guard vecgen.append u32 allocs.bump_bytes v 70000 . else return 2 .
```

`growvec` 은 그 중 `u8` 자리에 **짧은 이름**을 준 것뿐이다.

## 반례 — 이렇게 쓰면 안 된다

### ✘ 성장 전에 꺼낸 뷰를 성장 후에 쓴다

```lowent
rem ✘ 위험하다
let s slice u8 be growvec.view_of v .
let ok bool be growvec.add_all v "more" .   rem ← 여기서 buf 가 바뀔 수 있다
rem s 는 이제 **옛 버퍼**를 본다 — 늘어나지 않았다면 우연히 맞는다(그래서 더 나쁘다)
return s .
```

**뷰는 마지막에 꺼낸다.** 더 넣을 일이 남아 있으면 꺼내지 않는다.

### ✘ 반환값을 안 본다

```lowent
rem ✘ 자리를 못 받았는데 넣었다고 여긴다
let ok bool be growvec.add_byte v 65 .
rem ok 를 안 보면 벡터는 그 바이트 없이 계속 간다 — 조용히 짧아진 결과가 나온다
```

`guard … else` 로 받는다. OOM 은 값이라, **값을 안 보면 없던 일이 된다.**

### ✘ `cap_of` 만큼 읽는다

```lowent
rem ✘ n 뒤는 쓰레기다
var i u64 be 0 .
while lt i (growvec.cap_of v) . do … end
rem ✔
while lt i (growvec.count_of v) . do … end
```

또는 그냥 `view_of` 를 순회한다.

### ✘ `gvec` 을 복사해 두고 양쪽에 넣는다

```lowent
var a growvec.gvec be some_value g .
var b growvec.gvec be a .      rem ✘ 둘이 같은 버퍼를 본다
let x bool be growvec.add_byte a 65 .
let y bool be growvec.add_byte b 66 .   rem b 의 n 은 a 의 변경을 모른다 — 서로 덮어쓴다
```

한 벡터는 **한 이름**으로 다룬다. 넘길 때는 `mut gvec` 로 넘긴다(복사가 아니라 같은 것을 고친다).

### ✘ 얼로케이터가 죽은 뒤에도 쓴다

`gvec` 은 `al` 을 들고 있지만, 그 얼로케이터의 뒷받침 메모리가 사라지면(스코프를 벗어나면)
벡터도 함께 죽는다. **벡터의 수명은 얼로케이터의 수명 안이다.**

## 주의사항

- **개별 반납은 없다.** 범프 얼로케이터는 통째로만 되돌린다(RFC-0043 D5). 성장이 여러 번
  일어나면 옛 버퍼들이 얼로케이터 안에 남는다 — 크기를 대충이라도 알면 `open` 의 `cap0` 이나
  `reserve_more` 로 성장 횟수를 줄이는 것이 실질적인 이득이다.
- **바이트 전용이다.** `u32` 벡터가 필요하면 지금은 직접 인코딩하거나 [soa](soa.md) 를 본다.
  제네릭 원소는 제네릭 **소유** 타입(`vec T`)이 언어에 들어온 뒤의 일이다.
- **`effects state` 다.** 순수 계층(`effects none`)에서는 못 쓴다 — 그것이 의도다. 순수하게
  풀 수 있는 문제라면 호출자 버퍼([fmt](fmt.md) 규약)가 더 싸다.

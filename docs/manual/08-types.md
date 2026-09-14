# 8. 나만의 타입 — `struct` 와 `enum`

[← 목차](README.md) · [← 7. guard 와 option](07-guard-and-option.md)

## `struct` — 필드의 묶음

**`struct`** = 이름 붙은 필드의 묶음. `make` 로 만들고 `field` 로 읽는다(붙임 점 `p.x` 는 없다 — `E-FIELD-GLUED`).

```lowent
rem ✓ 점(point) 하나 — 필드는 무두 블록(do 없이 end 로 닫음)에 적는다.
struct point
  x u64 .
  y u64 .
end
fn origin output point . do
  return make point do x 3 . y 4 . end
end
fn getx input p point . output u64 . do
  return field p x .          rem 다단도 된다: `field o inner deep`
end
```

## `enum` — 여러 경우 중 하나 (페이로드 가능)

**`enum`** = 여러 경우 중 하나. 각 경우(변형)는 **페이로드**(딸린 값)를 가질 수 있다.
만들 때는 `<enum>.<변형>` 으로, 가를 때는 `isa`(태그 확인)·`get`(페이로드 꺼내기)로:

갈래마다 **점으로 닫는다**(`red .`). 개행은 닫개가 아니므로, 점 없이 줄마다 적으면 여러 갈래가
**한 갈래로 이어진다**.

```lowent
rem ✓ 페이로드 enum — lit 은 수 하나, add 는 자식 둘(아레나 인덱스).
enum node
  lit v u32 .
  add l u32 r u32 .
end
fn leaf output node . do return node.lit 42 . end
fn is_leaf input n node . output u8 . do
  guard isa n lit . else return 0 .     rem lit 변형인지 확인
  return 1 .
end
```

> `enum` 변형이 **자기 타입을 값으로** 담으면(`add l node .`) 크기가 무한이라 거절된다
> (`E-ENUM-INFINITE`) — 재귀는 위처럼 **인덱스**(`u32`)나 `owned` 로 끊는다. 그래서 AST 같은
> 나무 구조는 노드를 `slice node` 아레나에 담고 인덱스로 잇는다.

## `range` — 타입이 좁아지면 검사가 사라진다

정수 파라미터가 실제로는 좁은 범위만 받는다면, **타입 자리에서** 그렇게 적을 수 있다.

```lowent
fn scale input a range 0 100 . output u8 . do
  return narrow u8 (mul a 2) .          rem 200 을 못 넘는다 — 검사가 필요 없다
end
```

`range 0 100` 은 *"0 이상 100 이하"* 다. 이것은 주석이 아니라 **타입**이다:

- 호출자가 범위를 못 지키면 **진입에서** 걸린다,
- 본문의 산술은 그 범위를 **사실로** 쓴다 ⇒ 넘침 검사가 **지워진다**.

★ `requires le a 100 .` 로도 같은 일을 할 수 있다. 다른 점은 **어디에 적히는가**다 —
`range` 는 **시그니처에** 적히므로 부르는 쪽이 계약을 **보고** 쓴다.

☞ 별칭에도 실려 넘어간다: `type pct range 0 100 .` 을 만들면 `pct` 를 받는 모든 자리가
그 범위를 물려받는다.

---

[← 목차](README.md) · [다음: 9. match →](09-match.md)

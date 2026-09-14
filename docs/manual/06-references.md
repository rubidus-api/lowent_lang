# 6. 참조 — 값을 빌려 주기 (`ref` · `mut_ref`)

[← 목차](README.md) · [← 5. 배열과 슬라이스](05-arrays-and-slices.md)

큰 값을 복사하지 않고 **빌려** 넘길 때 참조를 쓴다. 두 가지뿐이고, 이름이 곧 권한이다:

- **`ref <타입>`** — **읽기 전용** 빌림. 볼 수는 있어도 **못 쓴다**.
- **`mut_ref <타입>`** — **쓰기 가능** 빌림. 빌린 동안 값을 바꾼다.

```lowent
rem ✓ mut_ref 로 빌려 1 을 더한다 — 호출자의 값이 바뀐다.
proc bump output u64 . input p mut_ref u64 . effects none . do
  set p (add p 1) .
  return p .
end
```

**✗ 실수 1 — `ref`(읽기 전용)로 쓰기:**

```lowent
rem ✗ 잘못 — ref 는 못 쓴다
proc f output u64 . input p ref u64 . effects none . do
  set p 1 .
  return p .
end
```
```
E-TYPE-REF: 공유 참조(`ref`)로는 쓸 수 없다 — 쓰려면 `mut_ref`
```

**✗ 실수 2 — 읽기 참조를 쓰기 자리로 몰래 넘기기:** `ref q` 를 `mut` 를 받는 자리에 주면
거절된다(**세탁 금지**):

```
E-TYPE-ARGMUT: `ref` 로 감싼 값을 `mut` 매개변수에 넘길 수 없다
```

⟹ **한 값을 여러 곳이 동시에 쓰지 못하게** 언어가 지킨다(readers-XOR-writer). 그래서
가비지 컬렉터 없이도 메모리가 안전하다 — 규칙은 "쓰는 자는 혼자, 읽는 자는 여럿".

**✗ 실수 3 — 슬라이스를 `mut ref` 로 받기:**

```lowent
rem ✗ 잘못 — 슬라이스에는 mut ref 를 쓰지 않는다
proc shrink output u64 . input p mut ref slice u8 . effects none . do
  set p (subslice p 0 0) .
  return 0 .
end
```
```
E-MREF-SLICE: `mut ref slice` 는 `mut slice` 가 못 하는 일을 하나만 더 준다 —
              호출자의 서술자를 갈아끼우는 것. 새 슬라이스는 **반환값으로** 말하라
```

**왜 이것만 막는가.** `mut slice` 를 그냥 넘기면 **원소는 이미 쓸 수 있다** — 길이·시작
주소는 복사되지만 가리키는 바이트는 같기 때문이다. 그러니 `mut ref slice` 가 더 주는
능력은 **호출자 쪽의 길이를 몰래 바꾸는 것** 하나뿐이고, 그러면 호출자의 루프는 자기
코드 어디에도 대입이 없는데 길이가 달라진다. 새 슬라이스는 **돌려주면** 된다:

```lowent
rem ✓ 이렇게 — 길이가 달라졌음을 반환값으로 말한다
fn head output slice u8 . input p slice u8 . input n u64 .
  requires le n (len p) . do
  return subslice p 0 n .
end
```

---

[← 목차](README.md) · [다음: 7. guard 와 option →](07-guard-and-option.md)

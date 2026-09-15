# 7. `guard` 와 "없을 수도 있는 값" (`option`)

[← 목차](README.md) · [← 6. 참조](06-references.md)

`guard <조건> . else <빠져나감> .` — 조건이 **거짓이면 그 자리에서 떠난다**(반환·`break`·
`panic`). 조건이 참이면, **그 아래 코드는 조건이 성립함을 믿어도 된다.** 이것이 `guard`
전부다 — `if` 와 달리 `else` 는 **반드시 op 을 떠나야** 한다.

없을 수도 있는 값은 **`option <타입>`** 으로 받는다. 꺼내기 전에 `guard is_some` 으로
있는지 확인하고, `some_value` 로 꺼낸다:

```lowent
rem ✓ 빈 슬라이스면 0, 아니면 첫 원소. guard 가 "비어 있지 않음"을 아래에 보장한다.
fn firstpos input xs slice u64 . output u64 . do
  guard gt (len xs) 0 . else return 0 .
  return index xs 0 .        rem 여기선 xs 가 비지 않았음이 보장된다
end
```

```lowent
rem ✓ option 을 guard 로 갈라 꺼낸다.
fn unwrap_or0 input o option u64 . output u64 . do
  guard is_some o . else return 0 .
  return some_value o .      rem is_some 를 통과했으니 안전하게 꺼낸다
end
```

**✗ 흔한 실수 — `guard` 의 `else` 가 안 떠나기:**

```lowent
rem ✗ 잘못 — else 가 값을 고칠 뿐 op 을 떠나지 않는다
fn f input a u64 . output u64 . do
  guard gt a 0 . else do set a 0 . end
  return a .
end
```
```
E-GUARD-FALLTHROUGH: 이 `guard` 의 `else` 가 빠져나가지 않는다 — 떠나지 않을 거면 `if` 를 써라
```

⟹ `else` 가 **떠나기** 때문에, 그 아래에서 조건을 믿을 수 있다. 떠나지 않을 일이면
`if <조건> . do … end else do … end` 를 쓴다.

> 오류를 **값으로** 돌려줄 땐 `result` + `errors`([3장](03-ops.md))와 `return error <이름>` /
> `return ok <값>` 을 쓰고, 호출한 쪽은 `try` 로 실패를 위로 전파한다. `option` 은 "값이 없다"만,
> `result` 는 "왜 실패했는지"까지 말한다.

## `try` 의 꼬리 — `option` 과 `result` 사이를 건넌다

`try` 는 실패를 위로 전파한다. 그런데 **부르는 쪽과 불리는 쪽의 종류가 다를 때**가 있다 —
불린 op 은 `result` 인데 나는 `option` 을 돌려주고 싶거나, 그 반대이거나. 그 자리에 꼬리를 붙인다.

```lowent
rem result → option : 오류를 **버린다**
return try (g a) else_none .

rem option → result : 없음에 **이름을 붙인다**
return try (h a) else_error not_found .
```

| 꼬리 | 방향 | 무엇을 하나 |
|---|---|---|
| `else_none` | `result` → `option` | 오류를 **버린다** — 왜 실패했는지 더는 말하지 않는다 |
| `else_error <이름>` | `option` → `result` | 없음을 **이름 있는 오류**로 만든다 |
| `map_error <op>` | `result` → `result` | 오류를 다른 오류로 옮긴다 |

**`else_none` 은 정보를 버리는 선택이다.** 편해 보여서 습관이 되기 쉬운데, 그 순간부터
호출자는 *"왜"* 를 물을 수 없다. 버릴 만한 자리에서만 버려라.

⚠ **`map_error` 는 아직 못 한다** — 어휘에는 있고 `E-IR-UNSUP` 으로 **못 한다고 말한다.**
오진하지 않는다는 뜻이고, 쓸 수 있다는 뜻은 아니다.

---

[← 목차](README.md) · [다음: 8. 나만의 타입 →](08-types.md)

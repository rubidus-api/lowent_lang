# 4. 지역 변수 — `let` · `var` · `set`

[← 목차](README.md) · [← 3. op](03-ops.md)

op 본문 안에서 값을 담아 두는 이름이 **지역 변수**다. 종류는 둘뿐이고, **바뀌는지 아닌지**를
언제나 이름에서 알 수 있다:

- `let <이름> <타입> be <식> .` — **불변**. 한 번 정하면 안 바뀐다.
- `var <이름> <타입> be <식> .` — **가변**. `set <이름> <식> .` 으로 다시 넣는다.

타입은 **꼭 적는다**(Lowent 는 지역의 타입도 명시한다 — 시그니처만 봐도 알게).

```lowent
rem ✓ 1 부터 n 까지 더하기 — var 로 누적하고 set 으로 갱신한다.
fn sumto input n u64 . output u64 . do
  var total u64 be 0 .       rem 가변: 계속 바뀐다
  var i u64 be 1 .
  while le i n . do
    set total (add total i) .
    set i (add i 1) .
  end
  return total .
end
```

**✗ 흔한 실수 — `let`(불변)에 `set` 하기:**

```lowent
rem ✗ 잘못
fn f output u64 . do
  let a u64 be 1 .
  set a 2 .                  rem let 은 안 바뀐다!
  return a .
end
```
```
E-IMMUTABLE: `let` 은 불변이다 — 바꾸려면 `var` 로 선언하라
```

⟹ **바뀔 값은 `var`, 안 바뀔 값은 `let`.** 헷갈리면 컴파일러가 알려 준다.

---

[← 목차](README.md) · [다음: 5. 배열과 슬라이스 →](05-arrays-and-slices.md)

#import "../lib.typ": *

= C 와 만나는 자리

#chapter-toc()

#prereq(
  ([#chref("capabilities") 권한], [`cap c` 는 시작점이 받을 수 없는 권한이다]),
  ([#chref("contracts") 계약], [`requires` 는 진입에서 검사된다]),
  ([#chref("modules") 모듈], [`export` 한 op 만 C 에서 부를 수 있는 심볼이 된다]),
)

#deepqa[
  #chref("capabilities")에서 `cap c` 는 왜 시작점이 요구할 수 없는 권한이라고 했는가?
][
  실행하는 쪽(운영체제)이 건넬 수 있는 권한이 아니기 때문이다. C 로 들어가는 문은 그것을 줄 자격이 있는 자리에서 만들어져
  인자로 흘러야 한다. 이 장은 그 문 --- `extern` --- 을 다룬다. 들어가는 방향과 나가는 방향 모두다.
]

#why[
  새 언어가 현실에서 쓰이려면 이미 있는 C 코드와 만나야 한다. 운영체제의 API, 수십 년 된 라이브러리, 하드웨어 공급사의 SDK
  가 모두 C 다. 그런데 C 로 넘어가는 순간 이 언어가 지키던 모든 것 --- 경계 검사, 소유, 효과 --- 이 C 쪽에서 깨질 수 있다.
  Lowent 는 이 자리를 편하게 만드는 대신 *좁고 보이게* 만든다. 그리고 반대 방향, C 가 Lowent 를 부르는 자리에는 계약을
  세워 문 안을 지킨다.
]

#organizer[
  C 함수를 부르는 `extern` op 이 갖춰야 할 세 가지(`unsafe` 표시·`cap c`·효과 줄)와 `link` 절을 익힌다. 경계를 건널 수 있는
  타입이 C ABI 가 표현할 수 있는 것으로 한정되고 슬라이스는 포인터와 길이 둘이 된다는 것을 알게 된다. `export` 한 op 을
  `--emit-h`·`--no-main` 으로 C 프로그램에 넣고, C 가 계약을 어기면 문에서 멈추는 모습을 확인한다. 되부름과 소유를 넘기는
  규칙도 보게 된다.
]

#chapter-questions()

== C 를 부른다 --- 표시·권리·효과 줄

#demo("examples/ch29/area.low")

C 함수를 부르는 op 은 셋을 *모두* 갖춘다.

#dtable(
  columns: 3,
  id: "ffi-three",
  caption: [C 를 부르는 op 이 갖추어야 하는 것],
  [*갖출 것*], [*없으면*], [*누구에게 말하나*],
  [`unsafe` 표시], [`E-FFI-NOUNSAFE`], [사람에게 --- 여기서부터는 언어가 아니라 사람이 책임진다],
  [`input k cap c .`], [`E-FFI-NOCAP`], [처리기에게 --- C 로 들어가는 것은 건네받은 권리다],
  [효과 줄(적어도 `effects unsafe`)], [`E-FFI-NOEFFECT`], [부르는 쪽에게 --- 무엇을 떠안는지 머리에서 배운다],
)

#idx("extern")
`extern` op 은 몸이 C 에 있으므로 `do` 대신 `link "lw_c_area"` 로 C 쪽 이름을 댄다. 이름은 처리기가 op 이름에서 지어내지
않는다. 다른 언어에 하는 약속이므로 약속한 사람이 적는다. `area_twice` 는 그 op 을 부르므로 스스로도 `unsafe` 이고 `cap c`
를 받는다. 표시와 권리가 호출 사슬을 따라 올라간다.

#demo("examples/ch29/nounsafe.low")

#demo("examples/ch29/nocap.low")

셋 중 하나만 있어도 "부를 수는 있다". 그러나 그러면 이 언어는 자기가 무엇을 잃었는지 말하지 못한다.

#qa[
  `unsafe` 가 붙은 op 안에서는 아무 일이나 해도 되는가?
][
  아니다. `unsafe` 는 "아무렇게나 해도 된다" 가 아니라 "이 자리의 일부를 처리기가 검사할 수 없다" 는 표시다. `unsafe` op
  의 본문에서도 경계 검사·소유·차용 규칙은 그대로 적용된다. 검사할 수 없는 것은 C 함수의 *안*이다. 그 표시가 소스에
  남아 있어서 나중에 감사할 자리를 찾을 수 있다.
]

== 경계를 건너는 타입

경계를 건널 수 있는 것은 C ABI 가 표현할 수 있는 것뿐이다. `option`·`result`·벡터·그릇은 C 에 없고, 처리기는 있는 척하지
않는다.

#demo("examples/ch29/fftype.low")

저절로 사상되는 것은 *슬라이스뿐*이다. `slice τ` 는 "`τ` 를 가리키는 포인터" 와 "개수" 두 인자가 된다. 폭도 안다
--- `slice u32` 는 `uint32_t *` 이지 바이트 포인터가 아니다. 권한은 값이 아니므로 C 로 건너가지 않는다. ABI 이름은 `c` 하나다.
"이 기계에서 C 가 무엇인가" 는 짓는 기계가 이미 정한다.

== C 가 Lowent 를 부른다

반대 방향이다. `export` 한 op 은 C 에서 부를 수 있는 심볼이 된다.

#demo("examples/ch29/exported.low")

`--emit-h` 가 헤더를 낸다. 헤더를 손으로 적으면 서명이 두 곳에 살고, 언젠가 갈린다.

```c
long long clamp_add(long long, long long);
long long sum_bytes(const unsigned char *, size_t);
```

`sum_bytes` 의 `slice u8` 이 포인터와 길이 둘로 나뉜 것을 볼 수 있다. `--no-main` 은 `main` 과 명령 줄 디스패처 없이 내보낸
op 의 진입점만 담은 C 를 낸다. 남의 빌드에 그대로 넣으면 된다. 다음 C 프로그램이 그 둘을 쓴다.

#raw(read("/examples/ch29/host.c"), lang: "c", block: true)

#raw(read("/build/examples-out/ch29/host.c.out"), block: true)

앞의 두 호출은 답을 받는다. 셋째 호출은 `requires le a 1000 .` 을 어긴다. C 는 계약을 모르지만, *불려 들어오는 자리는
이쪽 문*이므로 op 의 계약이 인자에 강제되고 진입에서 멈춘다. 문 안은 Lowent 가, 문 밖은 C 가 책임진다.

#misconception[FFI 경계에서는 언어의 보장이 모두 사라진다][
  C 로 *나가는* 쪽에서는 C 함수 안이 검사 밖이다. 그러나 그 사실은 `unsafe`·`cap c`·효과 줄로 머리에 적힌다. C 가 *들어오는*
  쪽에서는 보장이 그대로 선다. 들어오는 인자가 계약을 어기면 진입에서 멈춘다. 경계는 구멍이 아니라 문이고, 문에는 계약이
  서 있다.
]

== 되부름과 소유

C 에게 Lowent 함수를 넘겨 되부르게 하려면 `export extern` op 의 주소를 `unsafe_fn <op>` 으로 얻어 값으로 넘긴다. 새 규약은
없다. 규칙이 둘 붙는다.

- `unsafe_fn` 이 가리킬 수 있는 것은 `export extern` op 뿐이다(`E-FN-NOTEXPORT`). C 가 부를 수 없는 문의 주소는 주소가 아니다.
- 되부름으로 쓸 op 은 권한을 요구할 수 없다(`E-FN-CAP`). 되부름에 들어서는 것은 C 이고, C 는 건넬 권한이 없다. 권한이 필요한
  일은 되부름 밖에서 하고, 되부름 안은 셈만 한다.

`owned τ` 를 `extern` 에 넘기면 없앨 책임이 C 에게 옮겨간다. 이쪽의 의무는 그 지점에서 끝나고, 그 뒤의 반납이 지켜졌는지는
검증되지 않는다. 고정된 인자 뒤에 개수가 정해지지 않은 인자를 받는 C 함수는 `variadic .` 절로 부를 수 있지만, 그 인자에는
계약이 닿지 않으며 반대 방향(C 가 우리 가변인자를 부르기)은 없다.

#realcase[두 수를 더하는 프로그램이 지는 짐][
  기본 방출은 *프로그램*이다. `main` 과 명령 줄 디스패처가 함께 나오고, 디스패처는 태그 경로와 그 풀들을 붙잡는다. 개발
  저장소의 측정에서 두 수를 더하는 op 하나를 기본으로 내면 읽기 전용 데이터가 약 197 KB, 초기화되지 않은 데이터가 약 1.7 MB
  였고, `--no-main` 으로 내면 둘 다 50 바이트 이하가 되었다. 라이브러리로 넣을 때 `--no-main` 을 쓰는 이유다. 운영체제 없는
  대상(`--target cortex_m`)은 처음부터 디스패처를 내지 않는다.
]

#recap[
  C 를 부르는 `extern` op 은 `unsafe`·`cap c`·효과 줄을 모두 갖추고 `link` 로 C 이름을 댄다. 경계를 건너는 타입은 C ABI 가
  표현할 수 있는 것뿐이고 슬라이스는 포인터와 길이가 된다. `export` 한 op 은 `--emit-h`·`--no-main` 으로 C 에 넣으며, C 가
  계약을 어기면 문에서 멈춘다. 되부름은 권한 없는 `export extern` op 의 주소로 하고, `owned` 를 넘기면 책임이 C 로 간다.
]

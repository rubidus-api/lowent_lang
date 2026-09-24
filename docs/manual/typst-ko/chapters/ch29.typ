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
않는다. 다른 언어에 하는 약속이므로 약속한 사람이 적는다 --- 빠뜨리면 `E-FFI-LINK` 다. `area_twice` 는 그 op 을 부르므로 스스로도 `unsafe` 이고 `cap c`
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

#demo("examples/ch29/variadic.low")

- `newtype cstr unsafe_ptr u8 .` 는 C 의 `char*` 를 이름 붙인 타입으로 만든다. `unsafe_ptr` 는 홀로 쓰는 타입이 아니라 타입 앞에 붙는 한정자다.
- `variadic .` 절이 "고정 인자 뒤에 더 받는다" 를 적고, `link printf .` 가 C 이름을 적는다. 부르는 자리의 `42` 는 고정 인자 뒤에 붙는 값이라 타입
  검사도 계약도 닿지 않는다. 서식과 인자가 어긋나는 C 의 고전적 결함이 여기서는 막히지 않는다는 뜻이다. 그래서 `unsafe` 가 붙는다.
- `cstr_of "…\0"` 은 끝에 영 바이트를 둔 문자열 리터럴을 C 문자열로 본다. VM 은 C 를 부를 수 없으므로 이 예제는 검사만 하고, 네이티브로 지으면
  `sum=42` 를 찍는다.

#realcase[두 수를 더하는 프로그램이 지는 짐][
  기본 방출은 *프로그램*이다. `main` 과 명령 줄 디스패처가 함께 나오고, 디스패처는 태그 경로와 그 풀들을 붙잡는다. 개발
  저장소의 측정에서 두 수를 더하는 op 하나를 기본으로 내면 읽기 전용 데이터가 약 197 KB, 초기화되지 않은 데이터가 약 1.7 MB
  였고, `--no-main` 으로 내면 둘 다 50 바이트 이하가 되었다. 라이브러리로 넣을 때 `--no-main` 을 쓰는 이유다. 운영체제 없는
  대상(`--target cortex_m`)은 처음부터 디스패처를 내지 않는다.
]

== 흔한 실수

#antipattern[`extern` op 에 효과 줄을 적지 않는다][
  #demo("examples/ch29/mistake_noeffect.low")

  진단이 둘 나온다. `E-FFI-NOEFFECT` 는 "C 를 부르는데 효과 줄이 없다" 이고, `E-UNSAFE-UNUSED` 는 "`unsafe` 표시를 했는데 `unsafe` 효과가
  없으니 거짓 경보다" 이다. 둘은 같은 뿌리에서 나온다. 표시(`unsafe`)는 *누가 책임지는가*, 효과 줄(`effects unsafe`)은 *무엇을 하는가* 를
  말하고, 한쪽만 있으면 짝이 맞지 않는다. `effects unsafe .` 한 줄로 둘 다 사라진다.
]

#antipattern[`extern` op 에 `link` 절을 적지 않는다][
  #demo("examples/ch29/mistake_nolink.low")

  `link` 가 없으면 어느 C 심볼을 부르는지 소스 어디에도 없다. 처리기가 op 이름에서 지어내면 op 의 이름을 바꾸는 순간 *다른 C 함수*를
  부르게 되고 그 사실이 적힌 데가 없다. 그래서 `E-FFI-LINK` 로 거절한다 --- 약속은 적은 사람의 것이다.
]

#antipattern[C 를 부르는 op 을 부르면서 `unsafe` 표시를 빠뜨린다][
  #demo("examples/ch29/mistake_callerunsafe.low")

  `area_twice` 는 `effects unsafe` 를 적었지만 머리에 `unsafe` 가 없다. 효과는 호출을 따라 올라가므로 부르는 op 도 `unsafe` 효과를 내고,
  그 효과를 낸다면 누군가 서명해야 한다. `E-UNSAFE-UNDECLARED` 의 말대로 "아무도 서명하지 않은 unsafe op 이 바로 이 언어가 막으려는 구멍"
  이다. `unsafe proc area_twice …` 로 적는다.
]

#antipattern[보통 op 의 주소를 되부름으로 넘긴다][
  #demo("examples/ch29/mistake_cbplain.low")

  C 가 부를 수 있는 것은 `export extern` op 이 만든 심볼뿐이다. 그 입구에서 계약이 인자에 강제된다. 보통 op 에는 그런 문이 없으므로
  `E-FN-NOTEXPORT` 다. 머리를 `export extern fn by_value …` 로 바꾼다. 첫 줄의 `W-EFFECT-OVER` 는 주소를 얻는 것만으로는 `unsafe` 일을 한 것이
  아니라는 알림이다. 그 주소로 C 를 부르는 op 에서 `unsafe` 가 선다.
]

#antipattern[되부름으로 쓸 op 이 권한을 받는다][
  #demo("examples/ch29/mistake_cbcap.low")

  되부름에 들어서는 것은 C 이고 C 에게는 건넬 권한이 없다. 그러니 권한을 받는 op 은 되부름이 될 수 없다(`E-FN-CAP`). 출력이나 파일처럼 권한이
  필요한 일은 되부름 *밖*에서, C 가 돌아온 뒤에 한다. 되부름 안은 셈만 한다.
]

== 이 장의 문법 한눈에

#dtable(
  columns: 3,
  id: "ffi-glance",
  caption: [C 경계의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`unsafe extern proc c_area input k cap c . … effects unsafe . link "lw_c_area" . end`], [C 에 몸이 있는 op], [표시·권리·효과 줄 셋이 모두 있어야 한다],
  [`unsafe proc area_twice input k cap c . … effects unsafe .`], [C 를 부르는 op 을 부르는 op], [표시와 권리가 호출 사슬을 따라 올라간다],
  [`input xs slice u8 .`(경계)], [C 에서는 포인터와 길이 두 인자], [저절로 사상되는 것은 슬라이스뿐],
  [`option`·`result`·벡터를 경계에], [거절(`E-FFI-TYPE`)], [C ABI 에 없는 것을 있는 척하지 않는다],
  [`export fn clamp_add …`], [C 에서 부를 수 있는 심볼], [들어오는 인자에 계약이 강제된다],
  [`lowentc --emit-h` · `--no-main`], [헤더를 낸다 · `main` 없이 라이브러리로 낸다], [서명이 한 곳에만 산다],
  [`unsafe_fn cmp`], [`export extern` op 의 주소(되부름)], [보통 op 은 `E-FN-NOTEXPORT` · 권한을 받으면 `E-FN-CAP`],
  [`input h owned τ .`(extern 에)], [없앨 책임이 C 로 넘어간다], [그 뒤의 반납은 검증되지 않는다],
  [`variadic .`], [C 의 가변 인자 함수를 부른다], [가변 인자에는 계약이 닿지 않는다],
  [`newtype cstr unsafe_ptr u8 .` · `cstr_of "…\0"`], [C 포인터 한정자 · 영 바이트로 끝나는 C 문자열로 보기], [포인터는 이름 붙인 타입으로만 다룬다],
)

#recap[
  C 를 부르는 `extern` op 은 `unsafe`·`cap c`·효과 줄을 모두 갖추고 `link` 로 C 이름을 댄다. 경계를 건너는 타입은 C ABI 가
  표현할 수 있는 것뿐이고 슬라이스는 포인터와 길이가 된다. `export` 한 op 은 `--emit-h`·`--no-main` 으로 C 에 넣으며, C 가
  계약을 어기면 문에서 멈춘다. 되부름은 권한 없는 `export extern` op 의 주소로 하고, `owned` 를 넘기면 책임이 C 로 간다.
]

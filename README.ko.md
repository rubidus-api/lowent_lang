# Lowent (로우엔트)

> [!WARNING]
> **Lowent 는 아직 개발 중이며, 언어 설계도 계속 바뀌고 있다.**
> 문법·표준 라이브러리·진단 코드·명령줄 옵션이 예고 없이 바뀔 수 있다.
> 아직 호환성을 약속하지 않으며, 실제 서비스에 쓸 품질이 아니다.

**저엔트로피(low-entropy) 시스템 프로그래밍 언어.** C 의 기계 모델은 그대로 두고, 미정의 동작·암묵
정수 승급·손으로 하는 수명 관리를 걷어냈다. 그 자리에 컴파일러가 **검사하는** 네 가지를 넣었다:
계약, 효과, 능력(권한), 소유권.

목표는 사람과 AI 가 **짐작 없이** 읽고 쓰는 코드다.

[English README](README.md) · [매뉴얼(한국어)](docs/manual/md-ko/README.md) · [명세](docs/spec/canon/)

---

## 이렇게 생겼다

```lowent
module stats .

rem Pure: no I/O, no allocation, no hidden state. The contract is checked.
fn mean input xs slice u8 . output u64 .
  requires gt (len xs) 0 .
do
  var total u64 be 0 .
  for x xs do
    set total (add total (widen u64 x)) .
  end
  return div total (len xs) .
end
```

```text
$ lowentc --run mean stats.low '[3,4,8]'
mean([3,4,8]) = 5
$ lowentc --run mean stats.low '[]'
  0:0 E-VM-CONTRACT: `requires` violated at entry — the caller broke the contract
```

문장은 낱말 하나로 시작해 피연산자를 앞에서부터 받고(`total + x` 가 아니라 `add total x`), 점(`.`)으로
끝난다. 중위 수식은 `expr` 안에서만 쓴다. 특수문자가 거의 없어서 위에서 아래로 읽히고, 스마트폰
자판으로도 쉽게 친다. 언어 전체의 키워드는 **43 개**다.

## 이런 것은 거절한다

부수 효과는 숨을 수 없다. 출력하려면 권한을 값으로 받아야 하고, 머리에 그 사실을 적어야 한다.

```lowent
module hello .

proc main input out cap io . output u8 . effects io .
do
  let n u64 be write_out out 1 "hello, entropy!\n" .
  return 0 .
end
```

순수해야 할 `fn` 이 몰래 입출력을 하면 컴파일되지 않는다.

```lowent-거부: 순수 fn 이 입출력을 한다 · E-EFFECT-CALC
module leak .

fn shout input out cap io . output u64 .
do
  return write_out out 1 "hi\n" .
end
```

```text
$ lowentc --check leak.low
  leak.low:4:1 E-EFFECT-CALC: this fn is declared pure but performs `io` — make it a `proc` with `effects …`, or remove the effect
```

열어 놓고 닫지 않은 파일도 컴파일되지 않고(`E-OWN-INCOMPLETE`), `close` 뒤에 다시 쓴 핸들은
`E-OWN-MOVED` 다. 거절마다 바뀌지 않는 코드가 붙고, `--diag-json` 을 주면 도구와 에이전트가 읽기 좋게
진단 하나를 JSON 한 줄로 낸다.

## 왜 Lowent 인가

- **계약을 검사한다.** `requires` / `ensures` / `errors` 를 서명에 적는다. 계약을 어기면 경계에서
  멈추고, 아무것도 검사하지 않는 계약은 거절되며, 증명된 계약은 실행 중 검사를 없앤다.
- **효과를 강제한다.** `fn` 은 순수하고, `proc` 은 하는 일(`io`·`alloc`·`state`·`wait` …)을 선언한다.
  선언보다 많은 일을 하면 컴파일 오류다.
- **몰래 쓰는 권한이 없다.** 전역 힙도 전역 입출력도 없다. 할당·파일·C 호출은 모두 권한을 값으로
  받아야 하므로, 서명만 보고도 그 함수가 무엇에 닿는지 안다.
- **GC 없는 소유권.** 가비지 컬렉터도 손으로 하는 `free` 도 없다. 바깥으로 새는 참조, 겹친 쓰기
  참조, 옮긴 값을 다시 쓰는 일을 컴파일할 때 거절한다.
- **놀라지 않는 정수.** 암묵 승급도 미정의 오버플로도 없다. 폭이 바뀌는 자리는 모두 적는다:
  `widen`·`narrow`·`wrap_*`·`sat_*`·`chk_*`.
- **같은 답을 내야 하는 두 백엔드.** VM 이 프로그램을 돌리고, C 백엔드가 네이티브로 컴파일한다.
  둘을 맞대어 시험하며, 답이 다르면 컴파일러 결함이다.
- **작은 보드까지.** 포인터 16 비트·힙 없는 대상까지 다루고, 이식 가능한 SIMD·인라인 asm·MMIO·
  인터럽트 처리기·양방향 C FFI 를 갖췄다.
- **안 된 것은 안 됐다고 말한다.** 받아들이지만 아직 구현하지 않은 기능은 조용히 무시하지 않고
  경고한다(`W-NOT-YET`).

## 빠른 시작

필요한 것은 C23 컴파일러(gcc 또는 clang)와 POSIX 셸뿐이다. 다른 의존은 없다.

```sh
cd impl
make                  # → build/lowentc
make check            # 회귀 시험

build/lowentc --check hello.low              # 검사만
build/lowentc --run main hello.low           # VM 으로 실행
build/lowentc --emit-c hello.low > hello.c   # 네이티브 빌드를 위해 C 로 내보내기
cc -O2 -o hello hello.c -lm -lpthread && ./hello main
```

프로젝트라면 뿌리에 `pkg.low` 매니페스트를 두고 `lowentc run`·`lowentc build` 를 쓴다. 둘 다 검사를
통과하지 못한 프로그램은 돌리지도 짓지도 않는다.

## 더 읽을거리

| 자리 | 무엇 |
|---|---|
| [`docs/manual/`](docs/manual/README.md) | **Lowent 매뉴얼** — 책 형식, 한국어·영어. 언어 · 표준 라이브러리(모듈마다 한 쪽) · 무엇이 증명되었나. 웹·PDF·Markdown. 예제는 모두 두 백엔드로 돌려 확인 |
| [`docs/spec/canon/`](docs/spec/canon/) | 규범 명세. 매뉴얼과 명세가 어긋나면 명세가 이긴다 |
| [`docs/example/`](docs/example/) | 완결 예제 |
| [`impl/`](impl/README.md) | 컴파일러 `lowentc` (C23) |
| [`lib/`](lib/) | 표준 라이브러리 (Lowent 로 작성) |
| [`skills/`](skills/) | AI 에이전트가 Lowent 코드를 쓸 때 읽는 안내 |

## 라이선스

MIT — [`LICENSE`](LICENSE). 들여온 [`proven_c_lib`](impl/vendor/proven/VENDORED.md) 도 MIT 다.

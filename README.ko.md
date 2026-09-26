# Lowent (로우엔트)

> [!WARNING]
> **Lowent 는 아직 개발 중이며, 언어 설계도 계속 바뀌고 있습니다.**
> 문법, 표준 라이브러리, 진단 코드, 명령줄 옵션이 예고 없이 바뀔 수 있습니다. 아직 호환성을 약속하지 않으며, 실제 서비스에 쓸 단계가 아닙니다.

[English](README.md)

**매뉴얼:** [웹에서 읽기](https://rubidus-api.github.io/lowent_lang/manual/html-ko/index.html) · [PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-ko/lowent-manual-ko.pdf) · [English web](https://rubidus-api.github.io/lowent_lang/manual/html-en/index.html) · [English PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-en/lowent-manual-en.pdf)  
**명세:** [웹](https://rubidus-api.github.io/lowent_lang/spec/html/) · [PDF](https://rubidus-api.github.io/lowent_lang/spec/pdf/lowent-spec.pdf)

Lowent 는 **엔트로피가 낮은** 시스템 프로그래밍 언어입니다. 함수의 머리(서명)만 읽어도 그 함수가 무엇을 받고, 무엇을 돌려주고, 바깥 세상에 무엇을 할 수 있는지 알 수 있게 하는 것이 목표입니다. 짐작할 자리가 적은 코드는 사람도 AI 도 틀리지 않고 읽고 고칠 수 있습니다.

```lowent
module stats .

rem 순수 함수입니다: 입출력도, 할당도, 숨은 상태도 없습니다. 계약은 검사됩니다.
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

C 의 기계 모델(값의 배치, 포인터 크기, 비용이 보이는 코드)은 그대로 두었습니다. 대신 C 가 오래 안고 온 함정을 걷어 내고, 그 자리에 컴파일러가 직접 **검사하는** 약속 넷을 두었습니다. 계약, 효과, 권한, 소유입니다.

---

## 무엇이 문제였나

시스템 프로그래밍 언어는 오랫동안 같은 문제를 안고 왔습니다.

1. **코드가 말하지 않은 일을 합니다.** C 에서 정수는 몰래 넓어지거나 좁아지고, 넘치면 미정의 동작이 됩니다. 한 줄의 덧셈이 무슨 일을 할지 알려면 타입 승급 규칙을 외워야 합니다.
2. **서명이 거짓말을 합니다.** `int parse(const char *s)` 는 파일을 열 수도, 네트워크에 닿을 수도, 전역 힙에서 메모리를 가져올 수도 있습니다. 서명만 보고는 알 수 없고, 결국 본문을 끝까지 읽어야 합니다.
3. **메모리 수명은 둘 중 하나를 고르라고 합니다.** 가비지 컬렉터(GC)에 맡기면 멈춤과 런타임을 감수해야 하고, 손으로 관리하면 해제 누락, 두 번 해제, 해제 뒤 사용이 따라옵니다.
4. **문서, 시험, 코드가 따로 놉니다.** 주석에 적은 «이 값은 0 보다 커야 한다» 는 아무도 검사하지 않고, 코드가 바뀌면 조용히 거짓이 됩니다.
5. **문법이 짐작을 요구합니다.** 연산자 우선순위, 문맥에 따라 뜻이 바뀌는 기호, 같은 일을 하는 여러 철자. 사람은 익숙해지면 넘어가지만, 코드를 쓰는 AI 는 이런 자리에서 가장 자주 틀립니다.

## Lowent 는 이렇게 풉니다

### 1. 정수는 적은 대로만 움직입니다

저절로 일어나는 변환은 값을 잃을 수 없는 넓히기(`u8` 을 `i16` 으로)뿐입니다. 좁히기나 부호가 바뀌는 변환처럼 값을 잃을 수 있는 일은 `narrow` 같은 낱말로 직접 적어야 하고, 적지 않으면 컴파일러가 거절합니다. 넘침도 미정의 동작이 아닙니다. 멈출지(기본), 한 바퀴 돌지(`wrap_*`), 끝에서 멈출지(`sat_*`) 직접 고릅니다.

```lowent
module overflow .

fn bump input a u8 . output u8 .
do
  return add a 1 .
end

fn bump_wrap input a u8 . output u8 .
do
  return wrap_add a 1 .
end

fn bump_sat input a u8 . output u8 .
do
  return sat_add a 1 .
end
```

```text
$ lowentc --run bump overflow.low 255
  0:0 E-VM-OVERFLOW: integer overflow at the declared width (use wrap_*/sat_*, or prove the range)
$ lowentc --run bump_wrap overflow.low 255
bump_wrap(255) = 0
$ lowentc --run bump_sat overflow.low 255
bump_sat(255) = 255
```

값의 범위가 증명되는 자리에서는 컴파일러가 넘침 검사를 지웁니다. 증명된 안전에는 실행 비용이 들지 않습니다.

### 2. 서명이 곧 약속입니다

함수는 둘로 나뉩니다. `fn` 은 순수합니다. 같은 입력이면 늘 같은 출력을 내고, 바깥에 흔적을 남기지 않습니다. `proc` 은 바깥에 영향을 줄 수 있지만, 무엇을 하는지 **효과**(effect, 입출력·할당·상태 변경처럼 함수가 바깥에 남기는 자국)로 적어야 합니다. 그리고 그 일을 할 **권한**(capability, 파일이나 출력에 닿을 자격)을 인자로 건네받아야 합니다. 어디서나 몰래 쓸 수 있는 전역 출력이나 전역 힙은 없습니다.

```lowent
module hello .

proc main input out cap io . output u8 . effects io .
do
  let n u64 be write_out out 1 "hello, entropy!\n" .
  return 0 .
end
```

순수하다고 적은 `fn` 이 몰래 출력을 하려 들면 컴파일되지 않습니다.

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

그래서 머리만 보고도 «이 함수는 네트워크에 닿지 않는다», «이 함수는 메모리를 할당하지 않는다» 를 알 수 있습니다. 코드 리뷰도, 보안 점검도, AI 가 코드를 고치는 일도 본문 전체가 아니라 머리에서 시작할 수 있습니다.

### 3. GC 없이, 손 관리 없이 메모리를 지킵니다

값의 수명은 **영역**(region, 블록이 끝날 때 한꺼번에 걷히는 메모리)과 **소유**로 정해집니다. 빌려 쓸 때는 «쓰는 쪽은 하나, 읽는 쪽은 여럿» 이라는 규칙을 컴파일러가 검사합니다. 바깥으로 새는 참조, 겹친 쓰기, 옮긴 값을 다시 쓰는 일은 컴파일할 때 거절됩니다.

한 걸음 더 나아가, 없애는 일이 **실패할 수 있는** 값은 조용히 없애지 않습니다. 파일은 닫다가도 실패할 수 있으므로(디스크가 가득 차면 마지막 버퍼를 못 씁니다), 열어 놓고 닫지 않은 프로그램은 컴파일되지 않습니다.

```lowent-거부: 파일을 열고 닫지 않는다 · E-OWN-INCOMPLETE
module forgot .

use files .

proc leak input fs cap file_system . output u8 . effects io .
do
  let o result files.handle files.file_error be files.open fs "notes.txt" 0 .
  guard is_ok o . else return 1 .
  var h owned files.handle be ok_value o .
  return 0 .
end
```

운영체제가 없는 작은 보드를 위해서는 자라지 않는 고정 메모리 창만 쓰는 길이 따로 있습니다. 그런 보드를 대상으로 지으면 힙이 필요한 코드는 거절됩니다.

### 4. 계약은 문서이자 검사이자 최적화의 근거입니다

`requires`(부르는 쪽이 지킬 조건), `ensures`(돌려줄 때 보장하는 것), `errors`(어떤 조건에서 어떤 실패를 내는지)를 서명에 적습니다. 값이 컴파일 시점에 정해져 있으면 그때 검사하고, 아니면 실행할 때 경계에서 검사합니다. 증명된 계약은 본문의 검사를 지웁니다. 아무것도 검사하지 않는 죽은 계약은 오히려 거절됩니다. 적어 둔 약속이 거짓이 될 틈을 주지 않으려는 것입니다.

### 5. 한 뜻에는 한 표기만 둡니다

문장은 낱말 하나로 시작해 피연산자를 뒤에 늘어놓고, 점(`.`)으로 끝납니다. `total + x` 가 아니라 `add total x` 입니다. 긴 수식은 `expr` 안에서만 중위로 쓸 수 있고, 뜻은 전위 표기와 똑같습니다. 키워드는 **43 개**로 닫혀 있고, 같은 일을 하는 두 번째 철자는 두지 않습니다.

이 규칙 덕분에 문법 전체를 정규식만으로 칠할 수 있고(에디터 하이라이터가 근사치가 아니라 정확합니다), 특수문자가 거의 없어 스마트폰 자판으로도 편하게 칠 수 있습니다. 진단에는 바뀌지 않는 코드가 붙고, `--diag-json` 을 주면 도구와 AI 에이전트가 읽기 좋은 JSON 한 줄로 나옵니다.

## 설계의 바탕

- **날렵함.** 작고 직교하는 코어 위에 넉넉한 표준 라이브러리를 둡니다. 새 기능은 «기존 원칙에서 저절로 나오는가, 새 축을 더하는가» 를 묻고, 새 축이면 넣지 않습니다.
- **비용이 보입니다.** 숨은 할당, 숨은 제어 흐름, 숨은 동시성이 없습니다. 느린 길에 남은 함수는 `--why-slow` 가 이유와 함께 알려 줍니다.
- **머리만 읽고 압니다.** 한 함수를 서명과 계약만으로 이해하고, 만들고, 검증할 수 있어야 합니다.
- **두 백엔드가 서로를 검사합니다.** 컴파일러는 같은 중간 표현을 VM 으로도 돌리고 C 로도 내보냅니다. 둘의 답이 다르면 사용자 잘못이 아니라 컴파일러 결함입니다. 컴파일러를 고칠 때마다 이 대조를 수만 건씩 돌립니다.
- **하드웨어의 현실에 맞춥니다.** 포인터가 16 비트이고 힙이 없는 보드까지 대상으로 삼습니다. 이식 가능한 SIMD, 인라인 어셈블리, MMIO 레지스터, 인터럽트 처리기, 양방향 C FFI 를 언어가 직접 다룹니다.
- **안 된 것은 안 됐다고 말합니다.** 이름만 받아 두고 아직 뜻이 없는 기능은 조용히 무시하지 않고 `W-NOT-YET` 으로 경고합니다.

## 지금 어디까지 왔나

Lowent 는 설계 문서에서 끝나지 않고, 실제로 도는 컴파일러와 함께 자라고 있습니다(컴파일러 1.3.0, 언어 개정 1.3).

- **컴파일러 `lowentc`** 는 C23 으로 쓰였고, 외부 의존은 들여온 라이브러리 하나(`proven_c_lib`, MIT)뿐입니다. 네이티브 코드는 C 로 내보내 시스템의 C 컴파일러로 짓습니다. 대상은 `x86_64`·`arm64`·`riscv64`·`cortex_m`·`mips_be` 입니다.
- **컴파일할 때 막는 것**: 효과와 권한 위반, 차용·수명 위반, 옮긴 값 재사용, 완결되지 않은 자원, 값을 잃을 수 있는 정수 변환, 모순되거나 죽은 계약.
- **실행할 때 막는 것**: 증명되지 않은 자리의 경계 검사, 넘침, 계약 위반. VM 과 네이티브가 같은 자리에서 같은 진단으로 멈춥니다.
- **증명된 것**: 순차 단편의 메모리 안전(해제 뒤 사용·매달린 참조 없음)과 데이터 경합 없음이 Coq 로 기계 증명되어 있습니다(공리·`admit` 없음). 다만 이 증명은 언어의 **모델**에 대한 것이고, 구현이 모델을 따르는지는 회귀 시험과 유계 모델 검사로 확인합니다. 무엇을 증명하지 **않았는지**도 매뉴얼의 한 장으로 따로 정리해 두었습니다.
- **검증 규모**: 회귀 시험 2,065 개, 표준 라이브러리 64 개 모듈. 매뉴얼의 모든 예제는 두 백엔드로 돌려 출력이 같은지 확인한 것입니다.

## 감수하셔야 할 것

Lowent 는 몇 가지를 일부러 포기했습니다. 고르시기 전에 아시는 편이 좋습니다.

- **낯선 겉모습.** 전위 표기와 점으로 끝나는 문장은 처음엔 어색합니다. `a + b * c` 대신 `add a (mul b c)` 라고 쓰는 데 익숙해지는 시간이 필요합니다.
- **말이 깁니다.** 효과, 권한, 폭 변환을 모두 적으므로 같은 일을 하는 C 코드보다 길어집니다. 그 대가로 읽는 쪽이 짐작할 일이 줄어듭니다.
- **상속, 람다, 예외가 없습니다.** 익숙한 도구가 빠져 있습니다. 대신 쓰는 방법은 바로 아래에 정리했습니다.
- **동적 디스패치가 아직 없습니다.** `dyn` 은 이름만 예약되어 있습니다. 여러 타입을 다루는 일은 컴파일 시점 제네릭으로 하므로, 쓰인 조합마다 코드가 만들어져 바이너리가 커질 수 있습니다.
- **생태계가 작습니다.** 패키지 저장소와 언어 서버(LSP)가 아직 없고, 표준 라이브러리 밖의 라이브러리도 적습니다.
- **호환성을 약속하지 않습니다.** 언어 개정마다 깨지는 변경이 있을 수 있습니다. 변경 기록과 서식기(`--fmt`)가 옮기는 일을 돕습니다.

## 없는 것 대신 이렇게 합니다

| 익숙한 도구 | Lowent 에서는 | 이유 |
|---|---|---|
| 클래스 상속 | 값을 지닌 `enum` 과 `match`, 여러 타입이 지키는 약속인 **트레이트**, 타입에 붙인 함수 | 부모를 거슬러 올라가며 뜻을 찾을 필요가 없습니다. `match` 는 빠진 갈래를 컴파일러가 잡아 줍니다 |
| 가상 함수 | 컴파일 시점 제네릭(쓰인 타입마다 전용 코드) | 가상 함수 표도 간접 호출도 없어, 비용이 부르는 자리에 보입니다 |
| 람다 | 이름 붙인 함수를 `pipe` 에 건넵니다 | 이름이 곧 설명이 되고, 무엇을 붙잡는지 숨지 않습니다 |
| 예외 | `result`·`option` 으로 실패를 값으로 돌려주고, `errors` 절로 어떤 실패가 나는지 서명에 적습니다 | 실패가 제어 흐름 뒤로 숨지 않고, 처리하지 않은 실패는 보입니다 |
| 가비지 컬렉터 | 영역, 소유, 빌리기, 할당기 | 멈춤도 런타임도 없이, 언제 걷히는지가 코드에 보입니다 |
| 전역 힙·전역 입출력 | 권한을 인자로 건네받습니다 | 무엇에 닿는지가 서명에 보입니다 |
| 스레드와 락 | 태스크, 채널, 액터, 병렬 되풀이 | 규율이 메모리 모델의 어려움을 대신 떠맡습니다 |

상속 대신 쓰는 두 가지를 짧게 보여 드리면 이렇습니다. 먼저 값을 지닌 `enum` 입니다.

```lowent
module shapes .

enum shape do
  circle r u32 .
  rect w u32 h u32 .
  dot .
end

fn area input s shape . output u32 .
do
  match s do
    case circle r . do return mul 3 (mul r r) . end
    case rect w h . do return mul w h . end
    case dot . do return 0 . end
  end
end
```

여러 타입이 같은 약속을 지키게 하려면 트레이트를 씁니다. 어느 함수가 불릴지는 컴파일할 때 정해집니다.

```lowent
module why .

trait shape do
  area input s self . output u64 .
end

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

fn double_area input comptime t type . input s t . output u64 .
  requires shape t .
do
  return mul 2 (method s area) .
end
```

람다 대신 이름 붙인 함수를 `pipe` 에 건넵니다. 중간 배열 없이 한 번에 흐릅니다.

```lowent
module lambda_fixed .

fn over2 input a u8 . output bool .
do
  return gt a 2 .
end

fn count_big input xs slice u8 . output u64 .
do
  return pipe xs do
    filter over2 .
    count .
  end .
end
```

## 앞으로 나아갈 길

지금 계획에 올라 있는 일들입니다. 날짜는 약속하지 않습니다.

- **문서 정리**: 매뉴얼과 명세의 어려운 장을 쉬운 설명과 도해로 다시 씁니다.
- **`pipe` 의 표현력**: 바깥 값을 함께 건네는 지역 함수를 들여, 지금 `while` 로 쓰는 반복을 더 많이 `pipe` 로 옮길 수 있게 합니다.
- **동적 디스패치와 선택형 런타임**: `dyn` 에 뜻을 주고, 필요한 프로그램만 골라 쓰는 작은 동적 런타임을 둡니다.
- **비동기 입출력**: 지금의 reactor(poll·epoll·POSIX AIO)에 io_uring·IOCP 백엔드와 워크 스틸링(work-stealing) 스케줄러를 더합니다.
- **증명의 폭**: 약한 메모리 순서(atomics)까지 증명을 넓힙니다.
- **웹 판**: 매뉴얼과 명세를 웹에서 바로 읽을 수 있게 합니다.

## 지향점

Lowent 가 끝내 닿고 싶은 곳은 **머리만 읽고 믿을 수 있는 코드**입니다. 함수의 서명이 그 함수가 할 수 있는 일의 전부를 말하고, 그 말이 거짓이 되면 컴파일러가 멈추는 언어입니다. 그런 코드는 사람이 짐작 없이 읽을 수 있고, AI 가 한 함수씩 안전하게 고칠 수 있으며, 작은 마이크로컨트롤러부터 서버까지 같은 규칙으로 돌아갑니다.

안전은 과장하지 않고 등급으로 말합니다. 증명된 것, 컴파일 때 막는 것, 실행 때 막는 것, 아직 막지 못하는 것을 나누어 적고, 그 경계를 조금씩 앞으로 옮기는 것이 이 프로젝트의 일입니다.

## 빠른 시작

C23 컴파일러(gcc 또는 clang)와 POSIX 셸만 있으면 됩니다. 다른 의존은 없습니다.

```sh
cd impl
make                  # → build/lowentc
make check            # 회귀 시험

build/lowentc --check hello.low              # 검사만 합니다
build/lowentc --run main hello.low           # VM 으로 실행합니다
build/lowentc --emit-c hello.low > hello.c   # 네이티브 빌드를 위해 C 로 내보냅니다
cc -O2 -o hello hello.c -lm -lpthread && ./hello main
```

프로젝트라면 뿌리에 `pkg.low` 매니페스트를 두고 `lowentc run`·`lowentc build` 를 쓰시면 됩니다. 둘 다 검사를 통과하지 못한 프로그램은 돌리지도 짓지도 않습니다.

## 더 읽을거리

| 자리 | 무엇 |
|---|---|
| [`docs/manual/`](docs/manual/README.md) | **Lowent 매뉴얼** — 책 형식의 안내서입니다(한국어·영어). 언어, 표준 라이브러리(모듈마다 한 쪽), 증명과 그 한계를 다룹니다. [웹](https://rubidus-api.github.io/lowent_lang/manual/html-ko/index.html)·[PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-ko/lowent-manual-ko.pdf)로 읽을 수 있고(영어판: [웹](https://rubidus-api.github.io/lowent_lang/manual/html-en/index.html)·[PDF](https://rubidus-api.github.io/lowent_lang/manual/pdf-en/lowent-manual-en.pdf)), GitHub 에서는 Markdown 판으로 볼 수 있습니다. 모든 예제는 두 백엔드로 돌려 확인했습니다 |
| [`docs/spec/canon/`](docs/spec/canon/) | 규범 명세입니다([웹](https://rubidus-api.github.io/lowent_lang/spec/html/) · [PDF](https://rubidus-api.github.io/lowent_lang/spec/pdf/lowent-spec.pdf)). 매뉴얼과 명세가 어긋나면 명세가 우선합니다 |
| [`docs/example/`](docs/example/) | 완결된 예제 |
| [`impl/`](impl/README.md) | 컴파일러 `lowentc` (C23) |
| [`lib/`](lib/) | 표준 라이브러리 (Lowent 로 작성) |
| [`skills/`](skills/) | AI 에이전트가 Lowent 코드를 쓸 때 읽는 안내 |

## 라이선스

MIT — [`LICENSE`](LICENSE). 들여온 [`proven_c_lib`](impl/vendor/proven/VENDORED.md) 도 MIT 입니다.

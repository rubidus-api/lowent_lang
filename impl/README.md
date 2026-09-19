# lowentc — 로우엔트 컴파일러 (`impl/`)

로우엔트 언어의 C23 구현이다. 소스 한 벌에서 **두 가지 실행 경로**가 나온다:

- VM 이 IR 을 바로 돌린다 (`--run`).
- C 백엔드가 C 소스를 내고 시스템 C 컴파일러가 네이티브로 짓는다 (`--emit-c`).

두 경로는 같은 답을 내야 하고, 다르면 컴파일러 결함으로 본다.

> **실험 단계다.** 명령줄 옵션·진단 코드·내부 구조는 예고 없이 바뀐다. 언어의 규범은 [`../docs/spec/canon/`](../docs/spec/canon/) 이고,
> 이 파일은 컴파일러를 읽거나 고치려는 사람을 위한 **안내도**다.

## 짓기와 시험

```sh
make            # 디버그 빌드 → build/lowentc
make check      # 공개본의 회귀 시험: 단위 시험 + 표준 라이브러리 검사 + VM≡네이티브 + 거절 진단
make test       # 단위 시험만 (tests/run_tests.c)
make asan       # ASan/UBSan 빌드
make dist       # 정적 배포본 (dist/lowent-linux-x64/)
make clean
```

- **필요한 것:** C23 컴파일러(gcc 또는 clang)와 POSIX 셸, 이것뿐이다.
- 기반 라이브러리 [`proven_c_lib`](vendor/proven/VENDORED.md)(MIT)는 `vendor/proven/` 에 복사해 들여왔다. 원본은 고치지 않는다.
- 네이티브 경로를 시험할 때 `-lm -lpthread` 를 링크한다.

`make check` 가 무엇을 보는지는 [`tests/smoke.sh`](tests/smoke.sh) 머리말에 있다. 변경을 보내기 전에 이것이 통과해야 한다.
- 새 op 을 VM≡네이티브 목록에 넣으려면 [`tests/smoke/run.tsv`](tests/smoke/run.tsv) 에 한 줄을 더한다.
- 새 거절 사례는 `tests/smoke/err_*.low` 에 넣고, 둘째 줄에 `rem expect: <진단 코드>` 를 적는다.
- 더 큰 회귀 시험 묶음(골든)과 두 백엔드 차등 대조는 개발 저장소에 있다. 공개본의 목록도 거기서 함께 돈다.

## 쓰기

```sh
build/lowentc --check FILE.low              # 검사만 (타입·계약·효과·소유·능력)
build/lowentc --run OP FILE.low ARGS…       # VM 으로 op 하나를 돌린다 (슬라이스 인자는 [1,2,3])
build/lowentc --emit-c FILE.low > out.c     # C 를 낸다 → cc -O2 out.c -lm -lpthread
build/lowentc --test FILE.low               # test 블록을 돌린다
build/lowentc --fmt FILE.low                # 정규형으로 다시 찍는다
build/lowentc --diag-json --check FILE.low  # 진단을 JSON 한 줄씩 (rule · file · span · msg · repair)
build/lowentc                               # 전체 옵션 도움말
```

진단 한 줄은 `파일.low:줄:열 코드: 문장` 모양이다. 문장은 편의이고, 도구가 기대어도 되는 약속은 **코드**(`E-TYPE-FIELD` 같은 것)다.

## 파이프라인

```
소스 ─ 어휘 분석 ─ 점-닫힘 CST ─ 인자 수 정규화(나무) ─ using 해석 ─ 단형화
     ─ 검사(효과·능력·가시성 / 타입 / 계약 / 영역·배타) ─ 스택 IR
     ─┬─ VM (--run, --test)
      └─ C 백엔드 (--emit-c) ─ cc
```

- **표면은 닫혀 있다.** 키워드와 빌트인 op 은 고정된 목록이다.
  - keywords: **43** (정본 §6.1·부록 A)
  - 빌트인 op: **188** (`src/low_arity.h` · 정본 부록 D)
  - 머리 낱말이 피연산자를 몇 개 먹는지는 `low_arity.h` 한 곳이 답한다.
- **검사는 IR 전에 끝난다.** 검사를 통과하지 못한 프로그램은 IR 로 내려가지 않는다. 검사가 사실로 믿은 계약(`requires` 등)은 프로그램 경계에서 실행 시 검사로 남는다.
- **타입이 정해진 op 은 자연스러운 C 로 낮춘다.** 정수는 `long long`, 슬라이스는 배열로 내고, 나머지는 태그 달린 값 스택으로 간다.
  - `--why-slow` 는 태그 경로에 남은 op 과 그 까닭을 말한다.
  - `--no-fast` 는 이 최적화를 끄는 대조 스위치다.
- LLVM 같은 외부 IR 은 쓰지 않는다. 명령어 선택은 C 컴파일러가 한다. 이 컴파일러가 스스로 하는 최적화는 네 가지다:
  - 계약으로 증명된 검사를 없앤다.
  - 단형화한다.
  - comptime 에 접는다.
  - 타입을 따라 낮춘다(위 항목).

## 소스 안내 (`src/`)

| 파일 | 맡은 일 |
|---|---|
| `main.c` | 명령줄 처리, 단계 순서, 진단 출력(사람용·JSON), `build`/`install`/`sign` 같은 부속 명령 |
| `low_token.*` · `low_lex.*` | 토큰과 키워드 표, 어휘 분석 |
| `low_cst.*` · `low_cst_priv.h` | 점-닫힘 CST 파서, 인자 수 정규화(`low_nest`), 모듈 연결 |
| `low_arity.h` | 빌트인 op 의 인자 수 표 (유일한 출처) |
| `low_using.c` | `using` 절(객체마다 얼로케이터 고르기), 입력 `array N T` 의 길이 계약 |
| `low_mono.c` | 제네릭 틀에서 구체 인스턴스를 찍는 단형화 |
| `low_check.*` | 효과·능력·가시성·계층·FFI·동시성 규칙 (가장 큰 검사기) |
| `low_typecheck.*` | 타입 검사: 종류, 정수 폭·부호, struct 칸, enum |
| `low_contract.*` | 계약 검사: `requires`/`ensures`/`errors` |
| `low_region.*` | 영역·탈출·배타 참조 검사 |
| `low_iv.c` · `low_smt.*` | 구간 분석과 선형 정수 반박기 (계약으로 검사를 없애는 근거) |
| `low_ir.*` · `low_ir_stmt.c` · `low_ir_priv.h` | 스택 IR 로 낮추기, 내용 해시 |
| `low_vm.c` · `low_value.h` | 태그 값 VM, 빌림 스택 |
| `low_cbe.*` · `low_cbe_prelude.*` | C 백엔드와, 내보내는 C 에 들어가는 런타임 |
| `low_diag.h` · `low_repair.*` | 진단 구조체, 코드별 수리 제안 표 |
| `low_doc.*` | `--doc` 문서 생성 |
| `low_pkg.*` | `pkg.low` 매니페스트 |
| `low_blake3.*` · `low_sha256.h` · `low_sha512.h` | 내용 해시·서명에 쓰는 해시 |
| `low_hwm.*` · `low_hostfault*` | 고정 표의 최고수위 계측, 호스트 실패 주입(시험용) |
| `low_version.h` | 버전 (한 곳) |

## 시험 자료 (`tests/`)

| 자리 | 무엇 |
|---|---|
| `run_tests.c` | 단위 시험 (`make test`) |
| `smoke.sh` · `smoke/` | 공개 회귀 시험 (`make check`) |
| `vm_*.low` 와 그 밖의 `*.low` | 기능별 픽스처. 대부분 파일 머리 `rem` 에 무엇을 지키는지 적혀 있다 |
| `prog/` | 실제 쓰임에 가까운 작은 프로그램들 (`grep`, `json`, `chcheck` …). ☞ **제품**은 여기 살지 않는다 — `apps/` 로 갔다(`lowdiff`·`lowget`) |
| `mod/` | 여러 모듈을 잇는 시험 |

## 코드 규칙

- **메모리:** CST 노드는 proven arena 에서 받는다. 늘어나는 배열은 그 arena 를 받치는 할당자를 쓴다.
- **문자열:** `proven_u8str_view_t` 를 쓴다. 토큰은 소스 버퍼를 가리키는 (길이, 포인터) 조각이고, NUL 로 끝나는 사본을 만들지 않는다.
- **오류는 값이다.** 결과 구조체와 진단 배열로 돌려주고, 실패할 수 있는 함수에는 `[[nodiscard]]` 를 붙인다.
- **진단:** 문장을 조립할 때는 `detail` 에 쓰고 `msg` 는 NULL 로 둔다. 읽는 쪽은 `low_diag_text()` 를 쓴다. 진단 배열이 늘어나 옮겨져도 포인터가 끊기지 않게 하려는 규칙이다.
- **이름:** `low_` 접두사, snake_case, `_t` 타입 이름을 쓴다(proven 의 규칙과 같다).
- **주석:** 코드의 `★` 주석은 그 자리에서 실제로 난 결함과 그 까닭을 적은 기록이다. 고치기 전에 읽는다.

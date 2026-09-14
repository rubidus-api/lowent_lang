# 1. 빌드와 실행

[← 목차](README.md)

## 빌드 환경

Lowent 컴파일러의 이름은 **`lowentc`** 다. C23 로 작성되었고, 벤더링된
`proven_c_lib`(`impl/vendor/proven`) 하나에만 의존한다. 그 밖의 외부 의존성은 없으며 링크는
`-lm` 뿐이다.

```sh
cd impl          # 반드시 impl/ 에서 빌드한다
make             # 디버그 빌드 → build/lowentc
```

| 명령 | 결과 |
|---|---|
| `make` | 디버그 빌드 `build/lowentc` (`-std=c23 -g -O0 -Wall -Wextra -Wshadow`) |
| `make asan` | AddressSanitizer/UBSan 빌드 |
| `make dist` | `-O2 -DNDEBUG` **정적** 빌드 → `dist/lowent-linux-x64/{bin/lowentc,std/,keys/official/}` (glibc 버전 무관·폴더째 이동 가능) |
| `make test` | 단위 테스트 실행 |

`lowentc` 에는 **기본 모드가 없다** — 무엇을 할지 반드시 하나 고른다. 자주 쓰는 모드:

| 플래그 | 하는 일 |
|---|---|
| `--check` | 검사만 한다 (계약·효과·소유권·가시성·티어·능력·FFI). 방출 없음 |
| `--run OP [args…]` | VM 으로 op `OP` 를 실행한다. 슬라이스 인자는 `[a,b,c]` 로 준다 |
| `--emit-c` | 네이티브 C 소스를 **stdout** 으로 방출한다 |
| `--emit-h` | C 헤더를 방출한다(다른 언어/C 가 우리를 부를 때 · `--emit-c` 포함) |
| `--diag-json` | 진단을 JSON 한 줄씩 낸다(도구·AI 용) |
| `--fmt` | 정규형(canonical form)으로 출력한다 |
| `--ir` | 스택 IR 을 덤프한다 |
| `--test` | `test` 블록을 실행한다 |
| `--no-main` | **라이브러리로 낸다** — `main` 도 CLI 디스패처도 안 낸다(아래) |

진단(오류·경고)은 **stderr** 로, `--emit-c` 의 C 소스는 **stdout** 으로 나간다. 그래서
`--emit-c prog.low > prog.c` 가 자연스럽다.

### `--no-main` — 남의 프로그램 **안에** 넣을 때

기본 방출은 **프로그램**이다: `main` 과 CLI 디스패처(`bin op args…`)가 함께 나온다. 그
디스패처는 태그 경로를 붙잡고, 태그 경로는 자기 풀들을 붙잡는다. 그래서 **두 수를 더하는
프로그램 하나가** 이만큼을 진다:

```text
                text     rodata          bss     전역 심볼
기본           2,992    197,350    1,736,704     add2  main
--no-main         48         50            0     add2
```

`--no-main` 은 그 짐을 통째로 없애고 **`export` 한 op 의 C 진입점만** 남긴다.
방출된 `.c` 를 남의 빌드에 그대로 넣으면 된다(`--emit-h` 로 헤더도 함께).

> ★ **`export` 를 안 붙이면 아무 심볼도 안 나온다.** 위 표의 `add2` 는 `export fn add2` 다.
> 붙이지 않은 op 은 전부 `static` 이라 밖에서 못 부른다 — 그것이 기본값인 이유는
> *내보내는 것은 선택이지 부작용이 아니어야* 하기 때문이다.

> ☞ 하한선(`--target cortex_m`)은 **이미** 디스패처를 안 낸다 — 거기서는 `--no-main` 이
> 필요 없다. 이 플래그는 **호스트에서 라이브러리를 낼 때**의 것이다.

## hello, entropy

가장 작은 출력 프로그램:

```lowent
module hello .

proc main output u8 . input out cap io . effects io . do
  return narrow u8 (write_out out 1 "hello, entropy!\n") .
end
```

두 가지로 돌릴 수 있다 — 같은 답을 낸다:

```sh
# ① VM 으로 바로 실행
$ build/lowentc --run main hello.low
hello, entropy!

# ② 네이티브 C 로 방출 후 컴파일·실행
$ build/lowentc --emit-c hello.low > hello.c
$ cc -O2 -o hello hello.c -lm
$ ./hello main
hello, entropy!
```

읽을 점이 여럿이다:

- **출력에는 능력이 필요하다.** 표준출력에 쓰려면 `input out cap io .` 로 **`io` 능력**을
  받아야 한다. 능력 없이는 아무것도 새어나가지 못한다 — Lowent 에는 주변 권한(ambient
  authority)이 없다.
- `write_out <cap io> <fd> <bytes>` 가 유일한 출력 통로다. `fd` `1` 은 표준출력.
- `write_out` 은 **쓴 바이트 수**를 돌려준다. `main` 의 반환값(`output u8`)은 **프로세스
  종료 코드**로 관측된다 — 그래서 `narrow u8` 로 좁혀 돌려준다. (`hello, entropy!\n` 은 16
  바이트이므로 종료 코드는 16.)
- `effects io .` — 이 op 이 입출력이라는 효과를 낸다고 **선언**한다. 선언과 실제가 어긋나면
  검사에서 걸린다([3장](03-ops.md)).

---

[← 목차](README.md) · [다음: 2. 문법의 기본 →](02-syntax-basics.md)

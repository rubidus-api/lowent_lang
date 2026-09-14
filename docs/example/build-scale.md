# 예제 — 대규모 강제 분할 빌드 (커널 급, RFC-0036)

커널/대형 DB 급 가상 프로젝트 `nucleus`. **고정 시작점 + 강제 의미-카테고리 분할 + 한도 하위분할 + Kconfig 구성.**
★강제 주체 = *언어/규약/툴*(정적 lint)·나누는 주체 = *작성자*·**AI 는 빌드 비참여**(읽기/쓰기 편의일 뿐). (RFC-0036.)

---

## 1. 고정 시작점 — `pkg.low` (항상 작음)

```
package
  name    "nucleus" .
  version "6.2.0" .
  license "GPL-2.0" .
end

parts                                rem 카테고리 조각 인덱스(고정 의미 단위·role-typed)
  deps     build/deps.low .
  targets  build/targets.low .
  options  build/options.low .        rem 한도 초과 → options/ 디렉토리로 강제 하위분할(아래 4)
  steps    build/steps.low .
end

members                              rem 서브시스템 = 워크스페이스(각자 pkg.low·재귀)
  net      subsys/net .
  drivers  subsys/drivers .
  fs       subsys/fs .
end
```
독자/툴/AI 는 *언제나 여기서 시작* → parts/members 맵 보고 *필요 조각만* 읽음.

## 2. role-typed 카테고리 조각 (그 내용만 허용)

```
build/deps.low      rem role=deps: `use` 만
  use libz   from pub.lowent.dev/libz .
  use crypto from pub.lowent.dev/crypto .

build/targets.low   rem role=targets: config 변종만
  config x86_64  target x86_64-none .   profile freestanding .  entry start .  end
  config arm64   target aarch64-none .  profile freestanding .  entry start .  end
  config riscv   target riscv64-none .  profile freestanding .  entry start .  end

build/steps.low     rem role=steps: 빌드타임 op 만
  generate gen_syscall_table into build/gen .   rem 시스콜 테이블 생성
  embed "firmware/blob.bin" as fw .              rem 펌웨어 임베드(RFC-0035 file 처리기)
```

**role 강제(툴·정적):**
```
build/deps.low 에:
  use libz from … .        rem ✓ role=deps
  generate x into y .      rem ✗ 빌드 에러: deps 파일에 step 금지(role 위반) — 의미가 파일 경계와 일치
```

## 3. 구성 — Kconfig 류 `option` (서브시스템 분산)

```
build/options/core.low     rem role=options (core)
  option smp      bool   default on    help "대칭 멀티프로세싱" .
  option nr_cpus  int    default 256   depends smp .
  option preempt  choice none voluntary full   default voluntary .
  option hz       choice 100 250 300 1000   default 250 .

build/options/net.low      rem role=options (net 서브시스템)
  option net      bool   default on    help "네트워킹 스택" .
  option ipv6     bool   default on    depends net .
  option tcp_cong choice cubic reno bbr   default cubic   depends net .
  option net_buf_kb int  default 256   depends net .
```

## 4. 한도 → 강제 하위분할 (거의 강제·opt-out 가능)

```
build/options.low 가 한도(예: 200 옵션) 초과 → 빌드 lint(기본 강제):
  "options 한도 초과 → 서브시스템별 분할 필요"
  ⟹ 고정 규칙:  build/options.low →  build/options/{core,net,drivers,fs}.low
                net 도 초과 →  build/options/net/{a-m,n-z}.low  (결정적 버킷)
  그래서 옵션 수천이어도 *어떤 파일도 토큰 예산 안 넘김*.
명시 opt-out:  pkg.low 에  allow_monolith   → 단일 유지 가능("거의 강제지 불가능 아님").
강제 주체 = 툴(정적 lint)·AI 아님.
```

## 5. 서브시스템 멤버 (재귀) — `subsys/net/pkg.low`

```
package  name "nucleus.net" .  version "6.2.0" .  end
parts                                rem 서브시스템도 자체 parts 재귀
  deps    deps.low .
  options options.low .
  steps   steps.low .
end
```

## 6. 해결된 구성 — `lowent.config` (생성물·손-편집 아님)

```
rem `lowent config`(menuconfig 등가 TUI)가 의존/기본 지키며 생성. Linux .config 류 평탄·기계 소유.
config
  smp on .        nr_cpus 64 .    preempt voluntary .   hz 250 .
  net on .        ipv6 on .       tcp_cong bbr .         net_buf_kb 512 .
  drivers_e1000 on .            fs_ext4 on .           fs_btrfs off .
end
```
빌드 파라미터화: `fs_btrfs off` → btrfs 모듈 dead·미링크. content-addressing 이 이 구성 그래프를 재현·증분.

## 7. 선택적 읽기 — 구조의 *정적* 산물 (빌드 아님)

```
과업: "TCP 혼잡제어 옵션이 뭐지?"
  독자/도구/AI 가 읽는 것:  pkg.low(작음) → parts.options → build/options/net.low (그 한 조각)
                           + lowent.config 에서 tcp_cong grep → bbr.
  안 읽는 것:  drivers/fs/deps·전체 트리. ⟹ 커널 급이라도 *작은 루트 + 의미 조각 하나*.
★빌드는 별개:  컴파일러가 빌드 시 전체 트리를 해시캐시·증분 처리. AI 는 *비참여* — 위 선택 읽기는 *읽기/작성 편의*지 빌드 단계 아님.
```

## 무엇을 보여주나

```text
고정 시작점     pkg.low(package+parts+members 인덱스) — 항상 작고, 모든 진입의 뿌리.
강제 의미-분할   카테고리 role-typed(deps/targets/options/steps/members), 내용 강제·자유 위치 금지(위반=에러).
한도 하위분할    초과→고정 규칙(서브시스템→버킷)·거의 강제·allow_monolith opt-out → 어떤 파일도 토큰 예산 내.
Kconfig 구성    option(타입/기본/의존, 서브시스템 분산) + lowent.config 해결본(menuconfig 등가) + 조건 빌드.
선택적 읽기      구조의 *정적* 산물 — 독자/도구/AI 가 의미 조각만. *빌드 개입 아님*. (RFC-0014 토큰 효율 정신.)
행위자          강제=언어/규약/툴(정적)·분할=작성자·AI=정적 수혜자(읽기/쓰기 편의). AI 빌드 비참여.
```

## 정직한 경계

```text
- parts/members/option/lowent.config·role 강제·한도 lint·allow_monolith = RFC-0036(EBNF·기본값 §8 후속).
- generate/embed=RFC-0035·0032·config 변종=RFC-0031·content-addressing 증분=RFC-0012/0023.
- 옵션 의존 해결(순환/충돌)·menuconfig UX·구성×크로스 조합·증분 영향분석 = RFC-0036 §8 미해결.
- nucleus/숫자/서브시스템은 예시. lowent.config 는 *생성물*(RFC-0034 부류)·손-작성 .low 아님.
```

> 요지: 커널 급이면 *언어/규약/툴* 이 빌드 정보를 *의미별로 거의 강제 분할*(role·한도·계층)시키고 거대 구성은 *Kconfig
> option + lowent.config* 로 다룬다 — 그 결과 독자/AI 가 *작은 루트 + 의미 조각만* 읽어 토큰 효율적일 뿐, **AI 가 빌드에 끼어드는 게 아니다**.

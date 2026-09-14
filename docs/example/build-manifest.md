# 예제 — pkg.low 매니페스트 (좋은 기본 틀 + 원할 때 커스텀)

빌드 도구의 *전용 매니페스트* `pkg.low`(Lowent 문법, TOML+Make 역할 겸함, RFC-0031/0033). 핵심: **기본은 0 설정으로
동작, 일탈만 적는다**(Cargo식·anti-Gradle). 아래는 *최소* → *세밀 커스텀* 스펙트럼. (도구 동작 = RFC-0033.)

---

## 0. 최소 — pkg.low *없이도* 됨 (zero-config)

```
src/main.low:
  proc main  input out cap io . .  output result unit io_error . .  effects io .
  do  try write out "hello" .  return ok void . .  end
```
```text
$ lowent run          # pkg.low 없음 → 내장 기본(profile native·host target·entry main·표준 레이아웃) → 그냥 실행
```
관례 레이아웃(`src/`·entry `main`)만 지키면 **설정 0**. (RFC-0033 §4 D9·RFC-0031 §6.5.)

## 1. 보통 — deps + 크로스 config

```
build
  use json from pub.lowent.dev/json .   rem 공인 라이브러리(공식 키 서명 검증, RFC-0033 D6)
  feature actor on .

  config mcu                            rem  lowent build mcu
    profile freestanding .
    target arm-none-eabi .
    entry reset .
  end
end
```
host 빌드는 config 안 적음 → 내장 기본. mcu 만 일탈로 명시.

## 2. 세밀 — 소스 정책·레이아웃 일탈·빌드타임 동작

```
build
  rem ── 의존 (라이브러리 3등급, RFC-0032 D3·0033 D6/7) ──
  use json   from pub.lowent.dev/json .    rem 공인(official 키)
  use corp   from git.corp.local/lib .     rem 사설(private 키 + auth)
  use mylib  from ../mylib .                rem 개발중(로컬 경로, 키 면제·파일 존재만)

  rem ── 소스 정책: 어디서 받나 세밀 제어 (RFC-0033 D8) ──
  source policy official private .          rem 공식 + 등록 사설만(URL 임의 금지). 기본은 official-만.
  source allow https://mirror.corp/lowent . rem 추가 허용 미러

  rem ── 레이아웃 일탈만 (없으면 관례 src/res/data/doc, RFC-0033 D9) ──
  src      code .                          rem 소스 디렉토리 = code/(기본 src/ 대신)
  platform windows  src code/win .         rem 플랫폼 분기(common + win)
  res      assets .                        rem 리소스 = assets/
  out      build/out .                     rem 산출물 위치

  rem ── 빌드타임 동작: 선언적 sugar (Gradle DSL 아님, RFC-0033 D10) ──
  read-config "version.txt" as ver .       rem 설정값 파일서 읽기(빌드 시)
  embed "assets/table.bin" as lut .        rem 파일→const slice(comptime)
  generate make_atlas into build/res .     rem 빌드 op 이 리소스 생성(cap fs)
  mkdir build/out .
  clean build/tmp .

  rem ── 변종 ──
  config mcu
    profile freestanding .
    target arm-none-eabi .
    entry reset .
    source policy local $LOWENT_MCU_LIB .  rem MCU 는 로컬 벤더만(에어갭)
  end
end
```

## 3. 빌드타임 op (임의 동작 — capability-게이트, RFC-0033 D10)

흔한 건 sugar(§2), 임의 동작은 *빌드타임 Lowent op*(cap fs/net·effect 가시 — 별도 스크립트 언어 0):
```
proc make_atlas                         rem `generate make_atlas` 가 부르는 빌드 op
  input fs cap fs . .
  input src dir .
  input dst dir .
  output result unit build_error . .
  effects io state .
do
  rem … src 의 이미지들 읽어 atlas.res 생성 …(cap fs 로 게이트·effect 가시)
  return ok void . .
end
```

## 무엇을 보여주나 (RFC-0033)

```text
좋은 기본 틀     §0: pkg.low 없어도 관례(src/·main·native·host)로 *그냥* 동작. 0 설정(Cargo convention).
일탈만 명시      §1/§2: 기본과 다른 것만 적음(deps·config·레이아웃·정책). 적은 줄·읽기 쉬움.
라이브러리 3등급  공인(official 키)·사설(private 키+auth)·개발중(로컬·키 면제). use … from <source>.
소스 정책 세밀    official-만 / +사설 / 단일 URL / 로컬-만 / allow 화이트리스트. 기본=공식만(안전), 완화 opt-in.
빌드타임 동작     선언 sugar(embed/generate/copy/mkdir/clean/read-config) + 임의=빌드타임 op(cap fs). *DSL 아님*.
진본            받은 파일 = TLS·서명(등급 키)·해시·접근 4층 검증(어디서 받든·미러 신뢰 불요).
anti-Gradle      빌드=선언+capability op. Turing-완전 빌드 언어·플러그인·phase 없음. 통합·간결.
```

## 정직한 경계

```text
- 도구(lowc/lowpm/lowdoc)·내장 페처/curl 폴백·키 검증·자동페치 = RFC-0033(툴 구현, post-MVP/UNPROVEN R8 IO).
- source/policy/embed/generate/read-config 등 pkg.low sugar 문법 = RFC-0031/0033 후속(EBNF 미확정 — 본 예제는 형 가정).
- cap fs/net·dir 타입·빌드타임 op = RFC-0011/0032. 디렉토리 표준(전역/~/.lowent/플랫폼)·우선순위 = RFC-0033 §4.
```

> 요지: `pkg.low` 는 *Lowent 한 substrate* 로 TOML(설정)+Make(빌드 스텝) 역할을 겸하되, **기본은 0 설정·일탈만 명시**.
> Cargo 의 convention-over-config 를 따르고 Gradle 의 DSL 복잡성을 피한다 — *좋은 고정 틀 + 원할 때만 커스텀*.

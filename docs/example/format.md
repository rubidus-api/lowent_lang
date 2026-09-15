# 예제 — 포매팅 출력 라이브러리 (버퍼 조립 · capability io)

세 번째 결: **I/O + 버퍼 빌더 + 합성 + capability**. 포매팅은 *호출자 버퍼*에 조립(숨은 alloc 0,
`effects none`)하고, 실제 출력만 `cap io` 권한으로 분리(post-MVP). 실행-유도 오류(overflow)는 F2 대로
errors 절 없이 본문 `return error`. (라이브러리=MVP, print=post-MVP)


> **2026-09-13 다시 씀 (WO-0218).** 아래 코드는 지금의 언어로 고쳐 `--check` 를 통과한다(`usize`→`u64`, `loop`→`while true .`,
> 실행-유도 오류도 errors 절에 이름을 적는다, 권한 입력이 데이터보다 먼저, 뿌리 할당은 `alloc_bytes`). 이 설계를 실제로 쌓은
> 표준 라이브러리는 [`lib/fmt.low`](../manual/md-ko/) (매뉴얼 부록 E 의 `fmt` 쪽) 다.

```
module fmt .

type scratch u64 . .

enum write_error do
  overflow .         rem 버퍼 부족 — 실행-유도 오류(조건 없이 errors 절에 이름만)
end .

rem ── 저수준 라이터: 모두 (buf, pos) → 새 pos 를 돌려주는 순수 빌더(숨은 alloc 0) ──

proc write_byte
  input buf mut slice u8 . .
  input pos u64 .
  input b u8 .
  output result u64 write_error . .
  effects none .
  errors overflow .
do
  guard lt pos len buf . . else return error overflow . . .
  set (index buf pos) b .
  return ok add pos 1 . . .
end

proc write_str
  input buf mut slice u8 . .
  input pos u64 .
  input s slice u8 . .
  output result u64 write_error . .
  effects none .
  access s sequential .
  errors overflow .
do
  var p u64 be pos .
  for c s do
    set p try write_byte buf p c . .        rem overflow 자동 전파
  end
  return ok p . .
end

rem [lo, hi) 바이트 역순 — write_u32 보조
proc reverse_bytes
  input buf mut slice u8 . .
  input lo u64 .
  input hi u64 .
  output void .
  effects none .
do
  guard lt lo hi . else return .
  var i u64 be lo .
  var j u64 be sub hi 1 . .
  while lt i j . do
    guard lt j (len buf) . else return .
    let t u8 be index buf i . .
    set (index buf i) (index buf j) .
    set (index buf j) t .
    set i add i 1 . .
    set j sub j 1 . .
  end
end

rem u32 10진수 — LSB 먼저 쓰고 뒤집기(do-while = while true + guard)
proc write_u32
  input buf mut slice u8 . .
  input pos u64 .
  input n u32 .
  output result u64 write_error . .
  effects none .
  errors overflow .
do
  let start u64 be pos .
  var p u64 be pos .
  var v u32 be n .
  while true . do
    let d u32 be mod v 10 . .
    set p try write_byte buf p (narrow u8 (add d 48)) . .   rem '0' = 48, d≤9 → 안전 narrow
    set v div v 10 . .
    guard gt v 0 . else break .
  end
  reverse_bytes buf start p .
  return ok p . .
end

rem ── 고수준: 라이터들을 합성 ──

struct point do
  x u32 .
  y u32 .
end

rem "(x, y)" 를 buf 에 조립, 총 길이 반환. 문자열 리터럴 = slice u8(정적).
proc format_point
  input buf mut slice u8 . .
  input p point .
  output result u64 write_error . .
  effects none .
  errors overflow .
do
  var at u64 be 0 .
  set at try write_byte buf at 40 . .            rem '('
  set at try write_u32 buf at field p x . . .
  set at try write_str buf at ", " . .
  set at try write_u32 buf at field p y . . .
  set at try write_byte buf at 41 . .            rem ')'
  return ok at . .
end

rem ── 실제 출력: io effect + capability ──
rem cap io 없으면 호출 불가(ambient authority 없음). 권한이 먼저 온다(WO-0217). scratch 에 임시 버퍼(가시).
proc print_point
  input sink cap io .
  input temp region scratch . .
  input p point .
  output result void write_error . .
  effects io alloc .
  errors overflow .
do
  let g option mut slice u8 . . be alloc_bytes temp capacity 64 .
  guard is_some g . else return error overflow . . .
  let buf mut slice u8 . be some_value g .
  let n u64 be try format_point buf p . .
  let w u64 be write_out sink 1 (subslice buf 0 n) .     rem cap 경유 출력(1 = stdout)
  return ok .
end
```

## 보이는 Lowent 특징

```text
숨은 alloc 0     write_*/format_point 은 effects none — *호출자 버퍼*에 씀. 라이브러리가 메모리 정책을 안 정함.
비용 분리        포매팅(effects none) vs 출력(print_point: effects io alloc). 누가 무엇을 쓰는지 시그니처에.
capability       출력은 `cap io` 보유자만(권한 경계). 숨은 stdout 없음 — ambient authority 거부.
합성             format_point = 저수준 라이터의 합성. 각 단계 `try` 로 overflow 전파(한 줄, `?` 없이).
실행-유도 오류   overflow 는 입력으로 못 닫음 → errors 절에 **이름만**(조건 없이), 본문 return error(F2). result write_error.
명시 변환        narrow u8 (add d 48) — 암묵 coercion 없음(G2). 문자열 리터럴=slice u8(정적, 읽기).
```

## 세 예제 대비 (Lowent 의 결 스펙트럼)

```text
              rpn(eval)         bsearch          fmt(format/print)
성격          stateful 평가     pure 탐색         I/O·버퍼 빌더·합성
메모리        region+stack      무할당            호출자 버퍼(라이브러리) / scratch(print)
effect        alloc             none             none(포맷) → io alloc(출력)
실패          result(실행유도)  option(부재)      result write_error(실행유도)
권한          —                 —                 cap io(권한 경계)
대표 특징     명시 region·EXCL  계약 술어 op      capability·비용 분리·합성
```

> 포매팅 같은 흔한 라이브러리도 Lowent 에선 *메모리(누구 버퍼)·권한(누가 출력)·비용(누가 alloc)* 이 전부
> 시그니처에 드러난다 — "숨은 stdout·숨은 힙" 이 없다(P2·P3·capability).

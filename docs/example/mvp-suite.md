# Lowent MVP — 컴파일 가능한 예제 20개 (파서 계약 / 문법 닫기 게이트)

> SPEC-MVP.md §3 의 게이트: **구현 전 이 20개로 문법을 닫는다**(설명/EBNF/예제 불일치 0 확인 후 파서 착수).
> 전부 **MVP 부분집합만** 사용(match/defer/comptime/pipe/actor/await/list/ensures/set/bitset/owned 없음).
> op 본문은 `return`(op 탈출), do/end 값-블록·if/loop 식은 `give`/`break <v>`. 주석 = `rem`.
>
> **2026-09-13 다시 씀 (WO-0218).** 아래 예제 20 개는 **지금의 언어로** 고쳐 적었고 전부 `--check` 를 통과한다.
> 그 사이 사라진 낱말(`give`·`loop`·`for … in`·라벨 `as`·`errors … when`·`type … is`·`range`·`child_of`·`alloc … count`)은
> 한 가지 철자로 바뀌었고, 절은 한 차례로 적는다(WO-0217). 제목의 괄호와 맨 아래 «커버리지 행렬»·«미확정» 은
> 이 게이트를 닫던 **당시의 기록**으로 남긴다 — 지금 쓰는 꼴은 예제 본문이 말한다. if-식은 문법 부록에 있으나
> 쓸 수 없어(`E-IF-VALUE` — `if` 는 문이다) 03 은 if-문으로 적었다.

---

### 01 — 최소 fn (module·calc·output·return)
```
module demo .

fn answer
  output u32 .
do
  return 42 .
end
```

### 02 — 전위 산술 + `expr` 중위 섬 (add/mul·expr·let 타입)
```
module mathx .

fn poly
  output i32 .
  input x i32 .
  input a i32 .
  input b i32 .
do
  let t i32 be expr a * x + b . .      rem 중위 섬: (a*x)+b
  return t .
end
```

### 03 — if 로 값 고르기 (if-stmt·var·set — 당시: if-expr·give)
```
module signx .

fn classify
  output i32 .
  input n i32 .
do
  var s i32 be 1 .
  if lt n 0 . do set s 0 . end
  return s .
end
```

### 04 — bool 논리 (and/le/ge·전위 논리)
```
module rangechk .

fn in_range
  output bool .
  input x i32 .
  input lo i32 .
  input hi i32 .
do
  return and (ge x lo) (le x hi) .
end
```

### 05 — while + var + set (var·while·set·add)
```
module accum .

fn sum_to
  output u32 .
  input n u32 .
do
  var total u32 be 0 .
  var i u32 be 0 .
  while lt i n . do
    set total add total i . .
    set i add i 1 . .
  end
  return total .
end
```

### 06 — for over slice (for-in 원소·len 없이 원소 순회)
```
module sliceop .

fn sum_slice
  output u32 .
  input xs slice u32 . .
  access xs sequential .
do
  var total u32 be 0 .
  for x xs do
    set total add total x . .
  end
  return total .
end
```

### 07 — while 로 찾기 (while·if·return some/none — 당시: loop 식·break 값)
```
module findx .

fn find_first
  output option u64 . .
  input xs slice u32 . .
  input target u32 .
do
  var i u64 be 0 .
  while lt i (len xs) . do
    if eq (index xs i) target . do return some i . end
    set i add i 1 . .
  end
  return none .
end
```

### 08 — 중첩 탈출 (for 원소·if-stmt·return — 당시: 라벨 break. 라벨은 없다)
```
module grid .

fn has_pair
  output bool .
  input rows slice u32 . .
  input cols slice u32 . .
  input target u32 .
do
  for r rows do
    for c cols do
      if eq (add r c) target . do
        return true .
      end
    end
  end
  return false .
end
```

### 09 — requires 전제 + div (requires·div 계약 b≠0)
```
module ratio .

fn safe_div
  output i32 .
  input a i32 .
  input b i32 .
  requires
    ne b 0 . .
do
  return div a b . .
end
```

### 10 — result + try + errors (enum·result·errors <변형> <조건>·try·ok)
```
module digits .

enum parse_error .
  bad_char .
end

fn to_digit
  output result u8 parse_error . .
  input c u8 .
  errors bad_char or (lt c 48) (gt c 57) .
do
  guard and (ge c 48) (le c 57) . else return error bad_char . . .
  return ok sub c 48 . . .
end

fn two_digits
  output result u8 parse_error . .
  input a u8 .
  input b u8 .
do
  let hi u8 be try to_digit a . .
  let lo u8 be try to_digit b . .
  return ok add mul hi 10 . lo . . .
end
```

### 11 — option 소비 (guard else return none·some·value_or·op 호출)
```
module optx .

fn head
  output option u32 . .
  input xs slice u32 . .
do
  guard gt len xs . 0 . else return none . .
  return some index xs 0 . . .
end

fn head_or_zero
  output u32 .
  input xs slice u32 . .
do
  return value_or head xs . 0 . .
end
```

### 12 — struct 선언/생성/필드 접근 (struct-decl·struct-lit·field)
```
module geom .

struct point
  x i32 .
  y i32 .
end

fn make_point
  output point .
  input a i32 .
  input b i32 .
do
  return make point do
    x a .
    y b .
  end
end

fn sum_coords
  output i32 .
  input p point .
do
  return add field p x . field p y . . .      rem G1 확정: 필드 접근 = `field <struct> <name>`
end
```

### 13 — payload 없는 enum 분기 (enum-decl·match·case)
```
module light .

enum signal .
  red .
  yellow .
  green .
end .

fn can_go
  output bool .
  input s signal .
do
  match s do
    case green . do return true . end
    case _ . do return false . end
  end .
end
```

### 14 — 투명 타입 별칭 (type X Y)
```
module ids .

type user_id u32 .

fn same_user
  output bool .
  input a user_id .
  input b user_id .
do
  return eq a b . .
end
```

### 15 — proc + mut slice 변경 (proc·mut slice·set index·while)
```
module mutslice .

proc double_all
  output void .
  input xs mut slice u32 . .
  effects none .
do
  var i u64 be 0 .
  while lt i (len xs) . do
    set (index xs i) (mul (index xs i) 2) .
    set i (add i 1) .
  end
end
```

### 16 — stack push/pop (stack_new region·push·while pop into·effects alloc)
```
module stackx .

type scratch u64 . .

proc stack_sum
  output u32 .
  input scratch region scratch . .
  input xs slice u32 . .
  effects alloc .
do
  let s stack u32 . be stack_new scratch capacity len xs . . .
  for x xs do
    push s x .
  end
  var total u32 be 0 .
  while pop s into v . do
    set total add total v . .
  end
  return total .
end
```

### 17 — 어휘 region 블록 (region arena do…end·alloc_bytes·view_array)
```
module lexregion .

proc scratch_sum
  output u32 .
  input xs slice u32 . .
  effects alloc .
do
  var total u32 be 0 .
  region tmp arena do
    let g option mut slice u8 . . be alloc_bytes tmp capacity (mul (len xs) 4) .
    guard is_some g . else return 0 .
    let buf mut slice u32 . be view_array u32 (some_value g) .
    var i u64 be 0 .
    while lt i (len xs) . do
      set (index buf i) (mul (index xs i) 2) .
      set i (add i 1) .
    end
    for x buf do
      set total add total x . .
    end
  end
  return total .
end
```

### 18 — region 파라미터 (input region·alloc_bytes·view_array — 당시: child_of)
```
module childregion .

type scratch u64 . .

proc copy_doubled
  output u64 .
  input out region scratch . .
  input xs slice u32 . .
  input result mut slice u32 . .
  effects alloc .
do
  let g option mut slice u8 . . be alloc_bytes out capacity (mul (len xs) 4) .
  guard is_some g . else return 0 .
  let scratch mut slice u32 . be view_array u32 (some_value g) .
  var i u64 be 0 .
  while lt i (len xs) . do
    set (index scratch i) (mul (index xs i) 2) .
    set (index result i) (index scratch i) .
    set i (add i 1) .
  end
  return len xs .
end
```

### 19 — ★Flagship A: slice 기반 parser (requires·errors·struct·result·계약)
```
module packet .

enum parse_error .
  too_short .
  bad_version .
  too_long .
end

struct header
  version u8 .
  length u16 .
end

fn read_u16_be
  output u16 .
  input data slice u8 . .
  input at u64 .
  requires lt (add at 1) (len data) .
do
  let hi u16 be widen u16 (index data at) .
  let lo u16 be widen u16 (index data (add at 1)) .
  return expr hi * 256 + lo . .
end

fn parse_header
  output result header parse_error . .
  input data slice u8 . .
  access data sequential .
  errors too_short lt (len data) 4 .
  errors bad_version ne (index data 0) 1 .
  errors too_long gt (widen u64 (read_u16_be data 2)) (len data) .
do
  guard ge (len data) 4 . else return error too_short . . .
  guard eq (index data 0) 1 . else return error bad_version . . .
  let length u16 be read_u16_be data 2 .
  guard le (widen u64 length) (len data) . else return error too_long . . .
  return ok make header do
    version index data 0 . .
    length length .
  end
end
```

### 20 — ★Flagship B: arena 기반 graph traversal (region·stack·CSR 인접·EXCL)
```
module graph .

type scratch u64 . .

rem CSR 인접: 노드 i 의 이웃 = adj[ off[i] .. off[i+1] ],  node_count = len(off)-1
proc reachable_count
  output u64 .
  input scratch region scratch . .
  input off slice u64 . .
  input adj slice u64 . .
  input start u64 .
  effects alloc .
  access adj sequential .
  requires gt (len off) 0 .
do
  let node_count u64 be sub (len off) 1 .
  guard lt start node_count . else return 0 .
  let vg option mut slice u8 . . be alloc_bytes scratch capacity node_count .
  guard is_some vg . else return 0 .
  let visited mut slice u8 . be some_value vg .
  let work stack u64 . be stack_new scratch capacity node_count . .
  push work start .
  set (index visited start) 1 .
  var seen u64 be 1 .
  while pop work into cur . do
    guard lt (add cur 1) (len off) . else continue .
    var k u64 be index off cur .
    let hi u64 be index off (add cur 1) .
    while lt k hi . do
      guard lt k (len adj) . else break .
      let nb u64 be index adj k .
      if and (lt nb node_count) (eq (index visited nb) 0) . do
        set (index visited nb) 1 .
        set seen (add seen 1) .
        push work nb .
      end
      set k (add k 1) .
    end
  end
  return seen .
end
```

---

## 커버리지 행렬 (문법 생성 ↔ 예제)

```text
module/decl            01 모든
fn / proc      01(calc) 15(proc)
io-clause input/output 01·06(다중 input)·15(output void)
effects none/alloc     01(none) 16·17·18·20(alloc)
access 절              06·15·16·17·18·20
requires 절            09·19·20
errors 절(when)        10·19
let / var / set        02(let) 05(var/set) 12·19(let)
set index (lvalue)     15·17·18·20
if-expr 값 / if-stmt   03(식) 08·20(문)
else 분기              03
while                  05·16·20
for-in 원소 / range    06·17(원소) 15·17·18·20(range)
loop + break 값        07
라벨 break             08
guard else (발산)      07(break) 09·11(return)
return ok/error/some/none/expr  01·07·10·11·19·20
give (블록 값)         03·07
try (전파)             10·19
result / option        10·19(result) 07·11(option)
struct decl/lit/field  12·19
enum (payload 없음)    10·13·19
type alias             14
slice / array          06·15·… (slice 다수)
stack / stack_new      16·20
region 어휘블록/param/child_of  17(블록) 16·18·20(param) 18(child_of)
ref / mut_ref / mut    15(mut slice)
prefix op add/sub/mul/div/lt/le/gt/ge/eq/ne/and/or/not  전반
expr 중위 섬           02·19
prelude len/index/subslice/push/pop/count/stack_new/value_or  전반
```

## 이 게이트가 드러낸 미확정 (파서 전 닫을 것)

```text
G1  [확정 2026-06-28] 필드 접근 = `field <struct> <name>`(전위 키워드; name=필드 이름공간, 무충돌·타입무관 파싱).
    `<field> <struct>`·`.` 거부. RFC-0001 §6.8 · RFC-0002 §8-9.
G2  [확정 2026-06-28] 수치 변환 = `cast <scalar> <expr>`(전위 키워드; 확대 무손실·축소/float debug 검사·부호 reinterpret·
    암묵 coercion 없음). RFC-0001 §6.8 · RFC-0002 §8-10.
G3  [확정 2026-06-28] `pop s into v` = 부분형(v:T 바인딩, 빈 stack=panic; 총체형 `pop s : option T`). RFC-0001 into-stmt · RFC-0016.
G4  [확정] range = `range lo hi` **반개구간 [lo,hi)** step 1(연결어 to 삭제, RFC-0049). `for x in coll`=원소 copy(scalar)/
    shared ref(aggregate, 읽기); 제자리 변경=index 형. `for x in mut slice`(mut_ref 원소)=post-MVP. RFC-0016·RFC-0001.
G5  [확정] enum 변형 = **모듈 내 유일 이름**(중복 선언=에러)→bare 무모호·타입으로 확인. 한정 표기=post-MVP. RFC-0002 §8-11.
G6  [확정] **이미 명세됨**: `result t e` e=enum, **variants(errors 절)==variants(e)**, return error ⊆ variants(e). RFC-0006 §6.
G7  [확정] `deref <ref>`(전위, 포인티 읽기) + `set <mut_ref> v`(write-through); ref 재결합 불가. RFC-0001·RFC-0016.
```

> **G1~G7 전부 확정** ⟹ 20개 예제 "컴파일 가능" 게이트 **충족**. 예제는 range `to` 형·field·cast 반영 완료. → 파서 착수 조건 OK.

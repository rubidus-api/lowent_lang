# SPEC-BRIEF — 한 번에 읽는 규범 뼈대 (생성물)

> **이 파일은 손으로 쓰지 않는다.** `python3 scripts/gen-spec-brief.py --write` 가 낸다.
> 손으로 고치면 게이트가 운다 — STATUS.md 와 같은 규율이다.
>
> ★ **규범이 아니다.** 이것은 SPEC 을 *가리키는* 요약이고, 다투면 **SPEC 이 이긴다**.
> 각 절 머리에 출처를 적어 두었으니 정확한 문장은 거기서 읽어라. 요약이 규범 행세를
> 하면 두 벌이 생기고, 두 벌은 갈린다.
>
> ☞ 왜 있나(REQ-0005): 주 사용자가 AI 인데 SPEC+RFC 가 한 번에 안 읽히면 **그 자체가
> 의미 엔트로피**다(설계 원칙 §0 — 소스 밖에서 가져와야 하는 정보의 양).

## 0. 한 문장  ·  *출처: 설계 원칙 §0*

> Lowent는 계약을 가진 op들이 명시적 region·slice·stack 위에서 조합되고, 모든 비용이 가시적이며, effect가 profile을 게이팅하고, 의미가 내용주소화 정규형으로 식별되어 AI가 symbol 단위로 작업하는 — **날렵한 단일-패러다임 시스템 언어**다. 정체성 4축: **날렵함(zero-cost·군살 0)·이식성·정교함(계약·비용 가시)·하드웨어 현실성**(실제 CPU/MCU/임베디드 구조 적합). Rust보다 단순·Zig보다 안전, 고유각=*계약+비용 가시*. 멀티패러다임·상속 OO·C++식 중복은 거부. (방향: RFC-0037)

```text
철학은 크다.            core는 작다.            비용은 가시적이다.
runtime은 opt in이다.    test와 metadata는 바이너리에서 분리한다.
계약은 기본 개념이되 release 비용은 선택 가능하다.
```

## 1. 원칙  ·  *출처: 설계 원칙 §1*

| # | 원칙 | 뜻(첫 문장만) |
|---|---|---|
| P1′ | **날렵함(leanness) / pay-as-you-go** | (RFC-0037 개정: "작은 표면"→"날렵함") **작은 직교 코어 + 풍부한 주변부**(stdlib·comptime). |
| P2 | **cost visible** | "zero cost"는 낙관. |
| P3 | **no hidden X** | 숨은 heap·제어 흐름·동시성·할당 없음. |
| P4 | **계약 중심** | `requires/ensures/errors/effects/access/tests`가 일급. |
| P5 | **국소적 추론 가능성** | 한 op를 시그니처+계약만으로 이해·생성·검증 가능. |
| P6 | **의미는 정규형으로, 표기는 뷰로** | 표면 문법과 별개로 canonical IR을 두고, 정의를 내용(해시)으로 식별. |

☞ 새 기능의 시금석: *"이게 이 원칙들의 귀결인가, 새 축인가?"* 새 축이면 뺀다.

## 2. 어휘 — 하드 키워드 **43**  ·  *출처: 정본 §6.1 · 부록 A*

```text
  actor         be            break         case          continue      contract      do            drop          else          end
  enum          expect        export        expr          extern        false         fn            for           guard         if
  let           make          match         module        newtype       none          proc          return        satisfies     send
  set           spawn         state         struct        test          trait         true          try           type          unsafe
  use           var           while
```

## 3. 빌트인 op **188**  ·  *출처: `impl/src/low_arity.h` · 부록 D*

★ 외워야 하는 것은 키워드 수가 아니라 **키워드 + op** 이다. 그래서 둘 다 센다.

```text
  abs             add             all             alloc_bytes     and             any             arg             atomic_add
  atomic_and      atomic_cas      atomic_fence    atomic_load     atomic_or       atomic_store    atomic_sub      atomic_swap
  atomic_xor      avg             bit_and         bit_cast        bit_not         bit_or          bit_xor         bitset_new
  borrow          byte_swap       capacity        cast            ceil            chk_add         chk_mul         chk_sub
  collect         complement      config          contains        cos             count           count_ones      crc32
  cstr_of         deref           difference      dir_close       dir_make        dir_open        dir_read        div
  div_nz          encode          enumerate       env_get         eq              error           error_value     exp
  expect          field           file_close      file_open       file_read       file_seek       file_type       file_write
  filter          floor           fmod            fold            ge              gt              hash_bytes      index
  intersect       into            is_empty        is_error        is_none         is_ok           is_some         is_subset
  le              leading_zeros   len             link_type       load            load_masked     log             lt
  map             max             min             mod             mul             mut_ref         narrow          narrow_sat
  narrow_try      narrow_wrap     native_lanes    ne              neg             net_accept      net_close       net_connect
  net_listen      net_pair        net_port        net_recv        net_send        nonzero_of      not             ok
  ok_value        or              panic           path_remove     path_rename     pipe            pop             pow
  prefetch        push            r_read          r_write         range           reactor_new     read_in         read_volatile
  reduce_add      reduce_max      reduce_min      reduce_mul      ref             region          remove          ret
  reverse         rng_next        rotate          rotl            rotr            round           same_slice      sat_add
  sat_mul         sat_sub         scan            seg             segs            select          send            sha256
  sha512          shl             shr             sin             size_of         skip            some_value      spawn
  splat           sqrt            stack_new       store           store_masked    str_from_cstr   sub             subslice
  sum_neumaier    sum_seq         swap            take            trailing_zeros  try_view        union           value_or
  view            view_array      view_segments   widen           wrap_add        wrap_mul        wrap_shl        wrap_shr
  wrap_sub        write_out       write_volatile  zip
```

## 4. effect atom **15**  ·  *출처: 정본 §7.1*

전파 = 합집합 · 게이팅 = ⊆. 바닥은 `none`(=∅).

```text
  alloc  heap  io  wait  concurrent  lock  atomic  state  panic  device  unsafe  page_fault  blocking  cancel  detach
```

## 5. 권한(capability)·프로파일  ·  *출처: 정본 §7.2 · §5.6*

권한은 **건네받는 값**이지 어디선가 꺼내 오는 것이 아니다(RFC-0030 D2).

```text
  cap   io  file_system  net  clock  random  allocator  device_memory
  프로파일  freestanding  embedded  native  server
```

## 6. 오류 코드 — 계열 **96**  ·  *출처: `impl/src/*.c`*

**전부 적지 않는다**(수백 개다). 어느 계열이 얼마나 굵은지만 — 굵은 계열이 곧 이 언어가
가장 자주 거절하는 자리다.

```text
  E-VM(50)  E-TYPE(36)  E-NAME(12)  E-ASM(11)  E-ALLOC(10)
  E-MMIO(9)  E-ENUM(9)  E-EFFECT(8)  E-PKG(8)  E-PAR(7)
  E-FFI(7)  E-IR(6)  E-CONTRACT(5)  E-TRAIT(5)
  … 그 밖 82 계열
```

## 7. 지금의 수  ·  *가리키기만 한다*

**여기 베끼지 않는다.** 검사 수·픽스처 수·구현 줄 수 같은 오늘의 계수는 요약에 싣지 않는다 —
요약이 그 수를 품으면 **순환**이 된다: 검사를 하나 늘리면 이 파일이 낡는다.
요약의 일은 *규범의 뼈대*이지 *오늘의 계수*가 아니다.

## 8. 더 읽을 곳

| 무엇 | 어디 |
|---|---|
| 규범 전체 | `docs/spec/canon/*.md` (조항 정본) · 엮은 것은 `docs/spec/html/` |
| 쓰는 법 | 매뉴얼 `docs/manual/`(책 형식 — 표준 라이브러리는 부록 E) · 예제 `docs/example/` |
| 처리기 | `impl/README.md` |

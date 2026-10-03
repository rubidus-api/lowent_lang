// ★★★ **"이 머리는 피연산자를 몇 개 먹는가" 에 대한 유일한 답.**
//
// 이 언어의 파서는 **일부러 arity 를 모른다**(RFC-0046 — 구조=문자열). 그래서 진짜 나무는
// 파스 **다음에** 선다. 그런데 그 나무를 세우는 코드가 `low_ir.c` 안에만 있었고, 나머지 다섯
// 소비자(check·typecheck·contract·region·doc)는 평평한 원자열을 **각자 훑으며 구조를 짐작**했다
// — 합쳐 raw `kids[]` 접근 **883회**. 그리고 다섯이 조금씩 **다른 답**을 냈다(DECISION-0015 §2).
//
// 표를 **두 벌** 만들면 그것이 곧 여덟 번째 갈림이다(교훈 7 — 하나의 뜻에 두 표현이 있으면
// 반드시 갈린다). 그래서 표는 **하나**다. `low_ir.c` 는 IRW 낱말까지 읽고, 정규화 층은
// arity 만 읽는다 — **같은 줄에서**.
//
//   X(표면 이름, IR 낱말, arity)
#ifndef LOW_ARITY_H
#define LOW_ARITY_H

#define LOW_BUILTINS_CORE(X)                                                        \
    X(add, IRW_ADD, 2)   X(sub, IRW_SUB, 2)   X(mul, IRW_MUL, 2)               \
    X(div, IRW_DIV, 2)   X(mod, IRW_MOD, 2)   X(neg, IRW_NEG, 1)               \
    X(and, IRW_AND, 2)   X(or, IRW_OR, 2)     X(not, IRW_NOT, 1)               \
    /* ★★★ **비트 연산** (RFC-0064) — 이름이 논리 `and`/`or`/`not` 과 **다르다**:      \
       같은 이름에 두 뜻을 주면 그것이 **동음이의**이고, §2.5 가 금지하는 동의어와      \
       **같은 병**이다. 그리고 새 특수문자는 **0개**다(P6 — 중위를 늘리면 우선순위 표가 \
       늘어나고, 그것이 곧 엔트로피다). */                                              \
    X(bit_and, IRW_BAND, 2)  X(bit_or, IRW_BOR, 2)   X(bit_xor, IRW_BXOR, 2)      \
    X(bit_not, IRW_BNOT, 1)                                                        \
    X(shl, IRW_SHL, 2)       X(shr, IRW_SHR, 2)                                    \
    X(wrap_shl, IRW_WSHL, 2) X(wrap_shr, IRW_WSHR, 2)                              \
    X(rotl, IRW_ROTL, 2)     X(rotr, IRW_ROTR, 2)                                  \
    /* ★★★ **캐리 없는 곱셈** (RFC-0119, 2026-09-23) — GHASH 의 핵이다. 결과가 128 비트인데    \
       이 언어에 128 비트 타입이 없으므로 **아랫말·윗말 두 낱말**로 낸다. 기계에 명령이 있으면  \
       C 뒤끝이 그것을 내리고, 없으면 같은 답을 내는 소프트 판으로 내린다 — 뜻은 하나다. */     \
    X(count_ones, IRW_POPCNT, 1)  X(leading_zeros, IRW_CLZ, 1)                     \
    X(trailing_zeros, IRW_CTZ, 1) X(byte_swap, IRW_BSWAP, 1)                       \
    /* ★★★ **level-3 atomic** (RFC-0018) — 전위 낱말. 새 키워드 0개.                \
       ordering 은 **선택 절**(`order acquire`)로 준다 — 절 낱말이라 어휘를 안 늘린다. */\
    /* ★ place = **슬라이스 + 인덱스** (C11 의 `&x` 자리). 그래야 **parallel 워커가 공유**하는  \
       버퍼의 한 원소를 원자적으로 만질 수 있다 — 그것이 level-3 의 유일한 실사용처다. */     \
    X(atomic_load, IRW_ALOAD, 2)   X(atomic_store, IRW_ASTORE, 3)                  \
    X(atomic_add, IRW_AADD, 3)     X(atomic_sub, IRW_ASUB, 3)                      \
    X(atomic_and, IRW_AAND, 3)     X(atomic_or, IRW_AOR, 3)                        \
    X(atomic_xor, IRW_AXOR, 3)     X(atomic_swap, IRW_ASWAP, 3)                    \
    X(atomic_cas, IRW_ACAS, 4)     X(atomic_fence, IRW_AFENCE, 0)                  \
    X(eq, IRW_EQ, 2)     X(ne, IRW_NE, 2)     X(lt, IRW_LT, 2)                 \
    X(le, IRW_LE, 2)     X(gt, IRW_GT, 2)     X(ge, IRW_GE, 2)                 \
    X(len, IRW_LEN, 1)   X(idx, IRW_INDEX, 2)    X(subslice, IRW_SUBSLICE, 3)  \
    /* ★★★ **해시 둘** (RFC-0090 N3, 2026-08-11) — 권한이 **없다**: 바이트를 읽어 수를    \
       내는 순수 계산이고 세상에 안 닿는다. 리프인 이유는 표현이 아니라 **비용**이다      \
       (Lowent 로 쓸 수 있지만 바이트마다 op 호출이 붙는다 — 리프 규칙 RFC-0075).         \
       ★ 알고리즘을 **고정**한다: hash_bytes = FNV-1a 64 · crc32 = 반사 CRC-32           \
       (다항식 0xEDB88320). 고정해야 두 뒤끝이 같고, **알려진 답**이 오라클이 된다.       \
       그러지 않으면 "해시가 나온다" 는 것 말고는 아무것도 검사할 수 없다.  */            \
    /* ★★★★ **같은 바이트인가** (RFC-0112 D10 · WO-0215) — 두 슬라이스가 **같은 자리에서 시작하고 길이가 \
       같은가**. 권한 없음·순수. 얼로케이터의 `grow`/`release` 가 «내가 마지막에 준 그 조각인가» 를 크기가  \
       아니라 **정체**로 묻는 유일한 길이다(크기만 보면 같은 길이의 남의 버퍼를 늘렸다 — F13). 주소는      \
       밖으로 나오지 않는다: 답은 참거짓 하나다. */                                                   \
    X(same_slice, IRW_SAMESL, 2)                                                 \
    /* ★ 재현 가능한 난수 — **권한 없음**. 상태를 받아 다음 상태를 돌려준다(순수).       \
       OS 엔트로피(`random_bytes`)만 `cap random` 을 요구한다 — 갈라 둔 이유는 하나다:   \
       시험은 재현되어야 하고, 열쇠는 예측되면 안 된다. 한 낱말로는 둘 다 못 한다. */   \
    /* ★★★ **SHA-256** (RFC-0090 N3c) — 권한 없음(순수). 출력은 **호출자 버퍼**에 쓴다:      \
       라이브러리가 몰래 할당하지 않는다는 규율(RFC-0043 D1)이 리프에도 적용된다.            \
       32 바이트를 채우고 **채운 수**를 답한다 — 버퍼가 짧으면 0 이고, 조용히 덜 쓰지 않는다.\
       ★ 오라클은 **표준 테스트 벡터**다. 이 축에서 가장 강한 종류이고, 그래서 N3 을         \
         N2 보다 먼저 했다. */                                                               \
    /* ★★★★ AES-CTR 과 GHASH — 계산만 내린 잎(X-0043 ⓑ). **차례**는 `lib/gcm.low` 에 남는다. */ \
    /* ★★★ **AES 한 라운드** (RFC-0119 §10, 2026-09-23) — FIPS-197 의 라운드 하나를 낱말로.   \
       `aes_round <상태 16 B mut> <라운드키 16 B>` = SubBytes·ShiftRows·MixColumns·AddRoundKey, \
       `aes_round_last` 는 MixColumns 없이. 기계에 명령이 있으면 C 뒤끝이 `aesenc`/`aesenclast`  \
       로 내리고, 없으면 같은 답을 내는 표 셈으로 내린다 — **뜻은 하나**다. */                    \
    /* ★★★ **ChaCha20** (RFC-0122, 2026-09-24) — `chacha20 <키 32 B> <카운터 16 B mut>       \
       <원본> <결과 mut>`. 모양은 `aes_ctr` 와 **같다**: 카운터 블록을 제자리에서 올리므로      \
       나눠 불러도 이어진다. 뜻의 정의는 `lib/chacha.low` 에 남고 골든이 둘을 맞댄다. */        \
    /* ★★★ **Poly1305** (RFC-0122) — `poly1305 <상태 mut slice u64> <바이트>`. 열여섯의      \
       배수가 아니면 마지막 조각을 0 으로 채운다(ChaCha20-Poly1305 가 그렇게 먹인다).        \
       뜻의 정의는 `lib/poly.low` 의 `block` 에 남고, 골든이 둘을 맞댄다. */                 \
    /* ★★★ **GCM 한 덩이** (RFC-0124, 2026-09-24) — `aes_gcm <키 16> <카운터 16 mut>        \
       <H 16> <누산기 16 mut> <원본> <결과 mut>`. 흐름(`aes_ctr`)과 누산(`ghash`)을 한 바퀴에.  \
       뜻은 «그 둘을 차례로 부른 것» 이고(`low_aes.h`), 기계 판만 둘을 엮어 돈다.              \
       ☞ 봉인만 쓴다 — 복호는 태그를 먼저 증언해야 하므로 여전히 두 바퀴다. */                 \
    /* ★★★ **맞바꾸기** (2026-07-26) — `index` 는 구조체 원소를 **뷰**로 준다. 그래서 읽은  \
       두 값은 같은 바이트를 가리키고, `set` 두 번으로 맞바꾸면 자기 자신을 덮어쓴다:       \
       **제자리 알고리즘이 언어로 표현될 수 없었다.** 임시 복사가 답이 아닌 이유는 제네릭   \
       코드가 남의 구조체를 복사할 방법이 없기 때문이다(필드 이름을 모른다). 그래서 이것은  \
       리프다: 언어로 못 쓰는 것이고, 할당 0 이며, 원소 타입을 몰라도 된다. */              \
    X(swap, IRW_SWAP, 3)                                                       \
    X(push, IRW_SPUSH, 2)  X(bitset_contains, IRW_CONTAINS, 2)  X(count, IRW_COUNT, 1)\
    X(bitset_new, IRW_BNEW, 1)                                                 \
    /* ★ 집합 연산 (RFC-0010 §6.7.1) — 닫힌 어휘. 전부 워드별 bitwise, 할당 0 */    \
    X(bitset_remove, IRW_BREMOVE, 2)   X(bitset_union, IRW_BUNION, 2)          \
    X(bitset_intersect, IRW_BINTER, 2) X(bitset_difference, IRW_BDIFF, 2)     \
    X(bitset_complement, IRW_BCOMPL, 1) X(bitset_is_empty, IRW_BEMPTY, 1)     \
    X(bitset_is_subset, IRW_BSUBSET, 2)                                        \
    X(deref, IRW_DEREF, 1)                                                     \
    X(is_some, IRW_ISSOME, 1)   X(some_value, IRW_SOMEVAL, 1)                  \
    X(is_ok, IRW_ISOK, 1)       X(is_error, IRW_ISERR, 1)                      \
    /* ★ RFC-0016 — 부분 op 을 **총체형**으로 만드는 소비자 (panic 대신 기본값/none) */\
    X(value_or, IRW_VALOR, 2)                                                  \
    X(ok_value, IRW_OKVAL, 1)   X(error_value, IRW_ERRVAL, 1)                  \
    X(lane_select, IRW_SELECT, 3)                                              \
    X(reduce_add, IRW_RADD, 1)  X(reduce_mul, IRW_RMUL, 1)                     \
    X(reduce_min, IRW_RMIN, 1)  X(reduce_max, IRW_RMAX, 1)                     \
    X(lane_any, IRW_MANY, 1)    X(lane_all, IRW_MALL, 1)                     \
    /* ★★★ **cstr → str** (RFC-0068 S4) — 널종단 C 문자열을 스캔해 `str`(slice u8)로. 길이를    \
       몰라 O(n)(strlen) — 이름이 비용을 말한다. **FFI 경계 전용**(VM 은 못 함) · `effects unsafe`  \
       (생 포인터를 읽는다 — cstr 를 얻으려면 이미 `cap c` 를 거쳤다). 리프 규칙: ① 세상에 닿음   \
       (FFI 경계) ② 언어에 없는 표현(포인터 스캔). */                                             \
    X(str_from_cstr, IRW_CSTR2STR, 1)                                          \
    /* ★★★ **str_buf → cstr** (RFC-0068 S4 · C4) — 널종단 바이트 버퍼의 base 포인터를 cstr 로       \
       (O(1)·뷰). effect **unsafe**: 널종단을 호출자가 보증(D4 — str 뷰는 널을 약속 못 하니 안전한   \
       str→cstr 은 없다 · str_buf 만이 봉인). `lib/strbuf.low` 의 sb_as_cstr 가 봉인 뒤 이걸 부른다. */\
    X(cstr_of, IRW_STR2CSTR, 1)

// ★★★ 불규칙은 **불규칙이 아니었다 — 슬롯의 종류가 둘이었을 뿐이다.**
//
//   `view pt data` 의 `pt` 는 **값이 아니라 타입 이름**이다. `field s x` 의 `x` 는 **필드
//   이름**이고, `error too_short` 의 것은 **변형 이름**이다. 그래서 "arity" 하나로는 못 적는다.
//
//   ⇒ arity 대신 **모양(shape)** 을 적는다:
//        V = 값 슬롯 (재귀적으로 읽는다)
//        W = 낱말 슬롯 (맨 원자 하나를 **그대로** 먹는다 — 절대 적용이 아니다)
//
//   `add` = "VV" · `view` = "WV" · `field` = "VW" · `error` = "W".
//   **이 표가 서자 "포기 213" 이 사라진다** — 모양을 알면 괄호를 칠 수 있다.
// ★★★★★ **`field` 는 다단이다** (2026-08-25 · 소유자 결정) — `field o i z .`
//   모양이 "VW"(값 + 낱말 하나)에서 **"VR"**(값 + 나머지 전부)로 넓어졌다.
//   `R` 은 이미 `expr`·`send` 가 쓰던 것이라 **새 개념 0** 이다.
//   ☞ 붙은 점 `o.i.z` 를 없애는 대신 그 일을 **전위 하나로 모은다** — 같은 뜻을 적는
//     길이 둘이면 읽는 사람이 둘 다 알아야 한다(§2.5 정신).
// ★★★★★ **계산 잎은 `call_builtin` 뒤에서만 선다** (RFC-0125, 2026-09-24).
//   전역 어휘가 199 까지 왔고, 하루에 셋이 늘었다(`chacha20`·`poly1305`·`aes_gcm`). 그런데 늘어난
//   셋은 전부 «특수한 자리에서만 쓰는 것» 이다 — `aes_gcm` 은 코퍼스에 두 자리뿐이다.
//   ⇒ 아래 열다섯은 **이름을 자리로 가둔다**: `call_builtin sha256 msg out` 으로만 부른다.
//     맨몸으로 부르면 `E-BUILTIN-BARE`, `call_builtin` 뒤에 모르는 이름이면 `E-BUILTIN-NAME`.
//   ☞ 새 규율이 아니다. `pipe … do take 3 … end` 의 닫힌 어휘와 `cast u8 x` 의 타입 슬롯이
//     이미 같은 일을 한다 — **자리가 정해져 있어 사용자 이름과 안 부딪친다.**
//   ★ **표는 여전히 하나다.** `LOW_BUILTINS` 는 아래 둘의 합집합이고, arity·IR 낱말·타입·방출을
//     읽는 소비자는 한 줄도 안 바뀐다. 달라지는 것은 **이름 푸는 자리 하나**뿐이다.
//   ★★ 고르는 잣대: **알고리즘을 처리기가 고정한 계산.** 권한 잎(`file_*`·`net_*`)은 여기 없다 —
//     그것들은 첫 피연산자가 권한이라 이미 특별함이 보인다(그 재검토는 RFC-0127).
#define LOW_CALL_BUILTIN(X)                                                    \
    X(clmul_lo, IRW_CLMULLO, 2)  X(clmul_hi, IRW_CLMULHI, 2)  X(aes_round, IRW_AESROUND, 2) \
    X(aes_round_last, IRW_AESLAST, 2)  X(aes_ctr, IRW_AESCTR, 4)  X(ghash, IRW_GHASH, 3) \
    X(chacha20, IRW_CHACHA20, 4)  X(poly1305, IRW_POLY1305, 2)  X(aes_gcm, IRW_AESGCM, 6) \
    X(sha256, IRW_SHA256, 2)  X(sha384, IRW_SHA384, 2)  X(sha512, IRW_SHA512, 2) \
    X(crc32, IRW_CRC32, 1)  X(hash_bytes, IRW_HASH64, 1)  X(rng_next, IRW_RNGNEXT, 1) \
    X(chacha_poly, IRW_CHAPOLY, 5)

// ★ 합집합 — 표는 하나다(위 주석).
#define LOW_BUILTINS(X)  LOW_BUILTINS_CORE(X) LOW_CALL_BUILTIN(X)

// ★★★ **접두사로 가둔 이름** (RFC-0127 ⓐⓑ, 2026-10-02 · 소유자 «bitset_ · lane_»). 비트셋 여덟과 레인 여섯은
//   사용자가 제 op 에 붙일 만한 짧은 낱말(`union`·`remove`·`select`·`any` …)이라 접두사를 붙였다. 옛 철자는 **동의어로
//   남기지 않는다**(§2.5) — 이 표는 «없는 이름» 진단이 새 철자를 대게 할 뿐이고, 옛 낱말은 사용자 이름으로 비어 있다.
#define LOW_RENAMED(R)                                                         \
    R(remove, bitset_remove)  R(union, bitset_union)  R(intersect, bitset_intersect) \
    R(difference, bitset_difference)  R(complement, bitset_complement)         \
    R(is_empty, bitset_is_empty)  R(is_subset, bitset_is_subset)  R(contains, bitset_contains) \
    R(select, lane_select)  R(any, lane_any)  R(all, lane_all)                 \
    R(reverse, lane_reverse)  R(rotate, lane_rotate)  R(avg, lane_avg)

#define LOW_SHAPES(X)                                                          \
    /* 접근·생성 */                                                            \
    X(field, "VR")      X(ok, "V")          X(error, "W")                      \
    X(ref, "W")         X(mut_ref, "W")                                        \
    /* RFC-0052 S3 — 처분을 이름이 고른다 */                                   \
    X(wrap_add, "VV")   X(wrap_sub, "VV")   X(wrap_mul, "VV")                  \
    X(sat_add, "VV")    X(sat_sub, "VV")    X(sat_mul, "VV")                   \
    X(chk_add, "VV")    X(chk_sub, "VV")    X(chk_mul, "VV")                   \
    X(div_nz, "VV")                                                            \
    /* 폭 변환 — 첫 슬롯은 **타입 낱말** */                                    \
    X(widen, "WV")      X(narrow, "WV")     X(narrow_wrap, "WV")               \
    X(narrow_sat, "WV") X(narrow_try, "WV") X(cast, "WV")                      \
    /* 수치 프렐류드 */                                                        \
    X(sqrt, "V")  X(abs, "V")  X(floor, "V")  X(ceil, "V")                     \
    /* ★ 초월 함수 (RFC-0090 N2) — libm. 부동 전용 · 권한 없음 · 호스트 전용 */    \
    X(sin, "V")   X(cos, "V")  X(exp, "V")    X(log, "V")   X(round, "V")      \
    X(nonzero_of, "V")  X(sum_neumaier, "V")  X(sum_seq, "V")                          \
    X(fmod, "VV") X(min, "VV") X(max, "VV")   X(pow, "VV")                     \
    /* SIMD */                                                                 \
    X(splat, "V")       X(load, "VV")       X(store, "VVV")                    \
    X(lane_reverse, "V") X(lane_rotate, "VW")                                  \
    X(load_masked, "VVVV")   X(store_masked, "VVVV")                           \
    X(native_lanes, "W")     /* comptime: target 네이티브 벡터 폭 질의 */      \
    /* ★ 원소 크기 질의 (RFC-0084) — comptime. 제네릭 컨테이너가 **바이트 수를 셀 때** 쓴다:
       `size_of t` 를 못 물으면 제네릭 코드는 최대 폭(8)으로 잡아 u8 벡터가 8배를 쓴다.
       native_lanes 와 **같은 기계**다(타입 낱말 하나 → 정수 상수). */\
    X(size_of, "W")                                                            \
    X(lane_avg, "VV")        /* D5 target intrinsic: 라운딩 평균 (pavgb/vrhadd) */   \
    /* 뷰·퍼닝 — 첫 슬롯은 **타입 낱말** */                                    \
    X(view, "WV")  X(try_view, "WV")  X(encode, "WV")                          \
    X(view_array, "WV")  X(bit_cast, "WV")                                     \
    /* ★ 조각 뷰 (RFC-0104 §8-8) — 백업 슬라이스 + (at,n) 서술자 슬라이스.        */\
    X(view_segments, "VV")   X(segs, "V")   X(seg, "VV")                       \
    /* ★ RFC-0113 R5 — 비트셋에 넣기(옛 `add <비트셋> x`). 하강은 `add` 의 집합 갈래 그대로 쓴다(IR 낱말을 새로 안 만든다). */\
    X(bitset_insert, "VV")                                                     \
    /* 트랩 */                                                                 \
    X(panic, "V")                                                              \
    /* ★ 캐시 힌트 (RFC-0104 §8-14) — 유일하게 승격된 특례. 결과를 안 바꾼다.   */\
    X(prefetch, "VV")                                                          \
    /* ★ MMIO — 디바이스 레지스터 (RFC-0042 D1). W 슬롯은 **레지스터 이름**이다. */\
    X(read_volatile, "VW")   X(write_volatile, "VWV")                          \
    /* ★ 구성 (RFC-0036 D5). W 슬롯은 **옵션 이름**이다 — 값이 아니라 손잡이의 이름. */\
    X(config, "W")                                                             \
    /* ★ 고차 프렐류드 (RFC-0016). W 슬롯은 **op 이름**이다. fold=리덕션, map/filter=호출자 싱크에 쓴다. */\
    X(fold, "VWV")   X(map, "VWV")   X(filter, "VWV")

// ★ 그래도 **못 적는 것**이 남는다 — 선택적 표식을 가진 머리다.
//   `stack_new R capacity n` 의 `capacity` 는 **있어도 되고 없어도 된다.** 모양이 하나가
//   아니면 괄호를 칠 수 없다. 만나면 그 구간을 **평평하게 남긴다.**
//   ★ **조용히 틀린 나무를 세우느니 나무를 안 세우는 게 낫다.** 그리고 이 목록이
//     줄어드는 것이 곧 진척이다 — 지금은 **하나**다.
#define LOW_OPAQUE(X)  X(stack_new)

// ★★★ **어휘가 세 곳에 흩어져 있었다 — 그리고 그 흩어짐이 check-builtins 가 필요했던 이유의
//   절반이었다.** 게이트를 세울 때 스스로 적어 둔 후속을 여기서 닫는다(2026-07-19).
//
//   전에는:
//     ① 이 표          — 모양이 하나인 op (arity 를 적을 수 있는 것)
//     ② low_ir.c 의 NB[] — IR 이 아는 예약 이름. ★ **39 개 중 34 개가 이 표와 중복**이었다.
//                          arity 헤더 스스로가 경고한 그것: *하나의 뜻에 두 표현이 있으면
//                          반드시 갈린다.* 87% 가 두 번 적혀 있었다.
//     ③ **게이트 안** — check-builtins.py 가 파이프라인 스테이지와 IR 특수형을 **하드코딩**했다.
//                          ⇒ 게이트가 아는 어휘와 도구가 아는 어휘가 **따로 놀 수 있었다.**
//                            문지기가 자기 목록을 들고 있으면 그것은 문지기가 아니다.
//
//   이제 **표는 하나**다. 아래 두 목록이 ②·③ 을 흡수한다 — arity 를 적을 수 없어서
//   여기 없었을 뿐, **어휘인 것은 마찬가지**다.

// ★★★ **두 질문을 섞지 않는다** — 이 구분이 이 파일의 요점이다.
//     ① *이 이름이 우리 어휘인가?*        → check-builtins 가 세고 명세와 대조한다.
//     ② *해석기가 이 이름을 가로채는가?*  → E-NAME-BUILTIN 이 지역/파라미터를 막는다.
//   ①은 ②보다 **넓다**. 처음 통합할 때 둘을 한 목록으로 묶었더니 `fn scan`(json.low)이
//   깨졌다 — `scan` 은 **`pipe` 블록 안에서만** 스테이지라 바깥의 사용자 op 과 충돌하지 않는데,
//   E-NAME-BUILTIN 이 *"해석기가 항상 빌트인을 고른다"* 고 **거짓말**을 하게 됐다.
//   ★ 골든이 그것을 잡았다: 조직 정리가 **행동을 바꾸면** 그것은 조직 정리가 아니다.

// ★ **예약 이름** — 전위 자리에서 해석기가 가로채는 특수형. 지역/파라미터로 쓰면 문장이
//   다르게 묶인다 ⇒ E-NAME-BUILTIN 이 본다. (`low_ir_is_builtin_name` 이 읽는 목록.)
#define LOW_SPECIAL(X)                                                         \
    X(spawn)  X(send)  X(expect)  X(arg)  X(env_get)  X(write_out)  X(alloc_bytes)         \
    X(read_in)  X(reactor_new)  X(r_read)  X(r_write)                        \
    LOW_CALL_HOST(X)

// ★★★★★ **권한 잎도 `call_builtin` 뒤에서만 선다** (RFC-0127 ⓒ, 2026-10-02 · 소유자 «ⓐⓑⓒ 까지 진행»).
//   RFC-0125 는 «권한 잎은 매일 쓴다» 며 이들을 전역에 두었는데, 재 보니 `lib/file.low`·`lib/net.low` 의 감싸개와
//   시험 한 파일만 부른다(28 자리). 그래서 계산 잎과 같은 자리에 가둔다: `call_builtin net_send k fd b`.
//   ★ 계산 잎과 달리 이들은 IR 표(`LOW_BUILTINS`)가 아니라 특수형이다 — 검사기·타입·하강이 머리 이름으로 읽는다.
//     그래서 하강에서 이름을 풀지 않고, **파스 직후 한 번** `call_builtin` 을 벗긴다(`low_call_host_strip`, low_using.c).
//     뒤의 소비자는 옛 나무를 그대로 받으므로 한 줄도 안 바뀐다(방출 C 바이트 동일로 확인). 맨몸은 `E-BUILTIN-BARE`.
//   ★ 이름은 여전히 예약이다(E-NAME-BUILTIN) — RFC-0125 의 계산 잎과 같다.
#define LOW_CALL_HOST(X)                                                       \
    X(file_open)  X(file_read)  X(file_write)  X(file_close)  X(file_seek)         \
    X(dir_open)  X(dir_read)  X(dir_close)  X(file_type)  X(link_type)          \
    X(dir_make)  X(path_remove)  X(path_rename)         \
    X(net_pair)  X(net_send)  X(net_recv)  X(net_close)         \
    X(net_listen)  X(net_port)  X(net_connect)  X(net_accept)  X(net_resolve)  \
    X(proc_spawn)  X(proc_read)  X(proc_poll)  X(proc_wait)  X(proc_kill)

// ★ **어휘이지만 예약어는 아닌 것** — 자리가 정해져 있어 사용자 이름과 안 부딪친다.
//   · 절 표식: `into`(pop 의 두 번째 모양) · `capacity`(stack_new) · `ret`(ensures 반환 바인딩)
//   · 문맥 의존 전위: `pop` · `range` · `is_none`
//   · 파이프라인 스테이지·터미널: `pipe … do … end` 안의 **닫힌 어휘**(RFC-0010 §6.1·§8-2).
//     융합=보장(D-A)이 성립하는 이유가 이 집합이 닫혀 있다는 것이다 — 밖의 낱말은 E-PIPE-STAGE.
//   ☞ `pop`·`range` 를 **예약어로 삼을지는 별개 질문**이고 여기서 답하지 않는다.
//     전에도 예약어가 아니었다 — 그것이 틈이라면 **그 자체로 다뤄야 할 틈**이지,
//     "정리" 커밋이 조용히 바꿀 것이 아니다(후속으로 기록).
#define LOW_VOCAB_ONLY(X)                                                      \
    X(call_builtin)                                                            \
    X(pop)  X(range)  X(into)  X(is_none)  X(capacity)  X(ret)                 \
    X(pipe)  X(take)  X(skip)  X(enumerate)  X(zip)  X(scan)  X(collect)         \
    X(any)  X(all)   /* RFC-0127 — pipe 종결자. 레인 판정은 `lane_any`·`lane_all` 로 갈라졌다 */ \
    X(region)  X(borrow)

#endif

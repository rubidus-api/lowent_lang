/* low_cbe_prelude.c — 방출되는 C 런타임의 **문자열 상수**들.
 *
 * ★ `low_cbe.c` 에서 **떼어 왔다** (2026-08-31, WO-0166 · X-0011). 뜻은 한 줄도 안 바꿨다 —
 *   옮기기만 했다. 증명은 `scripts/check-emit-identical.py`(342 단위 바이트 동일).
 *
 * ★★ 왜 여기인가. 이것은 **코드가 아니라 데이터**다 — 방출된 C 파일의 머리에 그대로
 *   찍히는 런타임(태그 값 · 동시성 · 렌더 · 디스패치). 그래서 **들여오는 것이 0** 이다:
 *   아무것도 안 물어본다. 나가는 것은 상수 이름 여덟뿐이고 이미터가 붙여 쓴다 — 한 방향.
 *   ☞ *「코드」와 「데이터」는 다른 책임이다. 한 파일에 있을 이유가 없다.*
 *
 * ★★★ 값의 근거가 바뀌었다는 것도 적어 둔다. 세 조각 뒤 전체 재빌드는 이미 1172ms 이고
 *   병렬이 포화라, 이 조각의 이유는 **속도가 아니라 책임**이다.
 */
#include "low_cbe_prelude.h"
#include "low_sha256.h"   /* ★ 방출 런타임이 VM 과 **같은 매크로**를 찍는다 — 갈릴 자리가 없다 */
#include "low_sha512.h"
#include "low_aes.h"
#include "low_chacha.h"
#include "low_poly.h"
#include "low_blake3.h"
#include "low_version.h" /* ★ 방출 헤더가 자기가 어느 epoch 의 것인지 적는다 */

const char LW_HDR_HOSTED[] =
"#include <pthread.h>\n"
"#include <unistd.h>\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <dirent.h>\n"   /* ★ 디렉터리 순회 (RFC-0069 §6) */
"#include <sys/stat.h>\n" /* ★ 파일 타입 질의 (RFC-0069 §6 — stat) */
"#include <sys/socket.h>\n" /* ★ 소켓 리프 (socketpair/send/recv — cap net) */
"#include <netinet/in.h>\n" /* ★ 네트워크 면 (sockaddr_in · TCP loopback) */
"#include <arpa/inet.h>\n"
"#include <netdb.h>\n" /* ★ getaddrinfo — net_resolve (X-0032) */
"#include <errno.h>\n"    /* ★ readdir 끝 vs 오류 */
"#include <string.h>\n"   /* ★ 결함 주입기가 strncmp 를 쓴다 (RFC-0075 S5) */
// ★ 죽는 방법도 타깃이 정한다. 호스트엔 stderr 와 종료 상태가 있다.
"#define LW_DIE(m) do { fprintf(stderr, \"panic: %s\\n\", (m)); exit(70); } while (0)\n"
"#define LW_FMTF(o, c, d) lw_fmt_f_hosted((o), (c), (d))\n";
const char LW_HDR_FREE[] =
// ★★★ 베어메탈엔 **콘솔도 종료 상태도 없다.** panic 은 *멈추는 것*이다 — 돌아가지 않는다.
//   (진짜 보드에서는 여기가 워치독·리셋·오류 LED 자리다. 조용히 계속 도는 것보다 낫다.)
"#define LW_DIE(m) do { (void)(m); for (;;) { } } while (0)\n"
// ★ 부동소수 서식은 stdio 다 — 프리스탠딩엔 없다. no_float 프로파일이라 부를 일도 없다.
"#define LW_FMTF(o, c, d) do { (void)(d); if ((c)) (o)[0] = 0; } while (0)\n";
// ★★★★★ **호스트인데 POSIX 는 아닌 칸 — Windows** (2026-08-19, RFC-0069 §6.7 ①②).
//   `LW_HDR_HOSTED` 는 OS 가 **POSIX** 라고 가정하고 `dirent.h`·`sys/socket.h`·`termios.h` 를
//   넣는다. Windows 엔 그 셋이 없다. 그런데 `stdio`·`stdlib`·`string`·`time` 은 있다 —
//   즉 이 칸은 프리스탠딩(아무것도 없음)도 POSIX(다 있음)도 아닌 **세 번째 칸**이다.
//   ⇒ 표준 C 가 주는 것만 넣는다. POSIX 전용 잎은 방출 전에 **거절된다**(E-TARGET-LEAF)
//     — 헤더가 없어 mingw 에서 터지게 두는 것보다, 이유를 아는 자리에서 먼저 우는 편이 낫다.
const char LW_HDR_WIN[] =
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <errno.h>\n"
"#include <string.h>\n"
"#define LW_DIE(m) do { fprintf(stderr, \"panic: %s\\n\", (m)); exit(70); } while (0)\n"
"#define LW_FMTF(o, c, d) lw_fmt_f_hosted((o), (c), (d))\n";
const char LW_PRELUDE[] =
"#include <setjmp.h>\n"     /* ★ 공통 — 양쪽 다 쓴다(newlib 도 준다) */
"#include <math.h>\n"
"#include <stdalign.h>\n"
"#include <stdint.h>\n"
"#include <string.h>\n"
// ★★★ **level-3 atomic** (RFC-0018) — RC11 은 증명됐다(LowentRC11.v, Qed). 여기서 진짜가 된다:
//   `atomic_*_explicit` 로 낸다. **약한 ordering·경합은 네이티브(pthread)에서만 뜻이 있고**,
//   VM 은 단일 스레드라 값만 맞췄다 — 이제 C 백엔드가 그 값을 **진짜 원자성**으로 낸다.
//   ★ 기본값은 **seq_cst**(RFC-0018 §6.2 — 가장 강하고 가장 단순한 추론)이되, **약한 ordering 도
//     구현돼 있다**: `order acquire|release|relaxed|acq_rel|seq_cst` 가 진짜 memory_order 로 나간다
//     (실측: 이 파일이 다섯 가지를 전부 방출한다). 남은 것은 그 위의 **lock/rwlock 라이브러리** 뿐.
//   ☞ 여기 *"약한 ordering 은 후속"* 이라고 적혀 있었다 — low_check.c 의 W-NOT-YET 은 이미
//     구현됐다고 말하는데. **두 곳이 서로 다른 말을 하면 둘 중 하나는 거짓말이다**(§0).
"#include <stdatomic.h>\n"
// ★★★ **atomic ordering** (RFC-0018 §6.1) — 소스의 ordering 이 **memory_order 로** 나간다.
//   0=relaxed 1=acquire 2=release 3=acq_rel 4=seq_cst. 생략은 앞단에서 seq_cst 로 정해진다.
"static memory_order lw_mo(int o) {\n"
"    return o==0?memory_order_relaxed:o==1?memory_order_acquire:o==2?memory_order_release\n"
"         :o==3?memory_order_acq_rel:memory_order_seq_cst; }\n"
// place = 슬라이스 원소 주소. 폭에 맞는 _Atomic 로 접근한다(원소 폭은 컴파일 시점에 안다).
"static long long lw_at_rmw(unsigned char *base, long long i, int esz, int op, long long v, int mo) {\n"
"    void *a = base + (size_t)i * (size_t)esz; memory_order m = lw_mo(mo);\n"
"    switch (op) {  /* 0 load 1 store 2 add 3 sub 4 and 5 or 6 xor 7 swap */\n"
"    case 0: switch (esz) {\n"
"        case 1: return (long long)atomic_load_explicit((_Atomic unsigned char *)a, m);\n"
"        case 2: return (long long)atomic_load_explicit((_Atomic unsigned short *)a, m);\n"
"        case 4: return (long long)atomic_load_explicit((_Atomic unsigned int *)a, m);\n"
"        default:return (long long)atomic_load_explicit((_Atomic unsigned long long *)a, m); }\n"
"    case 1: switch (esz) {\n"
"        case 1: atomic_store_explicit((_Atomic unsigned char *)a, (unsigned char)v, m); return 0;\n"
"        case 2: atomic_store_explicit((_Atomic unsigned short *)a, (unsigned short)v, m); return 0;\n"
"        case 4: atomic_store_explicit((_Atomic unsigned int *)a, (unsigned int)v, m); return 0;\n"
"        default:atomic_store_explicit((_Atomic unsigned long long *)a, (unsigned long long)v, m); return 0; }\n"
"    default: { unsigned long long old;\n"
"        switch (esz) {\n"
"        case 1: { _Atomic unsigned char *q=(_Atomic unsigned char *)a;\n"
"            switch(op){case 2:old=atomic_fetch_add_explicit(q,(unsigned char)v,m);break;case 3:old=atomic_fetch_sub_explicit(q,(unsigned char)v,m);break;case 4:old=atomic_fetch_and_explicit(q,(unsigned char)v,m);break;case 5:old=atomic_fetch_or_explicit(q,(unsigned char)v,m);break;case 6:old=atomic_fetch_xor_explicit(q,(unsigned char)v,m);break;default:old=atomic_exchange_explicit(q,(unsigned char)v,m);} break; }\n"
"        case 2: { _Atomic unsigned short *q=(_Atomic unsigned short *)a;\n"
"            switch(op){case 2:old=atomic_fetch_add_explicit(q,(unsigned short)v,m);break;case 3:old=atomic_fetch_sub_explicit(q,(unsigned short)v,m);break;case 4:old=atomic_fetch_and_explicit(q,(unsigned short)v,m);break;case 5:old=atomic_fetch_or_explicit(q,(unsigned short)v,m);break;case 6:old=atomic_fetch_xor_explicit(q,(unsigned short)v,m);break;default:old=atomic_exchange_explicit(q,(unsigned short)v,m);} break; }\n"
"        case 4: { _Atomic unsigned int *q=(_Atomic unsigned int *)a;\n"
"            switch(op){case 2:old=atomic_fetch_add_explicit(q,(unsigned int)v,m);break;case 3:old=atomic_fetch_sub_explicit(q,(unsigned int)v,m);break;case 4:old=atomic_fetch_and_explicit(q,(unsigned int)v,m);break;case 5:old=atomic_fetch_or_explicit(q,(unsigned int)v,m);break;case 6:old=atomic_fetch_xor_explicit(q,(unsigned int)v,m);break;default:old=atomic_exchange_explicit(q,(unsigned int)v,m);} break; }\n"
"        default:{ _Atomic unsigned long long *q=(_Atomic unsigned long long *)a;\n"
"            switch(op){case 2:old=atomic_fetch_add_explicit(q,(unsigned long long)v,m);break;case 3:old=atomic_fetch_sub_explicit(q,(unsigned long long)v,m);break;case 4:old=atomic_fetch_and_explicit(q,(unsigned long long)v,m);break;case 5:old=atomic_fetch_or_explicit(q,(unsigned long long)v,m);break;case 6:old=atomic_fetch_xor_explicit(q,(unsigned long long)v,m);break;default:old=atomic_exchange_explicit(q,(unsigned long long)v,m);} break; }\n"
"        } return (long long)old; } }\n"
"}\n"
"static long long lw_at_cas(unsigned char *base, long long i, int esz, long long exp, long long des, int mo) {\n"
"    void *a = base + (size_t)i * (size_t)esz; memory_order m = lw_mo(mo);\n"
"    memory_order mf = (m==memory_order_release||m==memory_order_acq_rel)?memory_order_acquire:(m==memory_order_seq_cst?memory_order_seq_cst:m);\n"
"    switch (esz) {\n"
"    case 1: { unsigned char e=(unsigned char)exp; return atomic_compare_exchange_strong_explicit((_Atomic unsigned char *)a,&e,(unsigned char)des,m,mf); }\n"
"    case 2: { unsigned short e=(unsigned short)exp; return atomic_compare_exchange_strong_explicit((_Atomic unsigned short *)a,&e,(unsigned short)des,m,mf); }\n"
"    case 4: { unsigned int e=(unsigned int)exp; return atomic_compare_exchange_strong_explicit((_Atomic unsigned int *)a,&e,(unsigned int)des,m,mf); }\n"
"    default:{ unsigned long long e=(unsigned long long)exp; return atomic_compare_exchange_strong_explicit((_Atomic unsigned long long *)a,&e,(unsigned long long)des,m,mf); } }\n"
"}\n"
"typedef struct lowv lowv;\n"
"struct lowv { int tag; long long i; const unsigned char *p; size_t n; int box; lowv *q; };\n"
"enum { LWV_INT, LWV_SLICE, LWV_REC, LWV_OK, LWV_ERR, LWV_STACK, LWV_BITSET, LWV_VIEW, LWV_SOME, LWV_NONE, LWV_VARRAY, LWV_REF, LWV_VEC, LWV_MASK, LWV_FLT };\n"
"#define LW_REF_MUT (1ll << 62)\n"
// ★ 벡터 풀을 **나눠 쓰는 흐름**인가(워커 스레드·그린스레드) — 그러면 되감지 않는다(RFC-0089 B).
"static _Thread_local int lw_vec_shared;\n"
// ★★★ **증명한 것을 말한다.**
//
//   이 언어는 C 컴파일러가 **결코 스스로 알 수 없는 사실**을 강제한다:
//     · `effects none` = **검사된 순수 계약**(어기면 E-EFFECT-PURITY 로 죽는다)
//     · 소유·선형성   = **비앨리어싱**(C 최적화기의 최대 핸디캡이 바로 이것이다)
//     · 계약·구간분석 = **불가능한 분기**
//   그런데 **하나도 안 말하고 있었다** — 힌트 방출 0회.
//
//   ★ 주의: 어트리뷰트는 **주장**이다. 틀리면 C 컴파일러가 그것을 **믿고** 호출을 지운다
//     → **조용히 틀린 프로그램**. 보통 아무도 이걸 안 하는 이유다.
//     우리는 할 수 있다 — **강제된 계약 위에서만** 방출하고, **차등 스윕이 답을 대조**하니까.
//   ★ 그리고 `((const))` 는 **쓸 수 없다**: 우리 op 은 인자 배열을 **역참조**한다.
//     `const` 는 그것을 금한다 ⇒ 붙이면 **거짓 주장**이다. `pure` 만 쓴다.
"#if defined(__GNUC__) || defined(__clang__)\n"
"#  define LW_PURE     __attribute__((pure))\n"
"#  define LW_UNLIKELY(x) __builtin_expect(!!(x), 0)\n"
"#else\n"
"#  define LW_PURE\n"
"#  define LW_UNLIKELY(x) (x)\n"
"#endif\n"
// ★ `_Noreturn` 은 **표준 C11** 이다 — 방언이 아니다. 그리고 이것 하나로 컴파일러는
//   계약 위반 가지가 **돌아오지 않는다**는 것을 알고, 그 가지를 **차갑게** 배치한다.
// ★ 바이트 슬라이스의 **자연스러운 표현** — 포인터와 길이. 태그도 박스도 없다.
"typedef struct { const unsigned char *p; size_t n; } lw_sl;\n"
/* ★ 레코드 하나가 가질 수 있는 필드 수 — 슬라이스 필드의 평행 칸이 이 폭으로 산다
   (RFC-0106 단계 1). `rsl[파라미터 * LW_RECF + 슬롯]`. */
"#define LW_RECF 16\n"
// ★★★ **option·result 의 자연스러운 표현** — (태그, 값) **쌍**이다. 박스가 아니다.
//   태그 경로는 `some(v)`·`ok(v)` 를 **128칸짜리 박스 풀**에 넣는다. 값 하나를 감싸려고
//   힙 슬롯을 태우는 것이고, 루프에서 오류를 다루면 **풀이 마른다.**
//   여기서는 **레지스터 두 개**다(gcc 가 16바이트 구조체를 rax:rdx 로 돌려준다 — 비용 0).
//     tag: 0 = 그냥 정수 · 1 = some · 2 = none · 3 = ok · 4 = err(변형 인덱스)
//     tag: 0=정수 · 1=some · 2=none · 3=ok · 4=err · 5=f64 · 6=슬라이스 · **7=레코드**
/* ★★★★★ 단계 4 에서 여기에 평행 칸(`lw_sl rsl[LW_RECF]`)을 붙여 봤다가 **뺐다**
   (2026-08-30, WO-0158). 두 가지가 함께 걸렸다:
     ① 스윕이 `slice index out of bounds` 로 답했다 — 감싼 값이 호출을 건널 때
        칸이 어디까지 사는지가 아직 정리되지 않았다.
     ② `lw_r` 는 **값으로 오간다**. 16 칸을 더하면 256 바이트가 붙고, `ws[16]`·`ao[]`
        같은 배열이 그것을 곱한다 — `check-frames` 가 `vm_struct` 의 프레임이
        1152 → 5384 B(+367%) 로 늘었다고 물었다.
   ☞ *값으로 오가는 타입은 배열로도 산다. 한 칸을 더하면 그 곱만큼 늘어난다.* */
/* ★★★★★ **반환하는 레코드의 슬라이스 필드** (RFC-0106 단계 4).
   앞서 여기에 16 칸을 붙였다가 `check-frames` 에 걸렸다 — `lw_r` 는 값으로 오가고
   **배열로도 산다**(`ws[16]`)이라 한 칸이 그 곱만큼 늘어난다.
   ⇒ 이번엔 **4 칸**만 둔다: 한 레코드가 드는 슬라이스 필드는 실측상 둘(`mem`·`gens`)이
   가장 많고, 넷을 넘으면 그 레코드는 **반환하지 않는다**(관문이 거절한다).
   ☞ *상한을 둘 때는 그 상한이 무엇을 거절하는지도 함께 정해야 한다.* */
/* ★ 4 칸으로 줄여도 `grep.low` 가 +58% 였다 — `lw_r` 는 배열로 살기 때문이다.
   ⇒ **포인터 하나**만 둔다. 가리키는 곳은 부르는 쪽의 프레임(`rssl[...]`)이고,
   경계 복원은 **반환 직후** 그 값을 읽어 태그 값으로 되돌린다 —
   그 사이에 프레임이 죽지 않는다(호출이 이미 돌아왔고 아직 안 나갔다). */
"typedef struct { long long v; double f; lw_sl s; _Alignas(8) unsigned char r[LW_RECB];"
"                 const lw_sl *rs; int tag; } lw_r;\n"
"_Noreturn static void lw_panic(const char *m) { LW_DIE(m); }\n"
// ★★★ **restart 슈퍼비전** (RFC-0009 D4) — actor 핸들러의 **fault 경계**. `panic` 키워드(lw_upanic)만
//   여기로 잡힌다(계약 위반·트랩은 lw_panic 그대로 — restart 대상이 아니다, RFC-0019 §6.5).
//   경계가 있으면 longjmp 로 되돌아가 상태를 초기화하고 재시도한다. 없으면 프로세스 중단.
"static jmp_buf lw_fault[64]; static int lw_nfault; static const char *lw_upanic_msg = \"panic\";\n"
"_Noreturn static void lw_upanic(const char *m) {\n"
"    if (lw_nfault > 0) { lw_upanic_msg = m; longjmp(lw_fault[lw_nfault - 1], 1); }\n"
"    LW_DIE(m); }\n"
"static lowv lw_int(long long x) { lowv v = {0}; v.tag = LWV_INT; v.i = x; return v; }\n"
"static double lw_b2f(unsigned long long b, int sz);\n"
"static unsigned long long lw_f2b(double d, int sz);\n"
"static double lw_fval(lowv v) { double d; memcpy(&d, &v.i, 8); return d; }\n"
"static lowv lw_flt(double d) { lowv v = {0}; v.tag = LWV_FLT; memcpy(&v.i, &d, 8); return v; }\n"
"static lowv lw_fltbits(long long b) { lowv v = {0}; v.tag = LWV_FLT; v.i = b; return v; }\n"
"/* SPEC-004 190 T0: bit_cast — reinterpret the bits, do NOT change the width */\n"
// ★ bit_cast 의 **스칼라 핵** (RFC — SPEC-004 §190 T0, 무비용 값 재해석). 폭은 안 바꾼다.
"static long long lw_bitcast_ii(long long meta, long long x) {\n"
"    unsigned sz=(unsigned)(meta&0xff); int sgn=(meta&0x20000)!=0;\n"
"    unsigned long long bits=(unsigned long long)x; if(sz<8) bits&=(1ull<<(sz*8))-1;\n"
"    long long r=(long long)bits;\n"
"    if(sgn&&sz<8){unsigned long long sb=1ull<<(sz*8-1); if(bits&sb) r=(long long)(bits|~((1ull<<(sz*8))-1));}\n"
"    return r; }\n"
"static double lw_bitcast_if(long long meta, long long x) {\n"
"    unsigned sz=(unsigned)(meta&0xff); unsigned long long bits=(unsigned long long)x;\n"
"    if(sz<8) bits&=(1ull<<(sz*8))-1; return lw_b2f(bits,(unsigned char)sz); }\n"
"static long long lw_bitcast_fi(long long meta, double d) {\n"
"    unsigned sz=(unsigned)(meta&0xff); int sgn=(meta&0x20000)!=0;\n"
"    unsigned long long bits=lw_f2b(d,(unsigned char)sz); long long r=(long long)bits;\n"
"    if(sgn&&sz<8){unsigned long long sb=1ull<<(sz*8-1); if(bits&sb) r=(long long)(bits|~((1ull<<(sz*8))-1));}\n"
"    return r; }\n"
"static lowv lw_bitcast(lowv v, long long meta) {\n"
"    unsigned sz = (unsigned)(meta & 0xff);\n"
"    int flt = (meta & 0x10000) != 0, sgn = (meta & 0x20000) != 0;\n"
"    unsigned long long bits;\n"
"    if (v.tag == LWV_FLT) bits = lw_f2b(lw_fval(v), (unsigned char)sz);\n"
"    else { bits = (unsigned long long)v.i; if (sz < 8) bits &= (1ull << (sz * 8)) - 1; }\n"
"    if (flt) return lw_flt(lw_b2f(bits, (unsigned char)sz));\n"
"    long long r = (long long)bits;\n"
"    if (sgn && sz < 8) { unsigned long long sb = 1ull << (sz * 8 - 1);\n"
"        if (bits & sb) r = (long long)(bits | ~((1ull << (sz * 8)) - 1)); }\n"
"    return lw_int(r);\n"
"}\n"
"/* a f64 printed as `3` cannot be told from an int — keep the .0 (the VM does the same) */\n"
"#ifdef LW_HOSTED\n"
"static void lw_fmt_f_hosted(char *out, size_t cap, double d) {\n"
"    int n = snprintf(out, cap, \"%g\", d);\n"
"    if (n < 0 || (size_t)n + 3 >= cap) return;\n"
"    for (const char *p = out; *p; p++)\n"
"        if (*p=='.'||*p=='e'||*p=='E'||*p=='n'||*p=='i') return;\n"
"    snprintf(out + n, cap - (size_t)n, \".0\");\n"
"}\n"
"#endif\n"
"static void lw_fmt_f(char *out, size_t cap, double d) { LW_FMTF(out, cap, d); }\n"
"static long long lw_want_int(lowv v, const char *m) { if (v.tag != LWV_INT) lw_panic(m); return v.i; }\n"
"static lowv lw_add(lowv a, lowv b);\n"
"static lowv lw_vbin(int op, lowv a, lowv b);\n"
"static lowv lw_cmp(int op, lowv a, lowv b);\n"
"static lowv lw_sub(lowv a, lowv b);\n"
"static lowv lw_mul(lowv a, lowv b);\n"
"// ★★★ **핵은 한 곳, 그러나 호출은 남기지 않는다** (2026-07-27 벤치).\n"
"//   산술·비교의 스칼라 핵은 아래 한 곳뿐이다(교훈 7: 두 벌로 적으면 갈린다). 그런데 그것이\n"
"//   **함수 호출로 남으면** meta 를 런타임에 해독하고, 루프마다 호출이 서고, gcc 의 벡터화가\n"
"//   막힌다 — 실측 matmul 이 C 의 30배였다. meta 는 **컴파일 시각 상수**이므로 인라인만 되면\n"
"//   전부 접힌다. ⇒ 핵은 그대로 두고 **인라인을 강제**한다: 한 뜻·한 곳·호출 0.\n"
"#if defined(__GNUC__) || defined(__clang__)\n"
"#define LW_HOT static inline __attribute__((always_inline))\n"
"#else\n"
"#define LW_HOT static inline\n"
"#endif\n"
"// ★★★ **표현은 리틀엔디언이 규범이다 — 그러나 바이트를 하나씩 세지는 않는다** (2026-07-27).\n"
"//   슬라이스 원소 하나를 읽을 때마다 8회 시프트 루프가 섰다. 뜻은 옳지만 값이 비쌌다:\n"
"//   matmul 안쪽 루프는 원소 둘을 읽고 하나를 쓰므로 **매 반복 24 바이트 연산**이었다.\n"
"//   ⇒ 리틀엔디언 호스트에서 memcpy 한 번은 **정확히 같은 값**이고 기계어 로드 하나다.\n"
"//   빅엔디언 호스트는 옛 루프 그대로 — 방출 C 는 **어디서 컴파일해도** 같은 답을 낸다.\n"
"LW_HOT unsigned long long lw_ld_le(const unsigned char *q, int n) {\n"
"#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
"    unsigned long long x = 0; memcpy(&x, q, (size_t)n); return x;\n"
"#else\n"
"    unsigned long long x = 0; for (int b = n; b-- > 0; ) x = (x << 8) | q[b]; return x;\n"
"#endif\n"
"}\n"
"LW_HOT void lw_st_le(unsigned char *q, int n, unsigned long long v) {\n"
"#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
"    memcpy(q, &v, (size_t)n);\n"
"#else\n"
"    for (int b = 0; b < n; b++) q[b] = (unsigned char)(v >> (8 * b));\n"
"#endif\n"
"}\n"
/* ★★★★ **구조체 필드 하나를 읽고 쓴다** (RFC-0039 §9-4 — 표현 통합, 2026-08-03).
 *   레코드도 뷰도 이제 **바이트**다. 그러면 필드 접근은 어느 쪽이든 *"오프셋에서 폭만큼"* 이고,
 *   다른 것은 `big` 표기뿐이다. 그 하나를 **여기 한 곳**에 담는다 — 호출자마다 if 를 적으면
 *   그것이 곧 갈리는 두 표현이다(교훈 7). `be` 는 호출 자리에서 상수라 접힌다. */
"LW_HOT unsigned long long lw_ld_f(const unsigned char *q, int n, int be) {\n"
"    if (!be) return lw_ld_le(q, n);\n"
"    unsigned long long x = 0; for (int b = 0; b < n; b++) x = (x << 8) | q[b]; return x;\n"
"}\n"
"LW_HOT void lw_st_f(unsigned char *q, int n, int be, unsigned long long v) {\n"
"    if (!be) { lw_st_le(q, n, v); return; }\n"
"    for (int b = n; b-- > 0; ) { q[b] = (unsigned char)(v & 0xff); v >>= 8; }\n"
"}\n"
"LW_HOT int lw_ty_signed(long long meta);\n"
// ★★★ **진짜 OS 스레드 — 풀을 잠그지 않고 나눈다** (RFC-0071 A7, 2026-07-20).
//
//   ☞ 값 하나(`some`/`ok`/레코드)는 **전역 풀의 인덱스**를 들고 다닌다. 스레드마다 풀을 따로
//     두면 그 인덱스가 **받는 쪽에서 뜻을 잃는다** — 채널로 값을 못 보낸다.
//   ⇒ **같은 배열을 서로소 구간으로 나눈다.** 인덱스는 어디서나 유효하고, 두 스레드가 같은
//     칸을 절대 안 쓴다 ⇒ **잠금이 필요 없다.** (`parallel` 이 슬라이스를 서로소 청크로
//     나눈 것과 **같은 논증**이다 — 증명된 서로소성이 잠금을 대신한다.)
//   ★ 커서만 스레드-지역이고 **저장소는 공유**다. join 이 메모리 장벽이므로 결과를 읽는 것도 안전하다.
"static lowv lw_boxes[LW_BOXPOOL];\n"
"static _Thread_local int lw_nbox; static _Thread_local int lw_boxlim = LW_BOXPOOL;\n"
// ★★★★ **두 뿌리** (RFC-0112 D3 · WO-0211) — VM 의 `vm_root_take`/`vm_root_mark`/`vm_root_reset` 과 같은 규칙.
//   ★ 고정 창: 호스티드는 정적 저장소, 프리스탠딩은 **링커가 주는 경계** 사이(부팅 때 그 창이 쓰인다).
//   ★ 힙: 이 단위가 자라는 뿌리를 쓸 때만 있다(`LW_HEAP_ROOT`). 이미 준 바이트는 옮기지 않는다 — 청크를 잇는다.
"#if LW_FIXED_LINKER\n"
"extern unsigned char __lw_fixed_start[], __lw_fixed_end[];\n"
"#define LW_FIXED_BASE __lw_fixed_start\n"
"#define LW_FIXED_CAP ((size_t)(__lw_fixed_end - __lw_fixed_start))\n"
"#else\n"
"static unsigned char lw_fixed[LW_FIXED_BYTES];\n"
"#define LW_FIXED_BASE lw_fixed\n"
"#define LW_FIXED_CAP ((size_t)LW_FIXED_BYTES)\n"
"#endif\n"
"static size_t lw_fixed_n;\n"
"#if LW_HEAP_ROOT\n"
"static unsigned char **lw_hp_ch; static size_t *lw_hp_cap, *lw_hp_base;\n"
"static size_t lw_hp_n, lw_hp_max, lw_hp_cur, lw_hp_off;\n"
"#endif\n"
"static unsigned char *lw_root_take(int root, size_t n) {\n"
"    if (root == 0) {\n"
"        if (n > LW_FIXED_CAP - lw_fixed_n) return 0;\n"
"        { unsigned char *p = LW_FIXED_BASE + lw_fixed_n; lw_fixed_n += n; memset(p, 0, n); return p; }\n"
"    }\n"
"#if LW_HEAP_ROOT\n"
"    if (lw_hp_n && n <= lw_hp_cap[lw_hp_cur] - lw_hp_off) {\n"
"        unsigned char *p = lw_hp_ch[lw_hp_cur] + lw_hp_off; lw_hp_off += n; memset(p, 0, n); return p; }\n"
"    if (lw_hp_cur + 1 < lw_hp_n && n <= lw_hp_cap[lw_hp_cur + 1]) {\n"
"        lw_hp_cur++; lw_hp_off = n; memset(lw_hp_ch[lw_hp_cur], 0, n); return lw_hp_ch[lw_hp_cur]; }\n"
"    if (lw_hp_n == lw_hp_max) {\n"
"        size_t nm = lw_hp_max ? lw_hp_max * 2 : 16;\n"
"        unsigned char **c2 = (unsigned char **)realloc(lw_hp_ch, nm * sizeof *c2); if (!c2) return 0; lw_hp_ch = c2;\n"
"        size_t *k2 = (size_t *)realloc(lw_hp_cap, nm * sizeof *k2); if (!k2) return 0; lw_hp_cap = k2;\n"
"        size_t *b2 = (size_t *)realloc(lw_hp_base, nm * sizeof *b2); if (!b2) return 0; lw_hp_base = b2;\n"
"        lw_hp_max = nm; }\n"
"    { size_t want = n > (size_t)LW_HEAP_CHUNK ? n : (size_t)LW_HEAP_CHUNK;\n"
"      unsigned char *blk = (unsigned char *)malloc(want); if (!blk) return 0;\n"
"      for (size_t i = lw_hp_cur + 1; i < lw_hp_n; i++) free(lw_hp_ch[i]);\n"
"      size_t i = lw_hp_n ? lw_hp_cur + 1 : 0;\n"
"      lw_hp_ch[i] = blk; lw_hp_cap[i] = want; lw_hp_base[i] = i ? lw_hp_base[i - 1] + lw_hp_cap[i - 1] : 0;\n"
"      lw_hp_n = i + 1; lw_hp_cur = i; lw_hp_off = n; memset(blk, 0, n); return blk; }\n"
"#else\n"
"    (void)n; return 0;\n"
"#endif\n"
"}\n"
"static size_t lw_root_mark(int root) {\n"
"    if (root == 0) return lw_fixed_n;\n"
"#if LW_HEAP_ROOT\n"
"    return lw_hp_n ? lw_hp_base[lw_hp_cur] + lw_hp_off : 0;\n"
"#else\n"
"    return 0;\n"
"#endif\n"
"}\n"
"static void lw_root_reset(int root, size_t to) {\n"
"    if (to > lw_root_mark(root)) lw_panic(\"region reset needs its own mark\");\n"
"    if (root == 0) { lw_fixed_n = to; return; }\n"
"#if LW_HEAP_ROOT\n"
"    for (size_t i = lw_hp_n; i-- > 0; )\n"
"        if (to >= lw_hp_base[i]) { lw_hp_cur = i; lw_hp_off = to - lw_hp_base[i]; return; }\n"
"    lw_hp_cur = 0; lw_hp_off = 0;\n"
"#endif\n"
"}\n"
// ★ **OS 핸들 표는 OS 가 있을 때만 있다** — 파일·디렉터리·소켓·인자·환경.
//   프리스탠딩엔 이것들의 *타입*(FILE·DIR)조차 없다. 리프는 어차피 쓸 때만 나오지만
//   이 표는 무조건 나왔다 — 그래서 아무 op 도 안 쓰는 프로그램이 컴파일에 실패했다.
// ★ 이 표는 두 쪽이다: **표준 C 로 되는 것**(FILE·argv)과 **POSIX 라야 되는 것**
//   (DIR·소켓 fd·`environ`). Windows 는 앞쪽만 갖는다 — 뒤쪽을 같이 두면 mingw 가
//   `DIR` 을 모른다고 운다. *"호스티드"* 와 *"POSIX"* 는 같은 말이 아니다.
"#ifdef LW_HOSTED\n"
"static FILE *lw_files[32];   /* RFC-0069 §6 — 열린 파일. 핸들은 이 배열의 인덱스 */\n"
"static int lw_g_nargs; static char **lw_g_args;   /* RFC-0030 D2\u2032 — 프로그램 인자 */\n"
"#endif\n"
"#if defined(LW_HOSTED) && !defined(LW_NOPOSIX)\n"
"static DIR *lw_dirs[32];     /* RFC-0069 §6 — 열린 디렉터리. 핸들은 이 배열의 인덱스 */\n"
"static int lw_socks[32];     /* 소켓 fd+1 — 핸들은 이 배열의 인덱스(0=빈 슬롯) */\n"
"extern char **environ;                            /* RFC-0030 D2\u2032 — 환경 */\n"
"#endif\n"
// ★★★ **태그 경로의 option/result(lowv) → 빠른 경로의 (태그,값) 쌍(lw_r).**
//   경계에서 **한 번**만 푼다 — some/ok 는 상자에서 값을, none 은 0, err 는 오류코드를.
"static lw_r lw_opt2r(lowv v) {\n"
"    lw_r r; r.v = 0; r.f = 0; r.s.p = 0; r.s.n = 0; for (int i=0;i<LW_RECB;i++) r.r[i]=0; r.tag = 0;\n"
"    if (v.tag == LWV_SOME) { r.tag = 1; r.v = lw_boxes[v.box].i; }\n"
"    else if (v.tag == LWV_NONE) { r.tag = 2; }\n"
"    else if (v.tag == LWV_OK)  { r.tag = 3; r.v = lw_boxes[v.box].i; }\n"
"    else if (v.tag == LWV_ERR) { r.tag = 4; r.v = v.i; }\n"
"    else lw_panic(\"an option/result argument\");\n"
"    return r; }\n"
"/* call depth: C recursion segfaults with NO message. A tool must never crash silently. */\n"
// ★★★ **호출 깊이 원장은 스레드마다 따로다.**
//   `parallel` 은 청크를 **진짜 pthread 로** 돌린다. 그런데 이 카운터는 **전역**이었다 —
//   K 개 스레드가 같은 정수를 증감했다(**데이터 경합**). 16 스레드면 깊이가 16배로 세어져
//   **멀쩡한 프로그램이 "call depth limit exceeded" 로 죽을 수 있다.**
//   ★ 그리고 이 언어의 자랑이 **DRF(자료 경합 없음)** 다 — 런타임이 그것을 어기고 있었다.
// ★ **호스트 전용**이다 — 베어메탈에는 `clock_gettime` 도 `nanosleep` 도 없다.
//   처음엔 공통 영역에 뒀다가 프리스탠딩 ISR 방출이 깨졌다(골든이 즉시 울었다).
"#if !defined(LW_FREESTANDING) && !defined(LW_NOPOSIX)\n"
// ★★★★★ **`static inline` 이다 — 안 쓰면 방출되지 않는다** (RFC-0075 Stage 5 Task 2, 2026-08-15).
//   그냥 `static` 이면 `-O0` 에서 **아무도 안 부르는 도우미까지 방출**되고, 그러면 순수 프로그램의
//   오브젝트가 `clock_gettime`·`fopen` 을 **링크 의존으로 끌고 온다**(실측 `nm -u`). Stage 5 가
//   *"안 쓴 것은 방출되지 않는다"* 를 요구하는데 그것이 **최적화 여부에 달려 있으면 약속이 아니다**.
//   ⇒ `static inline` 로 적는다: 쓰이면 그대로 나오고, 안 쓰이면 out-of-line 복사가 **안 생긴다**.
//   낱말 하나로 되는 일이라, 프렐류드를 쪼개는 큰 수술을 안 해도 된다.
// ★★★★ 방출된 C 는 **자립해야 한다** — `cc file.c -lm` 하나로 서야 하므로 벤더 헤더를
//   넣을 수 없다(넣었더니 `proven_sys_time.h: No such file` 로 죽었다). POSIX 를 직접 쓴다.
"#include <time.h>\n"
"static inline long long lw_time_now(void) {\n"
"    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);\n"
"    return (long long)ts.tv_sec * 1000000000ll + (long long)ts.tv_nsec;\n"
"}\n"
"static inline long long lw_time_sleep(long long ms) {\n"
"    long long t0 = lw_time_now();\n"
"    if (ms > 0) { if (ms > 60000) ms = 60000;\n"
"        struct timespec rq; rq.tv_sec = (time_t)(ms / 1000); rq.tv_nsec = (long)((ms % 1000) * 1000000L);\n"
"        nanosleep(&rq, 0); }\n"
"    return (lw_time_now() - t0) / 1000000ll;\n"
"}\n"
"static inline long long lw_rand_bytes(unsigned char *p, size_t n) {\n"
"    if (!n) return 0;\n"
"    FILE *f = fopen(\"/dev/urandom\", \"rb\");\n"
"    if (!f) return 0;\n"
"    size_t got = fread(p, 1, n, f);\n"
"    fclose(f);\n"
"    return (long long)got;\n"
"}\n"
"static inline long long lw_time_local(void) {\n"
"    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);\n"
"    time_t t = ts.tv_sec; struct tm tmv; localtime_r(&t, &tmv);\n"
"    return (long long)(((unsigned long long)(tmv.tm_year + 1900) << 36)\n"
"                     | ((unsigned long long)(tmv.tm_mon + 1) << 32)\n"
"                     | ((unsigned long long)tmv.tm_mday << 27)\n"
"                     | ((unsigned long long)tmv.tm_hour << 22)\n"
"                     | ((unsigned long long)tmv.tm_min << 16)\n"
"                     | ((unsigned long long)tmv.tm_sec << 10));\n"
"}\n"
"#endif\n"
"static _Thread_local int lw_depth;\n"
// ★★★★★ **터미널 리프 셋 — C 뒤끝에 없던 것** (2026-08-07 · 소유자 지시 ③).
//   `tty_raw`·`tty_read`·`tty_size` 는 VM 에만 있었고 이 파일에는 **case 조차 없었다**
//   (gcc 가 -Wswitch 로 매번 말했다). 태그 경로 switch 에 default 가 없어 그 명령들이
//   **조용히 사라졌고**, 스택이 어긋난 채로 다음이 돌았다: 실측에서 `tty_raw` 가 파이프인데
//   **true** 를 냈고 `tty_size`/`tty_read` 는 "is_some needs an option" 으로 죽었다.
//   ⇒ VM 과 **같은 뜻**으로 옮긴다(low_ir.c 의 IRW_TTY* 와 한 줄씩 대응):
//     · 터미널이 아니면 raw 는 **거짓**, size 는 **none** — 모르면 모른다고 답한다
//     · 원래 termios 를 기억한다: 못 돌아오면 사용자의 셸이 망가진 채 남는다
//   ★ **호스트 전용이다.** 처음엔 무조건 넣었다가 프리스탠딩 방출이 깨졌다 —
//     베어메탈에는 `termios.h` 도 `ioctl` 도 없다(골든의 ISR 방출 검사가 즉시 울었다).
//     `cap tty` 자체가 호스트의 권한이므로 이 코드가 거기 있을 이유도 없다.
"#if !defined(LW_FREESTANDING) && !defined(LW_NOPOSIX)\n"
"#include <termios.h>\n#include <sys/ioctl.h>\n"
"static struct termios lw_tty_saved;\n"
"static int lw_tty_have_saved;\n"
"static inline long long lw_tty_raw(long long on) {\n"
"    if (!isatty(0)) return 0;\n"
"    if (on) {\n"
"        if (!lw_tty_have_saved) { if (tcgetattr(0, &lw_tty_saved) != 0) return 0; lw_tty_have_saved = 1; }\n"
"        struct termios raw = lw_tty_saved;\n"
"        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);\n"
"        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;\n"
"        return tcsetattr(0, TCSANOW, &raw) == 0 ? 1 : 0;\n"
"    }\n"
"    if (!lw_tty_have_saved) return 1;\n"
"    return tcsetattr(0, TCSANOW, &lw_tty_saved) == 0 ? 1 : 0;\n"
"}\n"
"static inline lowv lw_tty_read(lowv dst) {\n"
"    lowv n = {0};\n"
"    ssize_t got = read(0, (void *)(uintptr_t)dst.p, (size_t)dst.n);\n"
"    if (got < 0) { n.tag = LWV_NONE; return n; }\n"
"    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"    lw_boxes[lw_nbox] = lw_int((long long)got);\n"
"    n.tag = LWV_SOME; n.box = lw_nbox++; return n;\n"
"}\n"
"static inline lowv lw_tty_size(void) {\n"
"    lowv n = {0};\n"
"    struct winsize ws;\n"
"    if (!isatty(1) || ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) { n.tag = LWV_NONE; return n; }\n"
"    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"    lw_boxes[lw_nbox] = lw_int(((long long)ws.ws_row << 32) | (long long)ws.ws_col);\n"
"    n.tag = LWV_SOME; n.box = lw_nbox++; return n;\n"
"}\n"
"#endif\n"
"/* SPEC-007 28: channel switch — the value survives, only the channel changes */\n"
"static lowv lw_else_none(lowv v) {\n"
"    if (v.tag == LWV_ERR) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
"    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"    lw_boxes[lw_nbox] = lw_boxes[v.box];\n"
"    lowv r = {0}; r.tag = LWV_SOME; r.box = lw_nbox++; return r;\n"
"}\n"
"static lowv lw_else_err(lowv v, long long e) {\n"
"    if (v.tag == LWV_NONE) { lowv r = {0}; r.tag = LWV_ERR; r.i = e; return r; }\n"
"    if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"    lw_boxes[lw_nbox] = lw_boxes[v.box];\n"
"    lowv r = {0}; r.tag = LWV_OK; r.box = lw_nbox++; return r;\n"
"}\n"
"// RFC-0052 S3: 폭 충실 산술 — 기본 트랩, wrap_*/sat_* 가 정책을 고른다.\n"
"LW_HOT long long lw_lo(int bits, int sg) { if (!sg) return 0; return bits >= 64 ? INT64_MIN : -(1ll << (bits - 1)); }\n"
"LW_HOT long long lw_hi(int bits, int sg) { if (bits >= 64) return sg ? INT64_MAX : -1;\n"
"    return sg ? (1ll << (bits - 1)) - 1 : (1ll << bits) - 1; }\n"
"LW_HOT int lw_fits(long long r, int bits, int sg) { if (bits >= 64) return 1;\n"
"    return r >= lw_lo(bits, sg) && r <= lw_hi(bits, sg); }\n"
"LW_HOT long long lw_wrap(long long r, int bits, int sg) { if (bits >= 64) return r;\n"
"    unsigned long long m = (1ull << bits) - 1, v = (unsigned long long)r & m;\n"
"    if (sg && (v & (1ull << (bits - 1)))) return (long long)(v | ~m);\n"
"    return (long long)v; }\n"
"LW_HOT long long lw_satv(long long r, int bits, int sg) { if (bits >= 64) return r;\n"
"    long long lo = lw_lo(bits, sg), hi = lw_hi(bits, sg);\n"
"    return r < lo ? lo : (r > hi ? hi : r); }\n"
"// D9: f32 는 f32 정밀도로 계산한다 — 중간 결과를 double 에 남기지 않는다.\n"
"LW_HOT double lw_fround(long long meta, double r) {\n"
"    if ((meta & 0x1000) && (meta & 0x20000) && (meta & 0xff) == 32) return (double)(float)r;\n"
"    return r; }\n"
// ★★★ **산술의 스칼라 핵을 하나만 둔다.**
//   아래의 빠른 경로(`lw_s_*`)는 `lowv` 를 안 쓴다 — 그렇다고 산술을 **두 벌** 적으면
//   그것이 곧 갈림이다(교훈 7: 한 뜻에 두 표현이 있으면 반드시 갈린다).
//   ⇒ 계산은 **여기 한 곳**에 있고, `lowv` 판본은 그것을 **감싼다.**
"LW_HOT long long lw_arith_raw(long long meta, int op, long long x, long long y, int *ovf) {\n"
"    int bits = (int)(meta & 0xff), known = (meta & 0x1000) != 0, sg = lw_ty_signed(meta);\n"
"    *ovf = 0;\n"
"    if ((meta & 0x100000) || !known || !bits)   // RFC-0053: 증명됨 → 검사 없는 원시 산술\n"
"        return (long long)(op == 0 ? (unsigned long long)x + (unsigned long long)y\n"
"                         : op == 1 ? (unsigned long long)x - (unsigned long long)y\n"
"                         :           (unsigned long long)x * (unsigned long long)y);\n"
"    long long e = 0;\n"
"    if (!sg && bits >= 64) { unsigned long long ux = (unsigned long long)x, uy = (unsigned long long)y, ur = 0;\n"
"        *ovf = op == 0 ? __builtin_add_overflow(ux, uy, &ur)\n"
"             : op == 1 ? __builtin_sub_overflow(ux, uy, &ur) : __builtin_mul_overflow(ux, uy, &ur);\n"
"        e = (long long)ur; }\n"
"    else { *ovf = op == 0 ? __builtin_add_overflow(x, y, &e)\n"
"                : op == 1 ? __builtin_sub_overflow(x, y, &e) : __builtin_mul_overflow(x, y, &e);\n"
"           if (!*ovf && !lw_fits(e, bits, sg)) *ovf = 1; }\n"
"    return e; }\n"
"LW_HOT long long lw_arith_i(long long meta, int op, long long x, long long y) {\n"
"    int bits = (int)(meta & 0xff), sg = lw_ty_signed(meta), ovf;\n"
"    // ☞ **래핑을 따로 빠르게 하려 해 봤고, 되돌렸다** (2026-07-29 실측).\n"
"    //   `wrap_*` 는 넘침을 먼저 찾을 필요가 없어 보였다(답은 늘 `무부호 연산 뒤 폭 자르기`).\n"
"    //   그런데 **gcc 가 이미 그렇게 접고 있었다** — 64비트에서 측정 차이가 0 이었다(14.86 vs 14.90ms).\n"
"    //   대신 폭 미상(bits=0)에서 `lw_wrap` 이 0 으로 마스킹하는 함정을 밟아 차분 스윕이 10건\n"
"    //   발산했다. **이득 0 · 함정 1** ⇒ 한 가지 형태로 되돌린다(교훈 7).\n"
"    long long e = lw_arith_raw(meta, op, x, y, &ovf);\n"
"    if (!ovf) return e;\n"
"    if (meta & 0x4000) return lw_wrap(e, bits, sg);            // wrap_*: 정의된 래핑\n"
"    if (meta & 0x8000) {                                       // sat_*: 포화\n"
"        long long sat;\n"
"        if (op == 2) sat = (!sg || ((x < 0) == (y < 0))) ? lw_hi(bits, sg) : lw_lo(bits, sg);\n"
"        else if (op == 0) sat = (!sg || x > 0) ? lw_hi(bits, sg) : lw_lo(bits, sg);\n"
"        else sat = (!sg || x < y) ? lw_lo(bits, sg) : lw_hi(bits, sg);\n"
"        if (!sg && bits >= 64) sat = (op == 1) ? 0 : (long long)~0ull;\n"
"        return sat; }\n"
"    lw_panic(\"integer overflow at the declared width\"); return 0; }\n"
"LW_HOT long long lw_divs_i(long long meta, long long x, long long y) {\n"
"    if (!y) lw_panic(\"divide by zero\");\n"
"    if (lw_ty_signed(meta)) { int bits = (int)(meta & 0xff), known = (meta & 0x1000) != 0;\n"
"        long long wmin = (known && bits && bits < 64) ? lw_lo(bits, 1) : INT64_MIN;\n"   /* ★ 선언 폭 min/-1 트랩 */
"        if (x == wmin && y == -1) lw_panic(\"division overflow (MIN / -1) at the declared width\");\n"
"        return x / y; }\n"
"    return (long long)((unsigned long long)x / (unsigned long long)y); }\n"
"LW_HOT long long lw_mods_i(long long meta, long long x, long long y) {\n"
"    if (!y) lw_panic(\"modulo by zero\");\n"
/* ★★★★ **`mod` 은 제수의 부호를 따른다** (SPEC-004 · RFC-0052 D7, 2026-08-02 수리).
 *   여기는 C 의 `%` 를 그대로 냈다 — 그건 **`rem`**(피제수 부호)이다. D7 은 *"관례를
 *   이름으로 고른다"* 이고 이 낱말의 이름은 `mod` 이므로 제수 부호여야 한다.
 *   ☞ 그 어긋남이 **기계 증명을 깨고 있었다**: SPEC-004 가 인용하는 `mod_is_a_safe_index`
 *     (Qed: 0 < n → 0 ≤ i mod n < n, **i 가 음수여도**)가 `mod (-7) 2 = -1` 에서 거짓이 된다.
 *   ★ 양 백엔드가 **똑같이** 틀렸으므로 차등 스윕이 못 봤고, `check-builtins` 는 op 의
 *     **이름만** 대조하므로 못 봤다 — 이름이 뜻과 다른 것은 아무 게이트도 안 재고 있었다. */
"    if (lw_ty_signed(meta)) { if (x == INT64_MIN && y == -1) return 0;\n"
"        long long r = x % y; if (r && ((r < 0) != (y < 0))) r += y; return r; }\n"
"    return (long long)((unsigned long long)x % (unsigned long long)y); }\n"
"LW_HOT long long lw_cmps_i(long long meta, int op, long long x, long long y) {\n"
"    if (lw_ty_signed(meta))\n"
"        return op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y;\n"
"    unsigned long long ux = (unsigned long long)x, uy = (unsigned long long)y;\n"
"    return op == 2 ? ux < uy : op == 3 ? ux <= uy : op == 4 ? ux > uy : ux >= uy; }\n"
"static lowv lw_arith(long long meta, int op, lowv a, lowv b) {   // 0 add 1 sub 2 mul\n"
"    if (a.tag == LWV_FLT && b.tag == LWV_FLT) { double x = lw_fval(a), y = lw_fval(b);\n"
"        return lw_flt(lw_fround(meta, op == 0 ? x + y : op == 1 ? x - y : x * y)); }\n"
"    if (a.tag == LWV_FLT || a.tag == LWV_VEC || a.tag == LWV_BITSET)\n"
"        return op == 0 ? lw_add(a, b) : op == 1 ? lw_sub(a, b) : lw_mul(a, b);\n"
"    long long x = lw_want_int(a, \"ints\"), y = lw_want_int(b, \"ints\");\n"
"    if (meta & 0x40000) {   // chk_*: 오버플로를 **값으로**(none) — 스칼라 경로에는 안 온다\n"
"        int ovf; long long e = lw_arith_raw(meta, op, x, y, &ovf);\n"
"        if (ovf) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
"        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"        lw_boxes[lw_nbox] = lw_int(e);\n"
"        lowv r2 = {0}; r2.tag = LWV_SOME; r2.box = lw_nbox++; return r2; }\n"
"    return lw_int(lw_arith_i(meta, op, x, y)); }\n"
"// RFC-0052 S2: meta 가 부호를 싣는다(IR_TY_KNOWN/IR_TY_SIGNED). 0 = 미상 → 부호 있음.\n"
"LW_HOT int lw_ty_signed(long long meta) { return !(meta & 0x1000) || (meta & 0x2000); }\n"
"LW_HOT double lw_fround(long long meta, double r);\n"
"static lowv lw_divs(long long meta, lowv a, lowv b) {\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vbin(3, a, b);\n"
"    if (a.tag == LWV_FLT && b.tag == LWV_FLT) return lw_flt(lw_fround(meta, lw_fval(a) / lw_fval(b)));\n"
"    return lw_int(lw_divs_i(meta, lw_want_int(a, \"ints\"), lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_mods(long long meta, lowv a, lowv b) {\n"
"    return lw_int(lw_mods_i(meta, lw_want_int(a, \"ints\"), lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_cmps(long long meta, int op, lowv a, lowv b) {\n"
"    if (a.tag == LWV_FLT || a.tag == LWV_VEC) return lw_cmp(op, a, b);\n"
"    return lw_int(lw_cmps_i(meta, op, lw_want_int(a, \"ints\"), lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_div(lowv a, lowv b) {\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vbin(3, a, b);\n"
"    if (a.tag == LWV_FLT && b.tag == LWV_FLT) return lw_flt(lw_fval(a) / lw_fval(b));\n"
"    long long x = lw_want_int(a, \"ints\"), y = lw_want_int(b, \"ints\");\n"
"    if (!y) lw_panic(\"divide by zero\"); return lw_int(x / y); }\n"
"static lowv lw_mod(lowv a, lowv b) { long long x = lw_want_int(a, \"ints\"), y = lw_want_int(b, \"ints\");\n"
"    if (!y) lw_panic(\"modulo by zero\"); return lw_int(x % y); }\n"
/* ★ 참조를 따라간다(2026-07-19) — 네이티브엔 세대·빌림 추적이 없다(VM 이 그 역할: RFC-0017 V1). */
"static lowv lw_thru(lowv v) { return (v.tag == LWV_REF) ? *v.q : v; }\n"
// ★★★ 해시 둘 (RFC-0090 N3) — **공통 영역**이다: `#ifndef LW_FREESTANDING` **바깥**.
//   바이트를 읽어 수를 내는 순수 계산이라 OS 가 필요 없고, 베어메탈에서도 써야 한다.
//   ☞ 이 구분을 이번 저장소에서 **두 번 어겼다**(termios·time 을 공통 영역에 넣어
//     프리스탠딩 방출을 깨뜨렸다). 순수한 것은 공통, 세상에 닿는 것만 host 안.
// ★ VM(low_ir.c) 과 **같은 상수·같은 순서**여야 한다 — 표 대신 비트 8 번을 도는 형태를
//   고른 것도 그래서다: 256칸 표를 두 곳에 복사하면 한쪽만 고쳐질 자리가 하나 더 생긴다.
"static long long lw_hash64(const unsigned char *p, size_t n) {\n"
"    unsigned long long h = 0xcbf29ce484222325ull;\n"
"    for (size_t i = 0; i < n; i++) { h ^= (unsigned long long)p[i]; h *= 0x100000001b3ull; }\n"
"    return (long long)h;\n"
"}\n"
LOW_SHA256_C_SOURCE
LOW_SHA512_C_SOURCE
// ★★★★★ **AES 라운드를 기계 명령으로** (RFC-0119 §10, 2026-09-23) — 방출 C 에만 있다.
//   `_mm_aesenc_si128` 은 SubBytes·ShiftRows·MixColumns·AddRoundKey 를 한 명령에 한다.
//   ★ 표를 안 읽으므로 **상수시간**이다 — 소프트 판(S-box 표)은 캐시 타이밍을 흘린다.
//     즉 이 경로는 빠르기만 한 것이 아니라 **타이밍 성질이 다르다**(그 사실을 적는다).
//   ★★ VM 은 언제나 표 판이다 ⇒ 오라클(VM ≡ 네이티브)이 이 경로의 차등 시험이다.
"#if defined(__x86_64__) && (LW_HW_AES == 1 || LW_HW_AES == 2)\n"
"#include <immintrin.h>\n"
"__attribute__((target(\"aes\"))) static void lw_aes_block_hw(const unsigned char *rk, unsigned char *s) {\n"
"    __m128i st = _mm_loadu_si128((const __m128i *)s); int r;\n"
"    st = _mm_xor_si128(st, _mm_loadu_si128((const __m128i *)rk));\n"
"    for (r = 1; r < 10; r++) st = _mm_aesenc_si128(st, _mm_loadu_si128((const __m128i *)(rk + 16 * r)));\n"
"    st = _mm_aesenclast_si128(st, _mm_loadu_si128((const __m128i *)(rk + 160)));\n"
"    _mm_storeu_si128((__m128i *)s, st); }\n"
"#endif\n"
"#if defined(__x86_64__) && LW_HW_AES == 2\n"
"static int lw_hw_aes_ok = -1;\n"
"static int lw_hw_aes_pick(void) {\n"
"    if (lw_hw_aes_ok < 0) { __builtin_cpu_init(); lw_hw_aes_ok = __builtin_cpu_supports(\"aes\") ? 1 : 0; }\n"
"    return lw_hw_aes_ok; }\n"
"#endif\n"
// ★★★★★ **캐리 없는 곱셈의 핵** (RFC-0119, 2026-09-23) — 기계 명령이 있으면 그것을, 없으면 같은
//   답을 내는 소프트 판을 쓴다. **갈림은 여기 한 곳**이다(교훈 7: 계산은 한 곳에).
//   ★ 범위는 **빌드가** 정한다(`--hw`): `none` 은 소프트만, `pclmul` 은 기계 명령만(검사 없음),
//     `auto` 는 둘을 담고 **시작할 때 한 번** 골라 고정한다 — 실행 중에 안 바뀌므로 잰 수가 재현된다.
//   ★★ VM 은 언제나 소프트다 ⇒ 오라클(VM ≡ 네이티브)이 곧 기계 경로의 차등 시험이다.
"#if defined(__x86_64__) && (LW_HW_CLMUL == 1 || LW_HW_CLMUL == 2)\n"
"#include <immintrin.h>\n"
"__attribute__((target(\"pclmul\"))) static void lw_clmul64_hw(unsigned long long a, unsigned long long b,\n"
"                                                             unsigned long long *lo, unsigned long long *hi) {\n"
"    __m128i p = _mm_clmulepi64_si128(_mm_set_epi64x(0, (long long)a), _mm_set_epi64x(0, (long long)b), 0x00);\n"
"    *lo = (unsigned long long)_mm_cvtsi128_si64(p);\n"
"    *hi = (unsigned long long)_mm_cvtsi128_si64(_mm_srli_si128(p, 8)); }   /* srli 는 SSE2 — sse4.1 을 안 부른다 */\n"
"#endif\n"
"static void lw_clmul64_soft(unsigned long long a, unsigned long long b,\n"
"                            unsigned long long *lo, unsigned long long *hi) {\n"
"    unsigned long long l = 0, h = 0;\n"
"    for (int i = 0; i < 64; i++) if ((b >> i) & 1ull) { l ^= a << i; if (i) h ^= a >> (64 - i); }\n"
"    *lo = l; *hi = h; }\n"
"#if defined(__x86_64__) && LW_HW_CLMUL == 2\n"
"static int lw_hw_clmul_ok = -1;\n"
"static int lw_hw_clmul_pick(void) {\n"
"    if (lw_hw_clmul_ok < 0) { __builtin_cpu_init(); lw_hw_clmul_ok = __builtin_cpu_supports(\"pclmul\") ? 1 : 0; }\n"
"    return lw_hw_clmul_ok; }\n"
"#endif\n"
"static void lw_clmul64(unsigned long long a, unsigned long long b,\n"
"                       unsigned long long *lo, unsigned long long *hi) {\n"
"#if defined(__x86_64__) && LW_HW_CLMUL == 1\n"
"    lw_clmul64_hw(a, b, lo, hi);\n"
"#elif defined(__x86_64__) && LW_HW_CLMUL == 2\n"
"    if (lw_hw_clmul_pick()) lw_clmul64_hw(a, b, lo, hi); else lw_clmul64_soft(a, b, lo, hi);\n"
"#else\n"
"    lw_clmul64_soft(a, b, lo, hi);\n"
"#endif\n"
"}\n"
LOW_AES_C_SOURCE
// ★ `aes_round` 의 기계 판 — `aesenc`/`aesenclast` 한 명령이다(표를 안 읽는다 = 상수시간).
"#if defined(__x86_64__) && (LW_HW_AES == 1 || LW_HW_AES == 2)\n"
"__attribute__((target(\"aes\"))) static long long lw_aes_round_hw(void *sp2, unsigned long long slen2,\n"
"                                                                 const void *rkp, unsigned long long rklen, int last) {\n"
"    __m128i st, rk;\n"
"    if (slen2 < 16 || rklen < 16) return 0;\n"
"    st = _mm_loadu_si128((const __m128i *)sp2); rk = _mm_loadu_si128((const __m128i *)rkp);\n"
"    st = last ? _mm_aesenclast_si128(st, rk) : _mm_aesenc_si128(st, rk);\n"
"    _mm_storeu_si128((__m128i *)sp2, st); return 16; }\n"
"static long long lw_aes_round_x(void *sp2, unsigned long long slen2,\n"
"                                const void *rkp, unsigned long long rklen, int last) {\n"
"#if LW_HW_AES == 2\n"
"    if (!lw_hw_aes_pick()) return lw_aes_round(sp2, slen2, rkp, rklen, last);\n"
"#endif\n"
"    return lw_aes_round_hw(sp2, slen2, rkp, rklen, last); }\n"
"#else\n"
"#define lw_aes_round_x lw_aes_round\n"
"#endif\n"
// ★ `aes_ctr` 의 기계 판 — 카운터 규약(마지막 4 바이트 빅엔디언)은 소프트와 **같다**.
//   같은 일을 두 곳에 적는 셈이라, 골든이 둘을 **같은 입력으로 맞댄다**(그것이 값이다).
// ★★★★★ **여덟 덩이를 한꺼번에 돈다** (X-0051, 2026-09-24) — `aesenc` 는 내는 데 한 사이클,
//   답이 나오기까지 넷이다. 한 덩이를 직렬로 돌리면 열 라운드 내내 파이프라인이 **비어 있다**
//   (실측 618 MB/s). 서로 **독립인** 카운터 블록 여덟을 엮으면 그 구멍이 메워진다.
//   ☞ 카운터는 여전히 소프트와 같은 방식(바이트 단위 inc32)으로 올린다 — 여덟을 32비트 덧셈
//     하나로 바꾸면 `ff ff ff ff` 에서 도는 자리가 달라진다. **빠르게 하려고 뜻을 바꾸지 않는다.**
//   ☞ 꼬리(여덟 덩이가 안 되는 나머지·짧은 마지막 블록)는 **손대지 않은 한 덩이 길**로 간다.
"#if defined(__x86_64__) && (LW_HW_AES == 1 || LW_HW_AES == 2)\n"
"#define LW_AES8_ROUND(K) do { \\\n"
"    s0 = _mm_aesenc_si128(s0, K); s1 = _mm_aesenc_si128(s1, K);\\\n"
"    s2 = _mm_aesenc_si128(s2, K); s3 = _mm_aesenc_si128(s3, K);\\\n"
"    s4 = _mm_aesenc_si128(s4, K); s5 = _mm_aesenc_si128(s5, K);\\\n"
"    s6 = _mm_aesenc_si128(s6, K); s7 = _mm_aesenc_si128(s7, K); } while (0)\n"
"#define LW_AES8_XOR(J, S) do { \\\n"
"    _mm_storeu_si128((__m128i *)(dst + off + 16 * (J)),\\\n"
"        _mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + off + 16 * (J))), S)); } while (0)\n"
"__attribute__((target(\"aes\"))) static void lw_aes_ctr8(const unsigned char *rk, unsigned char *ctr,\n"
"                                                        const unsigned char *src, unsigned long long slen,\n"
"                                                        unsigned char *dst, unsigned long long *done) {\n"
"    __m128i k[11], cb, s0, s1, s2, s3, s4, s5, s6, s7; unsigned long long off; unsigned int n; int r;\n"
// ★★★★★ **카운터를 레지스터에서 세운다** (X-0054, 2026-09-24). 그전에는 덩이 여덟마다
//   128 바이트 버퍼에 **바이트 단위로** 적었다가 다시 읽었다 — 그 왕복이 실측 2,499 MB/s 의
//   상당 부분이었다(같은 명령을 쓰는 OpenSSL 은 5,483).
//   ☞ 규약은 그대로다: 올라가는 것은 **뒤 4 바이트뿐**이고 byte 11 로 넘지 않는다. 그것은 곧
//     32 비트 빅엔디언 증가이므로 `n + j` 한 번으로 **같은 뜻**이다(랩도 같다).
//   ☞ 앞 12 바이트는 안 바뀌므로 한 번 실어 두고(`cb`), 매 덩이는 뒤 4 바이트만 얹는다.
"    for (r = 0; r < 11; r++) k[r] = _mm_loadu_si128((const __m128i *)(rk + 16 * r));\n"
"    cb = _mm_and_si128(_mm_loadu_si128((const __m128i *)ctr), _mm_set_epi32(0, -1, -1, -1));\n"
"    n = ((unsigned int)ctr[12] << 24) | ((unsigned int)ctr[13] << 16)\n"
"      | ((unsigned int)ctr[14] << 8)  | (unsigned int)ctr[15];\n"
"#define LW_CTRBLK(J) _mm_xor_si128(_mm_or_si128(cb,\\\n"
"    _mm_slli_si128(_mm_cvtsi32_si128((int)__builtin_bswap32(n + (unsigned int)(J))), 12)), k[0])\n"
"    for (off = 0; off + 128 <= slen; off += 128) {\n"
"        s0 = LW_CTRBLK(0); s1 = LW_CTRBLK(1); s2 = LW_CTRBLK(2); s3 = LW_CTRBLK(3);\n"
"        s4 = LW_CTRBLK(4); s5 = LW_CTRBLK(5); s6 = LW_CTRBLK(6); s7 = LW_CTRBLK(7);\n"
"        n += 8u;\n"
"        LW_AES8_ROUND(k[1]); LW_AES8_ROUND(k[2]); LW_AES8_ROUND(k[3]);\n"
"        LW_AES8_ROUND(k[4]); LW_AES8_ROUND(k[5]); LW_AES8_ROUND(k[6]);\n"
"        LW_AES8_ROUND(k[7]); LW_AES8_ROUND(k[8]); LW_AES8_ROUND(k[9]);\n"
"        s0 = _mm_aesenclast_si128(s0, k[10]); s1 = _mm_aesenclast_si128(s1, k[10]);\n"
"        s2 = _mm_aesenclast_si128(s2, k[10]); s3 = _mm_aesenclast_si128(s3, k[10]);\n"
"        s4 = _mm_aesenclast_si128(s4, k[10]); s5 = _mm_aesenclast_si128(s5, k[10]);\n"
"        s6 = _mm_aesenclast_si128(s6, k[10]); s7 = _mm_aesenclast_si128(s7, k[10]);\n"
"        LW_AES8_XOR(0, s0); LW_AES8_XOR(1, s1); LW_AES8_XOR(2, s2); LW_AES8_XOR(3, s3);\n"
"        LW_AES8_XOR(4, s4); LW_AES8_XOR(5, s5); LW_AES8_XOR(6, s6); LW_AES8_XOR(7, s7);\n"
"    }\n"
"    _mm_storeu_si128((__m128i *)ctr, _mm_or_si128(cb,\n"
"        _mm_slli_si128(_mm_cvtsi32_si128((int)__builtin_bswap32(n)), 12)));\n"
"    *done = off; }\n"
"#undef LW_CTRBLK\n"
"static long long lw_aes_ctr_x(const void *keyp, unsigned long long klen, void *ctrp, unsigned long long clen,\n"
"                              const void *srcp, unsigned long long slen, void *dstp, unsigned long long dlen) {\n"
"#if LW_HW_AES == 2\n"
"    if (!lw_hw_aes_pick()) return lw_aes_ctr(keyp, klen, ctrp, clen, srcp, slen, dstp, dlen);\n"
"#endif\n"
"    { const unsigned char *key = (const unsigned char *)keyp, *src = (const unsigned char *)srcp;\n"
"      unsigned char *ctr = (unsigned char *)ctrp, *dst = (unsigned char *)dstp;\n"
"      unsigned char rk[176], ks[16]; unsigned long long off = 0; unsigned int c; int i;\n"
"      if (klen != 16 || clen != 16 || dlen < slen) return 0;\n"
"      lw_aes_expand(key, rk);\n"
"      lw_aes_ctr8(rk, ctr, src, slen, dst, &off);\n"
"      for (; off < slen; off += 16) {\n"
"          unsigned long long n = slen - off; if (n > 16) n = 16;\n"
"          for (i = 0; i < 16; i++) ks[i] = ctr[i];\n"
"          lw_aes_block_hw(rk, ks);\n"
"          for (i = 0; i < (int)n; i++) dst[off + i] = (unsigned char)(src[off + i] ^ ks[i]);\n"
"          c = (unsigned int)ctr[15] + 1u; ctr[15] = (unsigned char)c;\n"
"          if (c >> 8) { c = (unsigned int)ctr[14] + 1u; ctr[14] = (unsigned char)c;\n"
"          if (c >> 8) { c = (unsigned int)ctr[13] + 1u; ctr[13] = (unsigned char)c;\n"
"          if (c >> 8) { ctr[12] = (unsigned char)(ctr[12] + 1); } } }\n"
"      }\n"
"      return (long long)slen; } }\n"
"#else\n"
"#define lw_aes_ctr_x lw_aes_ctr\n"
"#endif\n"
// ★★★★★ **캐리 없는 곱셈으로 하는 GHASH** (RFC-0119, 2026-09-23) — **방출 C 에만 있다.**
//   VM 은 `low_aes.h` 의 가림(mask) 판을 그대로 돈다 ⇒ 오라클(VM ≡ 네이티브)이 이 경로의 차등 시험이다.
//   GCM 의 비트 차례는 뒤집혀 있다. 뒤집힌 표현끼리 곱하면 답이 한 비트 밀리므로 256 비트 곱을
//   **왼쪽으로 한 칸 민 뒤** 0xc2000000_00000000 으로 축약한다(고전적인 방법).
//   ☞ 분기도 표 읽기도 없다 — 가림 판과 같은 뜻이고 타이밍 성질도 같다.
"#if LW_HW_CLMUL == 1 || LW_HW_CLMUL == 2\n"
"static void lw_gh_shr128(unsigned long long h, unsigned long long l, int k,\n"
"                         unsigned long long *oh, unsigned long long *ol, unsigned long long *ov) {\n"
"    *oh = h >> k; *ol = (l >> k) | (h << (64 - k)); *ov = l << (64 - k); }\n"
// ★★★★★ **H 를 한 칸 밀어 두고, 축약을 곱셈 둘로 한다** (X-0054, 2026-09-24).
//   그전에는 곱마다 256 비트를 왼쪽으로 한 칸 민 뒤 시프트 사슬(>>1 · >>2 · >>7 을 두 번씩)로
//   축약했다 — 실측 2,004 MB/s. 같은 기계의 OpenSSL 은 7 GB/s 언저리다.
//   ☞ 대신 **H 를 x 로 한 번 나눠 두고**(`lw_gh_divx`, 부름당 한 번), 곱마다는 몽고메리 꼴
//     축약을 **캐리 없는 곱셈 둘**로 한다. 이 축약은 덤으로 **x 를 곱한다**(모형으로 쟀다:
//     `mont(kara(A,B)) = A·B·x`) — 미리 나눠 둔 x^-1 이 그것을 정확히 지운다.
//     곧 **한 칸 밀기가 사라진다.**
//   ☞ 거듭제곱도 같은 셈으로 낸다: H_k' = mont(kara(H_{k-1}', H_1')) = H^k · x^-1. 곱하는 쪽이
//     언제나 « ·x^-1 이 붙은 쪽» 이므로 A · H_k' 는 A·H^k 로 떨어진다.
//   ★ 상수를 **손으로 추측하지 않았다**: 첫 판은 x 를 곱해 두었다가 답이 틀렸고, 골든이 세워 둔
//     소프트 대조가 그 자리에서 잡았다. 그 뒤 축약을 파이썬으로 모형화해 덤 상수가 x 임을
//     재고(=x^1), 그 역을 미리 곱하도록 고쳤다. ☞ *상수는 재는 것이지 고르는 것이 아니다.*
//   ★★ 여기는 **2026-09-23 에 틀렸던 바로 그 자리**다. 그래서 골든이 `--hw none` 과 맞대는
//     길이에 5·16·32·48·64·80·96·112·128·144·257 을 모두 둔다.
"static void lw_gh_divx(unsigned long long v0, unsigned long long v1,\n"
"                       unsigned long long *o0, unsigned long long *o1) {\n"
"    unsigned long long msb = v0 >> 63;\n"
"    unsigned long long a0 = v0 ^ (0xe100000000000000ull & (0ull - msb));\n"
"    *o0 = (a0 << 1) | (v1 >> 63);\n"
"    *o1 = (v1 << 1) | msb; }\n"
// ★ 카라추바 — 곱셈 셋으로 128×128. 축약은 **안 한다**(모아서 한 번 하려고 갈라 둔다).
"__attribute__((target(\"pclmul,ssse3\"))) static void lw_gh_kara(__m128i a, __m128i b,\n"
"                                                                __m128i *lo, __m128i *hi) {\n"
"    __m128i t0 = _mm_clmulepi64_si128(a, b, 0x00);\n"
"    __m128i t3 = _mm_clmulepi64_si128(a, b, 0x11);\n"
"    __m128i am = _mm_xor_si128(a, _mm_shuffle_epi32(a, 0x4e));\n"
"    __m128i bm = _mm_xor_si128(b, _mm_shuffle_epi32(b, 0x4e));\n"
"    __m128i tm = _mm_clmulepi64_si128(am, bm, 0x00);\n"
"    tm = _mm_xor_si128(tm, _mm_xor_si128(t0, t3));\n"
"    *lo = _mm_xor_si128(t0, _mm_slli_si128(tm, 8));\n"
"    *hi = _mm_xor_si128(t3, _mm_srli_si128(tm, 8)); }\n"
// ★ 몽고메리 꼴 축약 — (lo·x^-128 + hi) mod (x^128+x^7+x^2+x+1). 곱셈 둘, 분기 없음, 표 없음.
"__attribute__((target(\"pclmul,ssse3\"))) static __m128i lw_gh_mont(__m128i lo, __m128i hi) {\n"
"    const __m128i pp = _mm_set_epi64x((long long)0xc200000000000000ull, 1ll);\n"
"    __m128i t = _mm_clmulepi64_si128(lo, pp, 0x10);\n"
"    lo = _mm_shuffle_epi32(lo, 0x4e);\n"
"    lo = _mm_xor_si128(lo, t);\n"
"    t = _mm_clmulepi64_si128(lo, pp, 0x10);\n"
"    lo = _mm_shuffle_epi32(lo, 0x4e);\n"
"    lo = _mm_xor_si128(lo, t);\n"
"    return _mm_xor_si128(lo, hi); }\n"
// ★ 16 바이트를 GCM 의 자리로 읽는다 — 앞 8 바이트가 **윗말**이다(빅엔디언).
"__attribute__((target(\"pclmul,ssse3\"))) static __m128i lw_gh_load(const unsigned char *p) {\n"
"    const __m128i bs = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);\n"
"    return _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)p), bs); }\n"
"__attribute__((target(\"pclmul,ssse3\"))) static void lw_gh_mul_hw(unsigned long long x0, unsigned long long x1,\n"
"                         unsigned long long h0, unsigned long long h1,\n"
"                         unsigned long long *r0, unsigned long long *r1) {\n"
"    unsigned long long g0, g1; __m128i lo, hi, r;\n"
"    lw_gh_divx(h0, h1, &g0, &g1);\n"
"    lw_gh_kara(_mm_set_epi64x((long long)x0, (long long)x1),\n"
"               _mm_set_epi64x((long long)g0, (long long)g1), &lo, &hi);\n"
"    r = lw_gh_mont(lo, hi);\n"
"    *r1 = (unsigned long long)_mm_cvtsi128_si64(r);\n"
"    *r0 = (unsigned long long)_mm_cvtsi128_si64(_mm_srli_si128(r, 8)); }\n"
"__attribute__((target(\"pclmul,ssse3\"))) static void lw_ghash_core(unsigned long long h0, unsigned long long h1,\n"
"                            unsigned long long *zp0, unsigned long long *zp1,\n"
"                            const unsigned char *data, unsigned long long dlen) {\n"
"    unsigned long long off = 0, g0, g1;\n"
"    __m128i hp[9], z, ql, qh, tl, th; int i, j;\n"
"    lw_gh_divx(h0, h1, &g0, &g1);\n"
"    hp[1] = _mm_set_epi64x((long long)g0, (long long)g1);\n"
"    z = _mm_set_epi64x((long long)*zp0, (long long)*zp1);\n"
"    if (dlen >= 64) {\n"
"        for (i = 2; i <= 8; i++) {\n"
"            lw_gh_kara(hp[i - 1], hp[1], &ql, &qh); hp[i] = lw_gh_mont(ql, qh); }\n"
"        for (; off + 128 <= dlen; off += 128) {\n"
"            lw_gh_kara(_mm_xor_si128(z, lw_gh_load(data + off)), hp[8], &ql, &qh);\n"
"            for (j = 1; j < 8; j++) {\n"
"                lw_gh_kara(lw_gh_load(data + off + 16 * j), hp[8 - j], &tl, &th);\n"
"                ql = _mm_xor_si128(ql, tl); qh = _mm_xor_si128(qh, th); }\n"
"            z = lw_gh_mont(ql, qh); }\n"
"        for (; off + 64 <= dlen; off += 64) {\n"
"            lw_gh_kara(_mm_xor_si128(z, lw_gh_load(data + off)), hp[4], &ql, &qh);\n"
"            for (j = 1; j < 4; j++) {\n"
"                lw_gh_kara(lw_gh_load(data + off + 16 * j), hp[4 - j], &tl, &th);\n"
"                ql = _mm_xor_si128(ql, tl); qh = _mm_xor_si128(qh, th); }\n"
"            z = lw_gh_mont(ql, qh); } }\n"
"    for (; off < dlen; off += 16) {\n"
"        unsigned char blk[16]; unsigned long long k, n = dlen - off;\n"
"        if (n > 16) n = 16;\n"
"        for (k = 0; k < 16; k++) blk[k] = (unsigned char)(k < n ? data[off + k] : 0);\n"
"        lw_gh_kara(_mm_xor_si128(z, lw_gh_load(blk)), hp[1], &ql, &qh);\n"
"        z = lw_gh_mont(ql, qh); }\n"
"    *zp1 = (unsigned long long)_mm_cvtsi128_si64(z);\n"
"    *zp0 = (unsigned long long)_mm_cvtsi128_si64(_mm_srli_si128(z, 8)); }\n"
"static long long lw_ghash_x(const void *hp, unsigned long long hlen, void *zp, unsigned long long zlen,\n"
"                            const void *datap, unsigned long long dlen) {\n"
"    const unsigned char *h = (const unsigned char *)hp, *data = (const unsigned char *)datap;\n"
"    unsigned char *z = (unsigned char *)zp; unsigned long long h0, h1, z0, z1;\n"
"    if (hlen != 16 || zlen != 16) return 0;\n"
// ★ 고르는 것은 **부름당 한 번**이다 — 곱셈마다 물으면 그 분기가 곧 비용이다(실측 533 → 737 MB/s).
"#if LW_HW_CLMUL == 2\n"
"    if (!lw_hw_clmul_pick()) return lw_ghash(hp, hlen, zp, zlen, datap, dlen);\n"
"#endif\n"
"    h0 = lw_gh_be64(h); h1 = lw_gh_be64(h + 8); z0 = lw_gh_be64(z); z1 = lw_gh_be64(z + 8);\n"
"    lw_ghash_core(h0, h1, &z0, &z1, data, dlen);\n"
"    lw_gh_put64(z, z0); lw_gh_put64(z + 8, z1);\n"
"    return (long long)dlen; }\n"
"#else\n"
"#define lw_ghash_x lw_ghash\n"
"#endif\n"
LOW_CHACHA_C_SOURCE
// ★ Poly1305 은 조각 셋(128 비트 중간값)과 조각 다섯 두 판이 있다 — **갈림은 여기 한 줄**이다.
//   답은 같고, 없는 기계에서는 다섯 판이 선다(`low_poly.h` 머리).
"#if defined(__SIZEOF_INT128__)\n"
LOW_POLY_C_SOURCE
"#else\n"
LOW_POLY_C_SOURCE_26
"#endif\n"
// ★★★★★ **ChaCha20 의 폭** (RFC-0122, 2026-09-24) — **방출 C 에만 있다.**
//   VM 은 `low_chacha.h` 의 셈 판을 그대로 돈다 ⇒ 오라클(VM ≡ 네이티브)이 이 경로의 차등 시험이다.
//   ChaCha 의 블록들은 서로 **독립**이다(카운터만 다르다). 그래서 넷·여덟을 한 레지스터의
//   레인에 나란히 싣고 같은 스무 라운드를 돌리면 그대로 넷·여덟 배가 된다 — 셈은 하나도 안 바뀐다.
//   ☞ SSE2 는 x86-64 의 바탕이므로 `auto` 에서 물을 것이 없다. AVX2 만 시작할 때 한 번 묻는다.
//   ☞ 꼬리(넷·여덟이 안 되는 나머지)는 **셈 판에 그대로 넘긴다** — 카운터 규약이 한 곳에만 산다.
//   ★ SSE2 에는 `pshufb` 가 없다(그것은 SSSE3 다). 그래서 16·8 자리 회전도 **밀고 합치기**로 한다.
"#if defined(__x86_64__) && (LW_HW_SSE2 == 1 || LW_HW_SSE2 == 2)\n"
"#include <immintrin.h>\n"
"#define LW_CC_ROTL(x, n) _mm_or_si128(_mm_slli_epi32(x, n), _mm_srli_epi32(x, 32 - (n)))\n"
"#define LW_CC_QR(a, b, c, d) do { \\\n"
"    a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a); d = LW_CC_ROTL(d, 16);\\\n"
"    c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c); b = LW_CC_ROTL(b, 12);\\\n"
"    a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a); d = LW_CC_ROTL(d, 8);\\\n"
"    c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c); b = LW_CC_ROTL(b, 7); } while (0)\n"
"#define LW_CC_TR(a, b, c, d, r0, r1, r2, r3) do { \\\n"
"    __m128i u0 = _mm_unpacklo_epi32(a, b), u1 = _mm_unpackhi_epi32(a, b);\\\n"
"    __m128i u2 = _mm_unpacklo_epi32(c, d), u3 = _mm_unpackhi_epi32(c, d);\\\n"
"    r0 = _mm_unpacklo_epi64(u0, u2); r1 = _mm_unpackhi_epi64(u0, u2);\\\n"
"    r2 = _mm_unpacklo_epi64(u1, u3); r3 = _mm_unpackhi_epi64(u1, u3); } while (0)\n"
"#define LW_CC_OUT(B, G, R) do { \\\n"
"    _mm_storeu_si128((__m128i *)(dst + off + 64 * (B) + 16 * (G)),\\\n"
"        _mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + off + 64 * (B) + 16 * (G))), R)); } while (0)\n"
"static void lw_chacha20_sse2(unsigned int *cs, const unsigned char *src, unsigned long long slen,\n"
"                             unsigned char *dst, unsigned long long *done) {\n"
// ★★★★ **원래 상태를 들고 있지 않는다** (X-0054, 2026-09-24). 그전에는 `v[16]` 과 `o[16]` 을
//   둘 다 들었다 — xmm 이 열여섯인데 서른둘을 원하니 절반이 스택으로 샜다. 열여섯 중 열다섯은
//   **같은 낱말의 브로드캐스트**라 다시 만드는 편이 싸다. 레지스터에 남는 것은 카운터 하나다.
"    __m128i v[16], c0, r0, r1, r2, r3; unsigned long long off; int i, r, g;\n"
"    for (off = 0; off + 256 <= slen; off += 256) {\n"
"        for (i = 0; i < 12; i++) v[i] = _mm_set1_epi32((int)cs[i]);\n"
"        v[12] = _mm_setr_epi32((int)cs[12], (int)(cs[12] + 1u), (int)(cs[12] + 2u), (int)(cs[12] + 3u));\n"
"        v[13] = _mm_set1_epi32((int)cs[13]); v[14] = _mm_set1_epi32((int)cs[14]);\n"
"        v[15] = _mm_set1_epi32((int)cs[15]);\n"
"        c0 = v[12];\n"
"        for (r = 0; r < 10; r++) {\n"
"            LW_CC_QR(v[0], v[4], v[8],  v[12]); LW_CC_QR(v[1], v[5], v[9],  v[13]);\n"
"            LW_CC_QR(v[2], v[6], v[10], v[14]); LW_CC_QR(v[3], v[7], v[11], v[15]);\n"
"            LW_CC_QR(v[0], v[5], v[10], v[15]); LW_CC_QR(v[1], v[6], v[11], v[12]);\n"
"            LW_CC_QR(v[2], v[7], v[8],  v[13]); LW_CC_QR(v[3], v[4], v[9],  v[14]); }\n"
"        for (i = 0; i < 16; i++)\n"
"            v[i] = _mm_add_epi32(v[i], (i == 12) ? c0 : _mm_set1_epi32((int)cs[i]));\n"
"        for (g = 0; g < 4; g++) {\n"
"            LW_CC_TR(v[4 * g], v[4 * g + 1], v[4 * g + 2], v[4 * g + 3], r0, r1, r2, r3);\n"
"            LW_CC_OUT(0, g, r0); LW_CC_OUT(1, g, r1); LW_CC_OUT(2, g, r2); LW_CC_OUT(3, g, r3); }\n"
"        cs[12] = (unsigned int)(cs[12] + 4u); }\n"
"    *done = off; }\n"
"#endif\n"
"#if defined(__x86_64__) && (LW_HW_AVX2 == 1 || LW_HW_AVX2 == 2)\n"
"#define LW_CW_ROTL(x, n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - (n)))\n"
// ★ 16 · 8 자리 회전은 **바이트 옮기기**다 — AVX2 에는 그 한 명령(`vpshufb`)이 있다.
//   밀고 합치기(둘)가 하나로 준다. 12 · 7 자리는 바이트 경계가 아니라 그대로 둔다.
"#define LW_CW_QR(a, b, c, d) do { \\\n"
"    a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); d = _mm256_shuffle_epi8(d, m16);\\\n"
"    c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); b = LW_CW_ROTL(b, 12);\\\n"
"    a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); d = _mm256_shuffle_epi8(d, m8);\\\n"
"    c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); b = LW_CW_ROTL(b, 7); } while (0)\n"
// ★ 256비트의 unpack 은 **128비트 레인 안에서** 섞는다. 그래서 4×4 자리바꿈을 하면 아래 레인에
//   블록 0~3, 위 레인에 블록 4~7 이 남는다 — 건너뛰는 순열 없이 두 반쪽을 그대로 내보내면 된다.
"#define LW_CW_TR(a, b, c, d, r0, r1, r2, r3) do { \\\n"
"    __m256i w0 = _mm256_unpacklo_epi32(a, b), w1 = _mm256_unpackhi_epi32(a, b);\\\n"
"    __m256i w2 = _mm256_unpacklo_epi32(c, d), w3 = _mm256_unpackhi_epi32(c, d);\\\n"
"    r0 = _mm256_unpacklo_epi64(w0, w2); r1 = _mm256_unpackhi_epi64(w0, w2);\\\n"
"    r2 = _mm256_unpacklo_epi64(w1, w3); r3 = _mm256_unpackhi_epi64(w1, w3); } while (0)\n"
"#define LW_CW_OUT(B, G, R) do { \\\n"
"    _mm_storeu_si128((__m128i *)(dst + off + 64 * (B) + 16 * (G)),\\\n"
"        _mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + off + 64 * (B) + 16 * (G))),\\\n"
"                      _mm256_castsi256_si128(R)));\\\n"
"    _mm_storeu_si128((__m128i *)(dst + off + 64 * ((B) + 4) + 16 * (G)),\\\n"
"        _mm_xor_si128(_mm_loadu_si128((const __m128i *)(src + off + 64 * ((B) + 4) + 16 * (G))),\\\n"
"                      _mm256_extracti128_si256(R, 1))); } while (0)\n"
"__attribute__((target(\"avx2\"))) static void lw_chacha20_avx2(unsigned int *cs, const unsigned char *src,\n"
"                            unsigned long long slen, unsigned char *dst, unsigned long long *done) {\n"
"    __m256i v[16], c0, r0, r1, r2, r3; unsigned long long off; int i, r, g;\n"
"    const __m256i m16 = _mm256_setr_epi8(2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13,\n"
"                                         2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13);\n"
"    const __m256i m8  = _mm256_setr_epi8(3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14,\n"
"                                         3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14);\n"
"    for (off = 0; off + 512 <= slen; off += 512) {\n"
"        for (i = 0; i < 12; i++) v[i] = _mm256_set1_epi32((int)cs[i]);\n"
"        v[12] = _mm256_setr_epi32((int)cs[12], (int)(cs[12] + 1u), (int)(cs[12] + 2u), (int)(cs[12] + 3u),\n"
"                                  (int)(cs[12] + 4u), (int)(cs[12] + 5u), (int)(cs[12] + 6u), (int)(cs[12] + 7u));\n"
"        v[13] = _mm256_set1_epi32((int)cs[13]); v[14] = _mm256_set1_epi32((int)cs[14]);\n"
"        v[15] = _mm256_set1_epi32((int)cs[15]);\n"
"        c0 = v[12];\n"
"        for (r = 0; r < 10; r++) {\n"
"            LW_CW_QR(v[0], v[4], v[8],  v[12]); LW_CW_QR(v[1], v[5], v[9],  v[13]);\n"
"            LW_CW_QR(v[2], v[6], v[10], v[14]); LW_CW_QR(v[3], v[7], v[11], v[15]);\n"
"            LW_CW_QR(v[0], v[5], v[10], v[15]); LW_CW_QR(v[1], v[6], v[11], v[12]);\n"
"            LW_CW_QR(v[2], v[7], v[8],  v[13]); LW_CW_QR(v[3], v[4], v[9],  v[14]); }\n"
"        for (i = 0; i < 16; i++)\n"
"            v[i] = _mm256_add_epi32(v[i], (i == 12) ? c0 : _mm256_set1_epi32((int)cs[i]));\n"
"        for (g = 0; g < 4; g++) {\n"
"            LW_CW_TR(v[4 * g], v[4 * g + 1], v[4 * g + 2], v[4 * g + 3], r0, r1, r2, r3);\n"
"            LW_CW_OUT(0, g, r0); LW_CW_OUT(1, g, r1); LW_CW_OUT(2, g, r2); LW_CW_OUT(3, g, r3); }\n"
"        cs[12] = (unsigned int)(cs[12] + 8u); }\n"
"    *done = off; }\n"
"#endif\n"
"#if defined(__x86_64__) && LW_HW_AVX2 == 2\n"
"static int lw_hw_avx2_ok = -1;\n"
"static int lw_hw_avx2_pick(void) {\n"
"    if (lw_hw_avx2_ok < 0) { __builtin_cpu_init(); lw_hw_avx2_ok = __builtin_cpu_supports(\"avx2\") ? 1 : 0; }\n"
"    return lw_hw_avx2_ok; }\n"
"#endif\n"
"#if defined(__x86_64__) && (LW_HW_SSE2 == 1 || LW_HW_SSE2 == 2)\n"
"static long long lw_chacha20_x(const void *keyp, unsigned long long klen, void *ctrp, unsigned long long clen,\n"
"                               const void *srcp, unsigned long long slen, void *dstp, unsigned long long dlen) {\n"
"    const unsigned char *key = (const unsigned char *)keyp, *src = (const unsigned char *)srcp;\n"
"    unsigned char *ctr = (unsigned char *)ctrp, *dst = (unsigned char *)dstp;\n"
"    unsigned int cs[16]; unsigned long long off = 0;\n"
"    if (klen != 32 || clen != 16 || dlen < slen) return 0;\n"
"    lw_chacha_state(key, ctr, cs);\n"
"#if LW_HW_AVX2 == 2\n"
"    if (lw_hw_avx2_pick()) lw_chacha20_avx2(cs, src, slen, dst, &off);\n"
"    { unsigned long long o2 = 0; lw_chacha20_sse2(cs, src + off, slen - off, dst + off, &o2); off += o2; }\n"
"#elif LW_HW_AVX2 == 1\n"
"    lw_chacha20_avx2(cs, src, slen, dst, &off);\n"
"    { unsigned long long o2 = 0; lw_chacha20_sse2(cs, src + off, slen - off, dst + off, &o2); off += o2; }\n"
"#else\n"
"    lw_chacha20_sse2(cs, src, slen, dst, &off);\n"
"#endif\n"
// ★ 꼬리는 **셈 판**이 판다 — 카운터 규약(2^32 에서 돈다)을 두 곳에 적지 않는다.
"    lw_cc_put32(ctr, cs[12]);\n"
"    if (off < slen) lw_chacha20(keyp, klen, ctrp, clen, src + off, slen - off, dst + off, dlen - off);\n"
"    return (long long)slen; }\n"
"#else\n"
"#define lw_chacha20_x lw_chacha20\n"
"#endif\n"
"static long long lw_rng_next(long long st) {\n"
"    unsigned long long z = (unsigned long long)st + 0x9e3779b97f4a7c15ull;\n"
"    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;\n"
"    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;\n"
"    z = z ^ (z >> 31);\n"
"    return (long long)z;\n"
"}\n"
"static long long lw_crc32(const unsigned char *p, size_t n) {\n"
"    unsigned int crc = 0xffffffffu;\n"
"    for (size_t i = 0; i < n; i++) {\n"
"        crc ^= (unsigned int)p[i];\n"
"        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320u & (unsigned int)-(int)(crc & 1u));\n"
"    }\n"
"    return (long long)(unsigned long long)(crc ^ 0xffffffffu);\n"
"}\n"
"static lowv lw_len(lowv s) { s = lw_thru(s); if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"len needs a slice\"); return lw_int((long long)s.n); }\n"
"static lowv lw_index(lowv s, lowv i) { s = lw_thru(s); if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"index needs a slice\");\n"
"    long long k = lw_want_int(i, \"index\"); if (k < 0 || (size_t)k >= s.n) lw_panic(\"slice index out of bounds\");\n"
"    if (s.tag == LWV_SLICE) return lw_int((long long)s.p[k]);\n"
"    if (s.i & 0x40000) { lowv v = {0}; v.tag = LWV_VIEW;\n"
"        v.p = s.p + (size_t)k * (size_t)s.box; v.n = (size_t)s.box;\n"
"        v.box = (int)((s.i >> 20) & 0xff); return v; }\n"
"    unsigned long long x = 0; const unsigned char *p = s.p + (size_t)k * (size_t)s.box;\n"
"    for (int b = s.box; b-- > 0; ) x = (x << 8) | p[b];\n"
"    if (s.i & 0x10000) return lw_flt(lw_b2f(x, s.box));\n"
"    long long r = (long long)x;\n"
"    if ((s.i & 0x20000) && s.box < 8) { unsigned long long sb = 1ull << (s.box*8 - 1);\n"
"        if (x & sb) r = (long long)(x | ~((1ull << (s.box*8)) - 1)); }\n"
"    return lw_int(r); }\n"
// ★★★ **배열 내용 술어 검사** (R5) — 모든 원소가 cmp N 인지 훑는다(진입 계약). 태그·빠른 판 공용.
"static long long lw_elemck_p(const unsigned char *p, size_t n, int esz, int cmp, long long N) {\n"
"    for (size_t i = 0; i < n; i++) {\n"
"        long long e; if (esz == 1) e = (long long)p[i];\n"
"        else { unsigned long long x = 0; const unsigned char *q = p + i * (size_t)esz;\n"
"               for (int b = esz; b-- > 0; ) x = (x << 8) | q[b]; e = (long long)x; }\n"
"        int ok = cmp==0 ? (e<N) : cmp==1 ? (e<=N) : cmp==2 ? (e>N) : (e>=N);\n"
"        if (!ok) return 0; }\n"
"    return 1; }\n"
"static long long lw_elemck(lowv s, lowv nv, int cmp) {\n"
"    if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"elem_* needs a slice\");\n"
"    int esz = (s.tag == LWV_SLICE) ? 1 : (s.box ? s.box : 8);\n"
"    return lw_elemck_p(s.p, s.n, esz, cmp, lw_want_int(nv, \"ints\")); }\n"
// ★ RFC-0055 D5: 경계가 **증명된** 인덱스는 검사 없이 읽는다 (i < len(s) ∧ i ≥ 0).
// ★ `idx_no_check` (Qed). VM 은 검사를 유지하고 자기 고발하지만, 네이티브는 실제로 제거한다 —
//   그것이 계약이 사 주는 성능이다.
"static void lw_istore_agg(lowv s, long long k, lowv v);\n"   /* ★ struct-array 원소 저장 — 정의는 struct 표 뒤(RFC-0080 아레나) */
"static void lw_istore_at(lowv s, long long k, lowv v) {\n"
"    s = lw_thru(s);\n"
"    if (s.tag == LWV_SLICE) { ((unsigned char *)s.p)[k] = (unsigned char)(lw_want_int(v, \"value\") & 0xff); return; }\n"
"    if (s.i & 0x40000) { lw_istore_agg(s, k, v); return; }\n"   /* ★ 원소가 구조체/변형 → 레코드를 인코딩 */
/* ★ 스칼라만 저장할 수 있다 — 구조체/집합 원소를 index-store 로 밀면 VM 은 거부한다(E-VM-TYPE).
   native 도 **같이 거부**해야 한다(전엔 struct 를 float 분기로 0 인코딩해 조용히 손상시켰다). */
"    if (v.tag != LWV_INT && v.tag != LWV_FLT) lw_panic(\"index-store needs a scalar value (a struct/aggregate element cannot be stored this way)\");\n"
"    /* typed array: encode one element (native = le) */\n"
"    unsigned long long x = (s.i & 0x10000) ? lw_f2b(v.tag == LWV_FLT ? lw_b2f((unsigned long long)v.i, 8)\n"
"                                                          : (double)v.i, s.box)\n"
"                               : (unsigned long long)lw_want_int(v, \"value\");\n"
"    unsigned char *p = (unsigned char *)s.p + (size_t)k * (size_t)s.box;\n"
"    for (int b = 0; b < s.box; b++) p[b] = (unsigned char)((x >> (8 * b)) & 0xff);\n"
"}\n"
/* ★★★ **맞바꾸기**(2026-07-26) — `index` 가 구조체 원소를 뷰로 주므로 읽은 두 값은 같은
   바이트를 가리킨다: `set` 두 번으로는 맞바꿀 수 없다(자기 자신을 덮는다). 제자리 정렬 같은
   알고리즘이 언어로 표현되려면 이 프리미티브가 있어야 한다. 할당 0·원소 타입 무관. */
"static void lw_swap(lowv s, lowv i, lowv j) {\n"
"    s = lw_thru(s);\n"
"    if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"swap needs a slice\");\n"
"    long long a = lw_want_int(i, \"index\"), b = lw_want_int(j, \"index\");\n"
"    if (a < 0 || b < 0 || (size_t)a >= s.n || (size_t)b >= s.n) lw_panic(\"swap index out of bounds\");\n"
"    size_t ez = (s.tag == LWV_VARRAY && s.box > 0) ? (size_t)s.box : 1;\n"
"    unsigned char *pa = (unsigned char *)s.p + (size_t)a * ez;\n"
"    unsigned char *pb = (unsigned char *)s.p + (size_t)b * ez;\n"
"    for (size_t z = 0; z < ez; z++) { unsigned char tv = pa[z]; pa[z] = pb[z]; pb[z] = tv; }\n"
"}\n"
"static void lw_istore(lowv s, lowv i, lowv v) {\n"
"    s = lw_thru(s);\n"
"    if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"set index needs a slice\");\n"
"    long long k = lw_want_int(i, \"index\");\n"
"    if (k < 0 || (size_t)k >= s.n) lw_panic(\"slice index out of bounds on write\");\n"
"    lw_istore_at(s, k, v);\n"
"}\n"
"static void lw_istore_nc(lowv s, lowv i, lowv v) {\n"
"    lw_istore_at(s, lw_want_int(i, \"index\"), v);\n"
"}\n"
"static lowv lw_index_nc(lowv s, lowv i) { s = lw_thru(s); long long k = lw_want_int(i, \"index\");\n"
"    if (s.tag == LWV_SLICE) return lw_int((long long)s.p[k]);\n"
"    if (s.i & 0x40000) { lowv v = {0}; v.tag = LWV_VIEW;\n"
"        v.p = s.p + (size_t)k * (size_t)s.box; v.n = (size_t)s.box;\n"
"        v.box = (int)((s.i >> 20) & 0xff); return v; }\n"
"    unsigned long long x = 0; const unsigned char *p = s.p + (size_t)k * (size_t)s.box;\n"
"    for (int b = s.box; b-- > 0; ) x = (x << 8) | p[b];\n"
"    if (s.i & 0x10000) return lw_flt(lw_b2f(x, s.box));\n"
"    long long r = (long long)x;\n"
"    if ((s.i & 0x20000) && s.box < 8) { unsigned long long sb = 1ull << (s.box*8 - 1);\n"
"        if (x & sb) r = (long long)(x | ~((1ull << (s.box*8)) - 1)); }\n"
"    return lw_int(r); }\n"
"static lowv lw_varray(lowv b, long long meta) {\n"
"    /* IDEMPOTENT: a typed slice passed from op to op must not be wrapped twice. */\n"
"    if (b.tag == LWV_VARRAY) return b;\n"
"    if (b.tag != LWV_SLICE) lw_panic(\"view_array needs a byte slice\");\n"
"    int esz = (int)(meta & 0xff);\n"
"    if (b.n % (size_t)esz) lw_panic(\"view_array: slice length is not a multiple of the element size\");\n"
"    lowv v = {0}; v.tag = LWV_VARRAY; v.p = b.p; v.n = b.n / (size_t)esz; v.box = esz;\n"
"    /* a STRUCT element type: carry it, so index gives a struct VIEW (zero-copy), not an int */\n"
"    /* else carry FLT (0x10000) and SIGN (0x20000) bits so index decodes/sign-extends right */\n"
"    v.i = (meta & 0x40000) ? (0x40000 | (((meta >> 20) & 0xff) << 20)) : ((meta & 0x10000) | (meta & 0x20000));\n"
"    return v; }\n"
/* ★ 타입 있는 배열(LWV_VARRAY)도 자른다 — VM 과 같은 규칙: .n 은 원소 수, .box 는 원소 크기 */
"static lowv lw_subslice(lowv s, lowv lo, lowv hi) { s = lw_thru(s);\n"
"    if (s.tag != LWV_SLICE && s.tag != LWV_VARRAY) lw_panic(\"subslice needs a slice\");\n"
"    long long a = lw_want_int(lo, \"lo\"), b = lw_want_int(hi, \"hi\");\n"
"    if (a < 0 || b < a || (size_t)b > s.n) lw_panic(\"subslice out of bounds\");\n"
"    lowv r = {0}; r.tag = s.tag; r.i = s.i; r.box = s.box;\n"
"    r.p = s.p + (size_t)a * (s.tag == LWV_VARRAY ? (size_t)s.box : 1);\n"
"    r.n = (size_t)(b - a); return r; }\n"
"typedef struct { int mk; int nf; lowv f[8]; } lowrec;\n"
// ★ 태그 경로의 레코드 풀. **VM 과 같은 크기·같은 규율**이어야 한다 — 아니면 둘이 갈리고,
//   차등 스윕이 그것을 (정당하게) 컴파일러 버그로 고발한다.
"static lowrec lw_recs[LW_RECPOOL];\n"
"static _Thread_local int lw_nrec; static _Thread_local int lw_reclim = LW_RECPOOL;\n"
// ★★★ **bounded mailbox** — 레코드(=actor 인스턴스)별 in-flight 메시지 수. VM 의 mbox_depth 와
//   **같은 규율**: 핸들러 진입 +1, 반환 −1, `mailbox bounded N` 초과 시 트랩. 순차(재진입) 배달.
"static unsigned short lw_mbox[LW_RECPOOL];\n"
// ★★★ **restart 슈퍼비전** (RFC-0009 D4, B3 재시도→escalate · 자기 상태만 초기화). actor 핸들러를
//   fault 경계로 감싸 부른다: 핸들러가 `panic` 하면 그 인스턴스의 **상태(레코드 필드)를 0 으로**
//   초기화하고 **다시 실행**한다(maxr 번까지). 소진하면 escalate(바깥 경계로 전파·프로세스 중단).
//   box < 0(진짜 인스턴스가 아님)이면 초기화 대상이 없어 즉시 escalate — VM 과 같은 규율.
"static lowv lw_restart_call(lowv (*fn)(const lowv *restrict), const lowv *args, int box, int maxr) {\n"
"    int fb = lw_nfault; if (fb >= 64) return fn(args);   /* 경계 소진 — 그냥 부른다 */\n"
"    int att = 0;\n"
"    for (;;) {\n"
"        lw_nfault = fb + 1;\n"
"        if (setjmp(lw_fault[fb]) != 0) {                  /* 핸들러가 panic 해서 되돌아왔다 */\n"
"            lw_nfault = fb;\n"
"            if (box >= 0 && att < maxr) { att++;\n"
"                lowrec *r = &lw_recs[box]; for (int i = 0; i < r->nf; i++) { lowv z = {0}; r->f[i] = z; }\n"
"                continue; }\n"
"            lw_upanic(lw_upanic_msg);                      /* escalate */\n"
"        }\n"
"        lowv result = fn(args); lw_nfault = fb; return result;\n"
"    }\n}\n"
;
// ★★★ **feature→link** (RFC-0013 §11.3 · P1 pay-as-you-go) — 아래 동시성 런타임(코루틴 스케줄러·채널·
//   await)은 프로그램이 동시성 op 을 **쓸 때만** 방출한다. 안 쓰면 0바이트다 — 그리고 임베디드엔
//   `ucontext` 가 없으니 **include 도 안 낸다**(안 그러면 bare metal 에서 컴파일조차 안 된다).
// ★★★ **A2 — reactor: 형제를 굶기지 않는다** (RFC-0071 A2).
//
//   ☞ A1 까지 `r_read` 는 **블로킹 syscall** 이었다. 그린스레드 하나가 그것을 부르면
//     **형제 태스크 전부가 프로세스와 함께 커널에 갇혔다.**
// ★★★★★ **프리스탠딩 협력 스케줄러 — T0 층** (X-0027 ⓑ · WO-0206 · RFC-0039 T0, 2026-09-13).
//
//   지금까지 `--target cortex_m` 은 액터·태스크·채널을 쓰는 프로그램에도 **호스트용 런타임**
//   (`LW_CONC` — ucontext 그린스레드 · pthread 워커 · epoll/aio)을 그대로 냈다. 컴파일러는
//   조용히 성공했고(rc=0), 그 C 는 **컴파일 단계에서 깨졌다**(`realloc` 암시적 선언).
//   ☞ *못 내는 것과 안 내는 것은 다르다. 안 내면서 아무 말도 안 하는 것이 가장 나쁘다.*
//
//   ★ **T0 는 «끝까지 돌린다»(run-to-completion)**. 베어메탈에는 `ucontext` 가 없어 도는 태스크를
//     중간에서 멈춰 둘 수 없다. 그래서 T0 는 VM 의 **스케줄러 밖 갈래**를 그대로 따른다:
//     막히는 자리(await · 빈 채널 recv · 찬 채널 send)에서 **대기 중인 태스크를 돌려 진전을 만들고**,
//     돌릴 것이 하나도 없으면 «진전할 수 없다» 고 **말하며** 멈춘다(VM 의 E-VM-AWAIT 와 같은 규율).
//     그것이 협력 floor 의 정직한 한계다 — 선점도, 스택 교체도 없다(RFC-0039: *no-preempt·no-heap*).
//
//   ★ **크기는 작게, 그리고 조절 가능하게.** 호스트판은 우편함 4096 칸에 인자를 `LW_MAXP`(32) 칸씩
//     들어 3 MB 가 넘는다 — 그 수를 베어메탈에 그대로 가져오면 이 층의 존재 이유가 사라진다.
//     여기서는 방출기가 **그 프로그램이 실제로 쓰는 최대 인자 수**를 적어 주고(`LW_T0_ARGS`),
//     표 크기는 작은 기본값에 `-D` 로 갈아끼울 수 있다.
const char LW_CONC_T0[] =
"#define LW_HAS_GSCHED 0\n"
"#ifndef LW_T0_MBOX\n#define LW_T0_MBOX 32\n#endif\n"          /* 대기 중인 메시지·태스크 */
"#ifndef LW_T0_JOBS\n#define LW_T0_JOBS 32\n#endif\n"          /* await 핸들 */
"#ifndef LW_T0_CHAN\n#define LW_T0_CHAN 8\n#endif\n"           /* 채널 수 */
"#ifndef LW_T0_CHCAP\n#define LW_T0_CHCAP 16\n#endif\n"        /* 채널 한 개의 칸 수 */
"static struct { int box; lowv (*fn)(const lowv *restrict); unsigned char nargs; int job; int peer;"
" lowv args[LW_T0_ARGS]; } lw_ambox[LW_T0_MBOX];\n"
"static int lw_nambox;\n"
"static struct { int done; lowv result; } lw_jobs[LW_T0_JOBS]; static int lw_njobs;\n"
"static struct { lowv buf[LW_T0_CHCAP]; int head, tail, count; } lw_chans[LW_T0_CHAN]; static int lw_nchan;\n"
"static int lw_cancel_scope;\n"
// ★ 큐에서 하나 빼서 **끝까지** 돌린다. 뺀 뒤에 부른다(재진입 send 가 뒤에 붙는다 — FIFO 유지).
//   돌린 것이 있으면 1, 큐가 비었으면 0. «진전이 있었는가» 를 이 반환값 하나로 말한다.
"static int lw_t0_step(void) {\n"
"    if (lw_nambox <= 0) return 0;\n"
"    int i = 0;\n"
"    int box = lw_ambox[i].box, job = lw_ambox[i].job, na = lw_ambox[i].nargs;\n"
"    lowv (*fn)(const lowv *restrict) = lw_ambox[i].fn;\n"
"    lowv cargs[LW_T0_ARGS + 1];\n"
"    int off = 0;\n"
"    if (box >= 0) { cargs[0] = (lowv){0}; cargs[0].tag = LWV_REC; cargs[0].box = box; off = 1; }\n"
"    for (int g = 0; g < na && g + off <= LW_T0_ARGS; g++) cargs[g + off] = lw_ambox[i].args[g];\n"
"    for (int m = i; m + 1 < lw_nambox; m++) lw_ambox[m] = lw_ambox[m + 1];\n"
"    lw_nambox--;\n"
"    lowv r = fn(cargs);\n"
"    if (job >= 0 && job < lw_njobs) { lw_jobs[job].result = r; lw_jobs[job].done = 1; }\n"
"    return 1;\n"
"}\n"
// ★ `drain <instance>` — 그 인스턴스 앞으로 온 메시지만 FIFO 로 비운다(호스트판과 같은 규율).
"static void lw_drain(int box) {\n"
"    for (int i = 0; i < lw_nambox; ) {\n"
"        if (lw_ambox[i].box != box) { i++; continue; }\n"
"        int na = lw_ambox[i].nargs;\n"
"        lowv cargs[LW_T0_ARGS + 1]; cargs[0] = (lowv){0}; cargs[0].tag = LWV_REC; cargs[0].box = box;\n"
"        for (int g = 0; g < na && g + 1 <= LW_T0_ARGS; g++) cargs[g + 1] = lw_ambox[i].args[g];\n"
"        lowv (*fn)(const lowv *restrict) = lw_ambox[i].fn;\n"
"        for (int m = i; m + 1 < lw_nambox; m++) lw_ambox[m] = lw_ambox[m + 1];\n"
"        lw_nambox--;\n"
"        (void)fn(cargs);\n"
"    }\n}\n"
// ★ `schedule .` — 큐가 빌 때까지 돌린다(quiescence). 선점이 없으므로 이것이 전부다.
"static void lw_schedule(void) { while (lw_t0_step()) ; }\n"
// ★ `yield` — 양보할 코루틴이 없다. 협력 floor 에서 양보의 뜻은 **남의 일을 하나 해 주는 것**이다.
"static void lw_gyield(void) { (void)lw_t0_step(); }\n"
"static int lw_chnew(void) { if (lw_nchan >= LW_T0_CHAN) lw_panic(\"channel pool exhausted (T0 floor: LW_T0_CHAN channels — raise it with -DLW_T0_CHAN=N)\");\n"
"    int ci = lw_nchan++; lw_chans[ci].head = lw_chans[ci].tail = lw_chans[ci].count = 0; return ci; }\n"
"static lowv lw_chsend(int ci, lowv v) {\n"
"    if (ci < 0 || ci >= lw_nchan) lw_panic(\"`chsend` needs a channel handle (from `channel`)\");\n"
"    while (lw_chans[ci].count >= LW_T0_CHCAP)\n"
"        if (!lw_t0_step()) lw_panic(\"`chsend` on a full channel and nothing left to run (T0 floor: tasks run to completion, so a sender cannot be suspended to let a receiver drain the channel)\");\n"
"    lw_chans[ci].buf[lw_chans[ci].tail] = v; lw_chans[ci].tail = (lw_chans[ci].tail + 1) % LW_T0_CHCAP; lw_chans[ci].count++;\n"
"    lowv u = {0}; u.tag = LWV_INT; return u; }\n"
"static lowv lw_chrecv(int ci) {\n"
"    if (ci < 0 || ci >= lw_nchan) lw_panic(\"`chrecv` needs a channel handle (from `channel`)\");\n"
"    while (lw_chans[ci].count == 0)\n"
"        if (!lw_t0_step()) lw_panic(\"`chrecv` on an empty channel and nothing left to run (T0 floor: no sender can make progress from here)\");\n"
"    lowv v = lw_chans[ci].buf[lw_chans[ci].head]; lw_chans[ci].head = (lw_chans[ci].head + 1) % LW_T0_CHCAP; lw_chans[ci].count--;\n"
"    return v; }\n"
"static lowv lw_await(int jid) {\n"
"    if (jid < 0 || jid >= lw_njobs) lw_panic(\"`await` needs a job handle (from `spawn <op>`)\");\n"
"    while (!lw_jobs[jid].done)\n"
"        if (!lw_t0_step()) lw_panic(\"the awaited task never completed (it is blocked with no way to make progress)\");\n"
"    return lw_jobs[jid].result; }\n"
;

const char LW_CONC[] =
"#define LW_HAS_GSCHED 1\n"
"#include <ucontext.h>\n"
"#include <sys/epoll.h>\n#include <poll.h>\n#include <fcntl.h>\n#include <errno.h>\n#include <unistd.h>\n#include <aio.h>\n"   /* green thread + channel — 코루틴 스케줄러(블로킹 recv 를 진짜로 중단) */
// ★★★ **async 메일박스** (RFC-0009 — call/cast 분리). `spawn send` 가 여기 넣고 `drain` 이 FIFO 로
//   비운다. 전역 대기열(도착 순서 = 삽입 순서). VM 과 **같은 규율**(차등 스윕이 그것을 지킨다).
"static struct { int box; lowv (*fn)(const lowv *restrict); unsigned char nargs; int job; int peer; lowv args[LW_MAXP]; } lw_ambox[4096];\n"
"static int lw_nambox;\n"
"static struct { _Atomic int done; lowv result; } lw_jobs[4096]; static int lw_njobs;\n"   /* await — job 결과 */
"static void lw_drain(int box) {\n"
"    for (int i = 0; i < lw_nambox; ) {\n"
"        if (lw_ambox[i].box != box) { i++; continue; }\n"
"        lowv cargs[LW_MAXP + 1]; cargs[0] = (lowv){0}; cargs[0].tag = LWV_REC; cargs[0].box = box;\n"
"        for (int g = 0; g < lw_ambox[i].nargs; g++) cargs[g + 1] = lw_ambox[i].args[g];\n"
"        lowv (*fn)(const lowv *restrict) = lw_ambox[i].fn;\n"
"        for (int m = i; m + 1 < lw_nambox; m++) lw_ambox[m] = lw_ambox[m + 1];\n"   /* 큐에서 뺀 뒤 실행(재진입 drain 대비) */
"        lw_nambox--;\n"
"        (void)fn(cargs);\n"
"    }\n}\n"
// ★★★ **green thread + channel — 코루틴 스케줄러** (RFC-0009 2/3·3/3). `schedule .`/task_group end 가
//   부른다. 각 태스크·메시지가 자기 ucontext 스택에서 돌아 `yield`·블로킹 채널 op 로 **중단**할 수
//   있고, 스케줄러가 round-robin 으로 재개한다(진행 없으면 deadlock). VM(vm_gsched)과 **같은 규율**:
//   네이티브는 오라클이 없으니 한 스케줄(round-robin)만 돌고, VM 오라클이 인터리빙을 탐색한다.
//   차등 스윕은 오라클이 순서 무관을 증명한 프로그램에서 VM≡native 를 지킨다.
"static struct { lowv buf[64]; int head, tail, count; } lw_chans[64]; static int lw_nchan;\n"
// ★★★ **그린스레드 상한 — VM(VM_GTHR)과 같은 32로 통일** (RFC-0072, 2026-07-21).
//   ☞ 전엔 16 이 코드에 흩어져 있었고, **끝난 자리를 리필 때 안 재활용**하면 동시에 살아있는
//     태스크가 16 을 넘을 때 조용히 데드락으로 오진됐다(실측: consumer 를 절반 넘게 먼저 spawn).
//     VM 과 네이티브가 **같은 지점에서 거절**해야 오라클이 성립한다 ⇒ 둘 다 32.
"#define LW_GTHR 16\n"        /* 초기 용량 — 상한이 아니다(가득 차면 성장. E-Alloc P4) */
"#define LW_GT_STK 131072\n"  /* 그린스레드 ucontext 스택 크기 */
// ★★★ **동적 그린스레드 — 워커별로 성장한다**(E-Alloc P4). 전엔 lw_gthr/lw_gt_stk 가 LW_GTHR
//   고정 배열이라 동시에 16개 넘게 막히면 lw_panic 했다. 이제 포인터 + 용량으로 두고 **가득 차면
//   realloc/malloc 으로 키운다**. 전부 _Thread_local 이라 워커끼리 안 섞이고 워커별 풀이 자연스럽다.
//   ☞ 스택은 코루틴의 실행 스택(C 스택과 같은 범주) — pthread 가 이미 워커 OS 스택을 잡는 것과
//     같은 종류의 할당이지, 언어가 세는 데이터 힙이 아니다(RFC-0043 위반 아님).
"static struct { ucontext_t ctx; int box; lowv (*fn)(const lowv *restrict); unsigned char nargs;\n"
"                lowv args[LW_MAXP]; int state; int blk_chan; int blk_recv; int job; int blk_job; int blk_fd; int blk_aio; int peer; } _Thread_local *lw_gthr; static _Thread_local int lw_ngthr, lw_gthr_cap;\n"
"static _Thread_local char **lw_gt_stk; static _Thread_local int *lw_ready;\n"
"static int lw_gthr_ensure(int need) {\n"
"    if (need <= lw_gthr_cap) return 1;\n"
"    int nc = lw_gthr_cap ? lw_gthr_cap : LW_GTHR;\n"
"    while (nc < need) nc *= 2;\n"
"    void *ng = realloc(lw_gthr, (size_t)nc * sizeof *lw_gthr);\n"
"    void *nr = realloc(lw_ready, (size_t)nc * sizeof *lw_ready);\n"
"    char **ns = realloc(lw_gt_stk, (size_t)nc * sizeof *lw_gt_stk);\n"
"    if (!ng || !nr || !ns) lw_panic(\"out of memory growing the green-thread pool\");\n"
"    lw_gthr = ng; lw_ready = nr; lw_gt_stk = ns;\n"
"    memset((char *)lw_gthr + (size_t)lw_gthr_cap * sizeof *lw_gthr, 0, (size_t)(nc - lw_gthr_cap) * sizeof *lw_gthr);\n"
"    for (int i = lw_gthr_cap; i < nc; i++) { lw_gt_stk[i] = malloc(LW_GT_STK); if (!lw_gt_stk[i]) lw_panic(\"out of memory allocating a green-thread stack\"); }\n"
"    lw_gthr_cap = nc;\n"
"    return 1;\n"
"}\n"
// ★★★ **lw_in_gsched 는 스레드 지역이다** (RFC-0072 E2, 2026-07-21).
//   ☞ 전역이면 스케줄러가 세운 `1` 을 **워커 스레드가 물려받아** io 대기에서 그린스레드
//     swapcontext 로 가서 **죽는다**(워커엔 그린스레드 문맥이 없다). 실측: segfault.
//   ⇒ 워커 스레드는 `lw_in_gsched=0` 이어야 IO 를 **블로킹 poll** 로 처리한다(그게 맞다 —
//     워커가 막히면 다른 워커가 큐를 돈다). lw_cur_gthr 도 같은 이유로 스레드 지역.
"static _Thread_local ucontext_t lw_sched_ctx; static _Thread_local int lw_cur_gthr; static _Thread_local int lw_in_gsched; static _Thread_local int lw_cancel_scope; static _Atomic unsigned long long lw_gprog;\n"
// ★ 스케줄러가 reactor 를 부르고 reactor 가 스케줄러 상태를 본다 — 전방 선언으로 잇는다.
"static int lw_io_poll_once(void);\n"
// ★ 워커 풀이 코어 수를 쓴다 — 동시성 런타임에 정의를 **항상** 둔다(가드로 중복 방지).
"#ifndef LW_HAS_NCPU\n#define LW_HAS_NCPU 1\n"
"static int lw_ncpu(void) { long n = sysconf(_SC_NPROCESSORS_ONLN); if (n < 1) n = 1; if (n > 64) n = 64; return (int)n; }\n"
"#endif\n"
// ★ 취소가 io 대기 fd 를 reactor 에서 **지워야** 하므로 취소 경로도 reactor 를 본다.
"static void lw_io_forget(int fd);\n"
"static void lw_io_wait_aio(int s);\n"
"static void lw_aio_drop(int s);\n"
"static void lw_gt_tramp(void) {\n"
"    int gi = lw_cur_gthr; lowv cargs[LW_MAXP + 1]; int base = 0;\n"
"    if (lw_gthr[gi].box >= 0) { cargs[0] = (lowv){0}; cargs[0].tag = LWV_REC; cargs[0].box = lw_gthr[gi].box; base = 1; }\n"
"    for (int g = 0; g < lw_gthr[gi].nargs; g++) cargs[base + g] = lw_gthr[gi].args[g];\n"
"    lowv rv = lw_gthr[gi].fn(cargs);\n"
"    lw_gthr[gi].state = 4;\n"
"    if (lw_gthr[gi].job >= 0) { int jid = lw_gthr[gi].job; lw_jobs[jid].result = rv; atomic_store_explicit(&lw_jobs[jid].done, 1, memory_order_release); lw_gprog++;\n"
"        for (int q = 0; q < lw_ngthr; q++) if (lw_gthr[q].state == 6 && lw_gthr[q].blk_job == jid) lw_gthr[q].state = 2; }\n"
"    if (lw_cancel_scope && rv.tag == LWV_ERR) {\n"   /* cancel_on_error — 오류 자식이 형제를 취소한다 */
// ★★★ **A4 — 취소가 io 대기에도 닿아야 한다** (RFC-0071 A4, 2026-07-20).
//   ☞ 여기 **상태 7 이 빠져 있었다**(1·5·6 만 훑었다). A2 가 상태 7(io 대기)을 새로 만들면서
//     이 목록을 안 고쳤고, 그래서 **형제가 실패해도 io 로 막힌 태스크는 안 죽었다** —
//     실측: `FAIL` 만 찍고 프로그램이 **영영 안 끝났다.**
//   ★★ 새 상태를 만들면 **그 상태를 훑는 모든 자리**를 다시 봐야 한다. 오늘 오라클에서
//     겪은 것과 같은 부류다(새 능력이 늘 때 "이 인자를 만들 수 있는가" 를 다시 묻기).
//   ★ 취소할 때 **fd 등록도 지운다** — 안 지우면 죽은 태스크의 fd 가 reactor 에 남아
//     다음 `epoll_wait` 가 그것 때문에 깨어난다(유령 깨움).
"        for (int q = 0; q < lw_ngthr; q++) { if (q == gi) continue;\n"
"            if (lw_gthr[q].state == 1 || lw_gthr[q].state == 5 || lw_gthr[q].state == 6 || lw_gthr[q].state == 7) {\n"
"                if (lw_gthr[q].state == 7) { lw_io_forget(lw_gthr[q].blk_fd); lw_aio_drop(lw_gthr[q].blk_aio); }\n"
"                lw_gthr[q].state = 4;\n"
"                if (lw_gthr[q].job >= 0) { int cj = lw_gthr[q].job; lw_jobs[cj].result = rv; atomic_store_explicit(&lw_jobs[cj].done, 1, memory_order_release);\n"
"                    for (int w = 0; w < lw_ngthr; w++) if (lw_gthr[w].state == 6 && lw_gthr[w].blk_job == cj) lw_gthr[w].state = 2; } } } }\n"
"}\n"
// ★★★ **진짜 OS 스레드 — 동료가 필요 없는 태스크만** (RFC-0071 A7).
//
//   ★★★ **A5 가 그 조건을 이미 준다.** `concurrent`(채널 op)는 *"동료가 돌아야 완결된다"* 이므로
//     **협조적 스케줄러 안에 있어야** 한다. 그것이 **없는** 태스크는 커널·계산만 기다리므로
//     RFC-0022 D-A 의 말대로 *"**어떤 executor 로도** 만족된다"* — **자기 OS 스레드**를 포함해서.
//   ⇒ 효과 구분이 곧 **스레드 자격**이다. 새 판정 기계가 0 이다.
//
//   ☞ 그리고 ucontext 를 스레드 사이로 **옮기지 않는다**(그것은 미정의다) — 진짜 스레드로 가는
//     태스크는 **처음부터 ucontext 를 안 쓴다**. 협조적 태스크만 그린스레드로 남는다.
"typedef struct { lowv (*fn)(const lowv *restrict); lowv a[LW_MAXP]; unsigned char np; int job; int bb, bl, rb, rl; lowv r; } lw_thr_t;\n"
"static void *lw_thr_run(void *v) {\n"
"    lw_thr_t *T = (lw_thr_t *)v;\n"
"    lw_nbox = T->bb; lw_boxlim = T->bl; lw_nrec = T->rb; lw_reclim = T->rl;\n"   /* ★ 서로소 구간 */
"    lw_vec_shared = 1;\n"   /* ★ 벡터 풀은 안 나눠져 있다 — 이 스레드에서는 되감지 않는다 */
"    T->r = T->fn(T->a);\n"
"    return (void *)0;\n"
"}\n"
// ★★★ **E1 — 고정 워커 풀 + 공유 큐** (RFC-0072 E1, 2026-07-21).
//
//   ☞ A7 은 태스크마다 스레드 하나(≤16)를 만들고 join 했다 — 태스크가 코어보다 많으면
//     스레드가 폭증하고, 16 을 넘으면 한 배치만 병렬이었다.
//   ⇒ E1: 워커를 **코어 수만큼 고정**하고, 자격 태스크를 **공유 큐**(원자 카운터)에서 뽑아
//     돌린다. 태스크가 수백이어도 스레드 = 워커 수다.
//
//   ★★★ **핵심: 값 풀 구간은 워커별이고, 커서는 태스크 사이에 *안 리셋*한다.**
//     워커 하나가 여러 태스크를 이어 돌린다 — 리셋하면 앞 태스크가 낸 박스(결과로 나간
//     인덱스)를 뒤 태스크가 덮는다. 그래서 워커의 커서는 자기 구간 안에서 **단조 증가**한다.
//     구간이 서로소이므로(A7 의 논증) 잠금이 필요 없고, join 이 메모리 장벽이다.
"typedef struct { const struct lw_job_desc *jobs; int n; _Atomic int next; } lw_wq;\n"
"struct lw_job_desc { lowv (*fn)(const lowv *restrict); lowv a[LW_MAXP]; unsigned char np; int job; };\n"
"typedef struct { lw_wq *q; int bb, bl, rb, rl; } lw_worker_arg;\n"
// ★★★ **E2 — 워커는 그린스레드 스케줄러다** (RFC-0072 E2, 2026-07-21).
//
//   ☞ E1 의 워커는 태스크를 **끝까지 bare 실행**했다 — 태스크가 IO 로 막히면 그 워커가
//     놀았다(워커 1개면 형제가 굶었다). E2: 워커가 **자기 그린스레드 스케줄러**를 돌린다.
//     공유 큐에서 태스크를 뽑아 그린스레드로 만들고, **다 막히면 큐에서 더 뽑아** 추가한다.
//     ⇒ 워커 1개여도 IO 로 막힌 태스크가 양보하고 형제가 진행한다(Go netpoller 모델).
//   ★★ 그린스레드 상태(lw_gthr·sched_ctx·gt_stk·epfd·aio…)는 전부 **_Thread_local** 이라
//     워커끼리 안 섞인다. 채널·await 태스크는 peer=1 이라 여기 안 온다(불변식 A) — 그래서
//     워커 경계 너머 채널 wake 는 **구조적으로 불가능**하다.
"static void *lw_worker_run(void *v) {\n"
"    lw_worker_arg *W = (lw_worker_arg *)v;\n"
"    lw_nbox = W->bb; lw_boxlim = W->bl; lw_nrec = W->rb; lw_reclim = W->rl;\n"
"    lw_vec_shared = 1;\n"   /* ★ 벡터 풀은 안 나눠져 있다 — 이 스레드에서는 되감지 않는다 */
"    lw_in_gsched = 1; lw_vec_shared = 1; unsigned long long rr = 0;\n"
"    lw_gthr_ensure(LW_GTHR);\n"                                                  /* 이 워커의 그린스레드 풀 초기 확보(성장 가능) */
"    for (;;) {\n"
// ★ 리필: **끝난 그린스레드 자리(state 4)를 먼저 재활용**하고, 없으면 새 자리를 쓴다.
//   ☞ 큐에서 계속 뽑으므로 끝난 자리를 안 치우면 lw_ngthr 이 16 에 붙어 무한 루프가 된다
//     (실측: 워커 1개로 태스크 스물이 매달렸다). 협조적 루프는 한 배치만 돌아 이 문제가 없었다.
"        for (;;) {\n"
"            int gi = -1;\n"
"            if (lw_ngthr < lw_gthr_cap) gi = lw_ngthr++;\n"                       /* 게으른 재활용: 상한 전엔 새 자리 */
"            else { for (int q = 0; q < lw_ngthr; q++) if (lw_gthr[q].state == 4) { gi = q; break; }\n"
"                   if (gi < 0) { lw_gthr_ensure(lw_ngthr + 1); gi = lw_ngthr++; } }\n"  /* 가득+다 바쁨 → 성장(상한 없음, E-Alloc P4) */
"            if (gi < 0) break;\n"
"            int i = atomic_fetch_add_explicit(&W->q->next, 1, memory_order_relaxed);\n"
"            if (i >= W->q->n) { if (gi == lw_ngthr - 1 && lw_gthr[gi].state != 4) lw_ngthr--; break; }\n"
"            const struct lw_job_desc *d = &W->q->jobs[i];\n"
"            lw_gthr[gi].box = -1; lw_gthr[gi].fn = d->fn; lw_gthr[gi].nargs = d->np; lw_gthr[gi].job = d->job;\n"
"            for (int x = 0; x < d->np; x++) lw_gthr[gi].args[x] = d->a[x];\n"
"            lw_gthr[gi].state = 1;\n"
"        }\n"
"        int *ready = lw_ready, nready = 0;\n"
"        for (int i = 0; i < lw_ngthr; i++) if (lw_gthr[i].state == 1 || lw_gthr[i].state == 2) ready[nready++] = i;\n"
"        if (nready == 0) {\n"
"            if (lw_io_poll_once()) continue;\n"            /* IO 로 막힌 것 깨우기 */
// ★ 준비된 것도 IO 대기도 없다: 큐가 아직 남았으면 리필하러 위로, 다 비었으면 이 워커 끝.
"            int more = atomic_load_explicit(&W->q->next, memory_order_relaxed) < W->q->n;\n"
"            if (more) continue;\n"
"            break;\n"
"        }\n"
"        int gi = ready[rr % (unsigned)nready]; rr++; lw_cur_gthr = gi;\n"
"        if (lw_gthr[gi].state == 1) {\n"
"            getcontext(&lw_gthr[gi].ctx);\n"
"            lw_gthr[gi].ctx.uc_stack.ss_sp = lw_gt_stk[gi];\n"
"            lw_gthr[gi].ctx.uc_stack.ss_size = LW_GT_STK;\n"
"            lw_gthr[gi].ctx.uc_link = &lw_sched_ctx;\n"
"            lw_gthr[gi].state = 3;\n"
"            makecontext(&lw_gthr[gi].ctx, lw_gt_tramp, 0);\n"
"            swapcontext(&lw_sched_ctx, &lw_gthr[gi].ctx);\n"
"        } else { lw_gthr[gi].state = 3; swapcontext(&lw_sched_ctx, &lw_gthr[gi].ctx); }\n"
"    }\n"
"    lw_ngthr = 0;\n"
"    return (void *)0;\n"
"}\n"
"static void lw_schedule(void) {\n"
"    int prev = lw_in_gsched; lw_in_gsched = 1; int vprev_ = lw_vec_shared; lw_vec_shared = 1; unsigned long long rr = 0;\n"
"    /* \xe2\x98\x85\xe2\x98\x85\xe2\x98\x85 \xec\x9e\xac\xec\xa7\x84\xec\x9e\x85: \xec\xa4\x91\xec\xb2\xa9 task_group \xec\x9d\x84 \xea\xb7\xb8\xeb\xa6\xb0\xec\x8a\xa4\xeb\xa0\x88\xeb\x93\x9c \xec\x95\x88\xec\x97\x90\xec\x84\x9c \xec\x97\xb4\xeb\xa9\xb4 lw_schedule \xec\x9d\xb4 \xec\x9e\x90\xea\xb8\xb0\xeb\xa5\xbc\n"
"       \xeb\x8b\xa4\xec\x8b\x9c \xeb\xb6\x80\xeb\xa5\xb8\xeb\x8b\xa4. lw_sched_ctx(\xeb\xb0\x94\xea\xb9\xa5 \xec\x9e\xac\xea\xb0\x9c\xec\xa0\x90)\xea\xb3\xbc \xea\xb7\xb8\xeb\xa6\xb0\xec\x8a\xa4\xeb\xa0\x88\xeb\x93\x9c \xed\x91\x9c\xeb\xa5\xbc \xeb\x8d\xae\xec\x96\xb4 \xeb\xb0\x94\xea\xb9\xa5 \xec\x8a\xa4\xec\xbc\x80\xec\xa4\x84\xeb\x9f\xac\xea\xb0\x80 \xec\xa3\xbd\xeb\x8a\x94\xeb\x8b\xa4.\n"
"       \xe2\x87\x92 \xeb\xb0\x94\xea\xb9\xa5 \xec\x83\x81\xed\x83\x9c\xeb\xa5\xbc \xec\xa0\x80\xec\x9e\xa5\xed\x95\x98\xea\xb3\xa0, \xec\xa4\x91\xec\xb2\xa9 \xec\x8a\xa4\xec\xbc\x80\xec\xa4\x84\xeb\x9f\xac\xeb\x8a\x94 base \xec\x9c\x84 \xec\x8a\xac\xeb\xa1\xaf\xeb\xa7\x8c \xeb\x8b\xa4\xeb\xa3\xac\xeb\x8b\xa4. */\n"
"    ucontext_t lw_saved_ctx = lw_sched_ctx; int lw_saved_cur = lw_cur_gthr; int base = lw_ngthr;\n"
// ★ 먼저 **동료가 필요 없는** 것들을 걷어 진짜 스레드로 돌린다. 남은 것만 협조적으로 간다.
"    if (getenv(\"LOWENT_THREADS\")) {\n"
"      pthread_t th_[16]; lw_thr_t ts_[16]; int nt_ = 0;\n"
// ★★★ **`cancel_on_error` 그룹은 진짜 스레드로 안 보낸다** (2026-07-20 실측이 잡았다).
//   돌고 있는 pthread 를 **안전하게 취소할 수 없다** — 협조적 스케줄러는 태스크가 양보한
//   자리에서 죽이지만, 진짜 스레드는 아무 데서나 멈출 수 없다(pthread_cancel 은 자원을
//   어중간하게 남긴다). ⇒ 실측: `drive_cancel` 이 **0 대신 1** 을 냈다.
//   ★★ *"어느 실행 방식을 골라도 답이 같다"* 를 지키는 길은 **다르게 굴 수 있는 것을 안 보내는
//     것**이다. 취소가 필요한 그룹은 협조적으로 남는다 — reactor 백엔드에서 배운 것과 같다.
"      if (lw_cancel_scope) { lw_in_gsched = 1; goto coop_; }\n"
// ★★★ **자격 태스크를 공유 큐로 모은다** — peer 필요 없고 인스턴스 메시지도 아닌 것.
"      static struct lw_job_desc jd_[4096]; int nj_ = 0;\n"
"      for (int m = 0; m < lw_nambox && nj_ < 4096; ) {\n"
"        if (lw_ambox[m].peer || lw_ambox[m].box >= 0) { m++; continue; }\n"
"        jd_[nj_].fn = lw_ambox[m].fn; jd_[nj_].np = lw_ambox[m].nargs; jd_[nj_].job = lw_ambox[m].job;\n"
"        for (int x = 0; x < lw_ambox[m].nargs; x++) jd_[nj_].a[x] = lw_ambox[m].args[x];\n"
"        for (int q = m; q + 1 < lw_nambox; q++) lw_ambox[q] = lw_ambox[q + 1];\n"
"        lw_nambox--; nj_++;\n"
"      }\n"
"      if (nj_ > 0) {\n"
// ★ 워커 수 = min(코어, LOWENT_WORKERS, 태스크 수). 큐보다 많은 워커는 낭비다.
"        int W_ = lw_ncpu(); const char *we_ = getenv(\"LOWENT_WORKERS\");\n"
"        if (we_) { int w = atoi(we_); if (w >= 1) W_ = w; }\n"
"        if (W_ > nj_) W_ = nj_; if (W_ < 1) W_ = 1; if (W_ > 16) W_ = 16;\n"
"        lw_wq q_; q_.jobs = jd_; q_.n = nj_; atomic_store_explicit(&q_.next, 0, memory_order_relaxed);\n"
"        pthread_t wth_[16]; lw_worker_arg wa_[16];\n"
// ★★★ **구간은 워커별**(태스크별이 아니다) — 워커가 여러 태스크를 이어 돌리며 커서를 단조로 쓴다.
"        for (int c = 0; c < W_; c++) {\n"
"          wa_[c].q = &q_;\n"
"          wa_[c].bb = LW_BOXPOOL / 2 + c * (LW_BOXPOOL / 32); wa_[c].bl = wa_[c].bb + (LW_BOXPOOL / 32);\n"
"          wa_[c].rb = 2048 + c * 128; wa_[c].rl = wa_[c].rb + 128;\n"
"        }\n"
"        for (int c = 0; c < W_; c++) if (pthread_create(&wth_[c], 0, lw_worker_run, &wa_[c])) lw_panic(\"pthread_create\");\n"
"        for (int c = 0; c < W_; c++) pthread_join(wth_[c], 0);\n"
"        lw_gprog++;\n"
"      }\n"
"    }\n"
"    coop_: ;\n"
"    lw_gthr_ensure(LW_GTHR);\n"                                                  /* 협조적 스케줄러 그린스레드 풀 초기 확보(성장 가능) */
"    for (;;) {\n"
// ★★★ **끝난 자리(state 4) 재활용 → 없으면 새 자리 → 상한 넘으면 거절** (2026-07-21).
//   ☞ 전엔 `lw_ngthr < 16` 만 봐 끝난 자리를 안 썼고, 동시에 살아있는 태스크가 16 을 넘으면
//     조용히 안 들어가 **whoever waited 가 데드락**했다. VM(low_ir.c)과 같은 규율로 맞춘다.
"        while (lw_nambox > 0) {\n"
"            int gi = -1;\n"
"            if (lw_ngthr < lw_gthr_cap) gi = lw_ngthr++;\n"                       /* 게으른 재활용: 상한 전엔 새 자리(슬롯 배정 불변 → 오라클 카운트 불변) */
"            else { for (int q = base; q < lw_ngthr; q++) if (lw_gthr[q].state == 4) { gi = q; break; }\n"
"                   if (gi < 0) { lw_gthr_ensure(lw_ngthr + 1); gi = lw_ngthr++; } }\n"  /* 가득+다 바쁨 → 성장(상한 없음, E-Alloc P4) */
"            lw_gthr[gi].box = lw_ambox[0].box; lw_gthr[gi].fn = lw_ambox[0].fn; lw_gthr[gi].nargs = lw_ambox[0].nargs;\n"
"            lw_gthr[gi].job = lw_ambox[0].job;\n"
"            for (int x = 0; x < lw_ambox[0].nargs; x++) lw_gthr[gi].args[x] = lw_ambox[0].args[x];\n"
"            lw_gthr[gi].state = 1;\n"
"            for (int m = 0; m + 1 < lw_nambox; m++) lw_ambox[m] = lw_ambox[m + 1];\n"
"            lw_nambox--;\n"
"        }\n"
"        int *ready = lw_ready, nready = 0;\n"
"        for (int i = base; i < lw_ngthr; i++) if (lw_gthr[i].state == 1 || lw_gthr[i].state == 2) ready[nready++] = i;\n"
"        if (nready == 0) {\n"
// ★★★ **데드락이라 부르기 전에 커널에게 물어본다** (A2). io 로 막힌 태스크는 **동료가 아니라
//   커널이** 깨운다 — 그래서 여기서 `epoll_wait` 로 자고, 깨어나면 다시 돌린다.
//   ☞ 이 한 줄이 없으면 io 대기가 곧 "데드락" 으로 **오진**된다.
"            if (lw_io_poll_once()) continue;\n"
// ★ refill 이 ambox 를 **항상** 비운다(자리가 모자라면 성장 — E-Alloc P4). 그래서 여기서
//   nambox 가 남는 일은 없다: runnable 도 없고 남은 건 블록뿐이면 그건 **진짜 데드락**이다.
//   (전엔 상한 초과를 데드락과 구분해 lw_panic 을 냈지만, 상한이 사라져 그 경우가 없다.)
"            for (int i = base; i < lw_ngthr; i++) if (lw_gthr[i].state == 5 || lw_gthr[i].state == 6 || lw_gthr[i].state == 7) lw_panic(\"deadlock — every task is blocked (channel, await or io) and none can wake another\");\n"
"            break;\n"
"        }\n"
"        int gi = ready[rr % (unsigned)nready]; rr++; lw_cur_gthr = gi;\n"
"        if (lw_gthr[gi].state == 1) {\n"
"            getcontext(&lw_gthr[gi].ctx);\n"
"            lw_gthr[gi].ctx.uc_stack.ss_sp = lw_gt_stk[gi];\n"
"            lw_gthr[gi].ctx.uc_stack.ss_size = LW_GT_STK;\n"
"            lw_gthr[gi].ctx.uc_link = &lw_sched_ctx;\n"
"            lw_gthr[gi].state = 3;\n"
"            makecontext(&lw_gthr[gi].ctx, lw_gt_tramp, 0);\n"
"            swapcontext(&lw_sched_ctx, &lw_gthr[gi].ctx);\n"
"        } else { lw_gthr[gi].state = 3; swapcontext(&lw_sched_ctx, &lw_gthr[gi].ctx); }\n"
"    }\n"
"    lw_ngthr = base; lw_sched_ctx = lw_saved_ctx; lw_cur_gthr = lw_saved_cur; lw_in_gsched = prev; lw_vec_shared = vprev_;\n"
"}\n"
"static void lw_gyield(void) {\n"
"    if (lw_in_gsched) { int gi = lw_cur_gthr; lw_gthr[gi].state = 2; swapcontext(&lw_gthr[gi].ctx, &lw_sched_ctx); }\n"
"}\n"
//   ⇒ A2: fd 를 **논블로킹**으로 두고, 읽을 것이 없으면 **상태 7(io 대기)** 로 스케줄러에
//     양보한다. 형제가 돈다.
//   ★★★ **바쁜대기가 아니다**: 돌 것이 하나도 없을 때만 스케줄러가 `epoll_wait` 로
//     **진짜로 잔다**. 그것이 reactor 이고, 그래서 이 자리가 스케줄러 안이어야 했다.
//   ☞ 스케줄러 밖(그린스레드가 아닌 곳)에서는 `poll` 로 그냥 막는다 — 굶길 형제가 없다.
// ★★★ **A3 — 백엔드는 갈아끼우는 부품이다** (RFC-0071 A3, 사용자 지적 2026-07-20:
//   *"윈도와 여러 플랫폼을 아우르는 언어인데 리눅스 최신 커널에 반드시 의존한다는 것은 이상하다.
//     그것은 그저 백엔드일 뿐이고 갈아끼울 수 있는 여러 부품 중 하나다."*)
//
//   ★★★ **그리고 이 환경이 그 지적을 증명한다**: 커널은 6.12 이고 `io_uring_setup` 이
//     **존재하는데도** EPERM 이다(샌드박스 정책). **최신 커널 = 쓸 수 있음이 아니다.**
//     ⇒ 특정 백엔드를 전제하면 그 언어는 그 정책에 인질이 된다.
//
//   ⇒ 백엔드를 **런타임에 고른다**. 지금 두 개가 실물로 선다:
//       poll   — POSIX 어디서나. 의존 0
//       epoll  — 리눅스. fd 가 많을 때 poll 보다 낫다
//     그리고 **없으면 조용히 내려간다**(probe 실패 → 다음 것). 죽지 않는다.
//   ★★★ **어느 것을 끼워도 답이 같아야 한다** — 그것이 RFC-0071 §3 의 *"폴백은 성능 축이지
//     의미 축이 아니다"* 이고, 골든이 **같은 프로그램을 두 백엔드로 돌려 바이트 대조**한다.
//   ☞ `LOWENT_REACTOR=poll|epoll` 로 **강제**할 수 있다 — 그래야 그 대조가 가능하다.
//     (io_uring·IOCP·kqueue 는 같은 자리에 끼우면 된다. **여기서 검증할 수 없는 것은 안 짓는다.**)
"enum { LW_RB_POLL = 0, LW_RB_EPOLL = 1, LW_RB_AIO = 2 };\n"
"static int lw_rb = -1;\n"
"static _Thread_local int lw_epfd = -1;\n"
"static void lw_rb_pick(void) {\n"
"    if (lw_rb >= 0) return;\n"
"    const char *want = getenv(\"LOWENT_REACTOR\");\n"
"    if (want && !strcmp(want, \"poll\"))  { lw_rb = LW_RB_POLL;  return; }\n"
// ★★★ **completion 백엔드** (RFC-0071 A6 · RFC-0022 §6.4 · D-B). 아래 주석이 왜 POSIX AIO 인지 적는다.
"    if (want && !strcmp(want, \"aio\"))   { lw_rb = LW_RB_AIO;   return; }\n"
"    if (want && !strcmp(want, \"epoll\")) { lw_epfd = epoll_create1(0); lw_rb = (lw_epfd >= 0) ? LW_RB_EPOLL : LW_RB_POLL; return; }\n"
"    lw_epfd = epoll_create1(0);\n"      /* ★ 없으면(권한·플랫폼) poll 로 내려간다 — 죽지 않는다 */
"    lw_rb = (lw_epfd >= 0) ? LW_RB_EPOLL : LW_RB_POLL;\n"
"}\n"
// ★ 이 fd 를 더는 기다리지 않는다(취소·완료). **poll 백엔드는 등록 자체가 없어 할 일이 없다** —
//   그것이 readiness 모델의 취소가 간단한 이유다(§A4).
"static void lw_io_forget(int fd) {\n"
"    if (lw_rb == LW_RB_EPOLL && lw_epfd >= 0) (void)epoll_ctl(lw_epfd, EPOLL_CTL_DEL, fd, (struct epoll_event *)0);\n"
"}\n"
// ★★★ **completion 백엔드 — POSIX AIO** (RFC-0071 A6, 2026-07-20).
//
//   ☞ **왜 io_uring 이 아닌가**: 이 기계에서 `io_uring_setup` 은 **EPERM** 이다(커널 6.12 인데도 —
//     샌드박스 정책). **검증할 수 없는 백엔드는 짓지 않는다**(§9). POSIX AIO 는 **여기서 돌고**,
//     그리고 **정확히 같은 어려움을 재현한다**: `aio_cancel` 이 `AIO_NOTCANCELED` 를 낸다 —
//     *"구현이 아직 그 버퍼를 쓰고 있다."* 그것이 RFC-0022 §8-9 가 남긴 문제 그 자체다.
//   ⇒ io_uring·IOCP 는 **같은 자리에 끼운다.** 이 백엔드가 그 자리의 모양을 못박는다.
//
//   ★★★ **D-B 를 실물로 만든다: 완료 버퍼는 reactor 가 소유한다.**
//     커널이 쓰는 곳은 **reactor 의 뒷받침 바이트**(호출자가 `reactor_new` 에 준 것)이고,
//     완료된 뒤에야 호출자의 슬라이스로 옮긴다. ⇒ 태스크가 취소돼도 **커널이 쓰는 메모리는
//     여전히 살아 있다** — 그것이 D-B 가 refcount 를 기각하고 소유를 고른 이유다.
"static _Thread_local struct { struct aiocb cb; int used; int gi; unsigned char *dst; size_t dlen; } lw_aio[16];\n"
"static _Thread_local unsigned char *lw_rmem; static _Thread_local size_t lw_rmem_n;\n"
// ★★★ **completion 은 오프셋을 스스로 들어야 한다** (2026-07-20 실측으로 나온 것).
//
//   ☞ `read()` 는 **파일 위치**를 물려받는다 — 부를 때마다 다음 자리를 읽는다.
//     `aio_read()` 는 **`aio_offset` 을 본다** — 우리가 0 으로 두면 **같은 자리를 영원히 다시
//     읽는다.** 실측: rcat 이 AIO 백엔드에서 **끝나지 않았다**(같은 256 바이트를 무한 반복).
//   ★★★ **이것이 readiness 와 completion 의 진짜 의미 차이**이고, *"어느 백엔드를 끼워도
//     답이 같다"* 는 게이트가 **그것을 잡았다**(hang 으로). 백엔드는 부품이지만,
//     **부품마다 무엇을 스스로 들어야 하는지가 다르다.**
//   ⇒ fd 마다 오프셋을 든다. 파이프·소켓은 `aio_offset` 을 무시하므로 **양쪽 다 맞는다.**
"static _Thread_local struct { int fd; off_t off; } lw_aoff[16]; static _Thread_local int lw_naoff;\n"
"static off_t *lw_aoff_of(int fd) {\n"
"    for (int i = 0; i < lw_naoff; i++) if (lw_aoff[i].fd == fd) return &lw_aoff[i].off;\n"
"    if (lw_naoff >= 16) return (off_t *)0;\n"
"    lw_aoff[lw_naoff].fd = fd; lw_aoff[lw_naoff].off = 0; return &lw_aoff[lw_naoff++].off;\n"
"}\n"
// 제출: reactor 소유 자리에 읽는다. 슬롯이 없으면 -1(호출자가 블로킹으로 내려간다).
"static int lw_aio_submit(int fd, unsigned char *dst, size_t n) {\n"
"    int s = -1; for (int i = 0; i < 16; i++) if (!lw_aio[i].used) { s = i; break; }\n"
"    if (s < 0 || !lw_rmem || lw_rmem_n == 0) return -1;\n"
"    size_t cap = lw_rmem_n / 16; if (cap == 0) return -1;\n"
"    if (n > cap) n = cap;\n"
"    memset(&lw_aio[s].cb, 0, sizeof lw_aio[s].cb);\n"
"    lw_aio[s].cb.aio_fildes = fd;\n"
"    lw_aio[s].cb.aio_buf = lw_rmem + (size_t)s * cap;\n"   /* ★ reactor 소유 자리 */
"    lw_aio[s].cb.aio_nbytes = n;\n"
"    { off_t *o_ = lw_aoff_of(fd); lw_aio[s].cb.aio_offset = o_ ? *o_ : 0; }\n"
"    if (aio_read(&lw_aio[s].cb) != 0) return -1;\n"
"    lw_aio[s].used = 1; lw_aio[s].gi = lw_in_gsched ? lw_cur_gthr : -1;\n"
"    lw_aio[s].dst = dst; lw_aio[s].dlen = n;\n"
"    return s;\n"
"}\n"
// 수확: 완료됐으면 reactor 자리에서 호출자 자리로 옮기고 읽은 수를 낸다. -1 = 아직.
"static long lw_aio_reap(int s) {\n"
"    int e = aio_error(&lw_aio[s].cb);\n"
"    if (e == EINPROGRESS) return -1;\n"
"    ssize_t got = aio_return(&lw_aio[s].cb);\n"
"    if (got > 0) { off_t *o_ = lw_aoff_of(lw_aio[s].cb.aio_fildes); if (o_) *o_ += got; }\n"
"    if (got > 0 && lw_aio[s].dst) memcpy(lw_aio[s].dst, (const void *)lw_aio[s].cb.aio_buf, (size_t)got);\n"
"    lw_aio[s].used = 0;\n"
"    return got < 0 ? 0 : (long)got;\n"
"}\n"
// ★★★ **취소 — 그리고 커널이 아직 쓰고 있으면 버퍼를 살려 둔다**(D-B).
//   `AIO_NOTCANCELED` 는 *"못 멈춘다"* 이고, 그때 우리가 할 수 있는 정직한 일은
//   **그 자리를 반납하지 않는 것**이다. 슬롯은 완료가 수확될 때까지 살아 있다.
"static void lw_aio_drop(int s) {\n"
"    if (s < 0 || !lw_aio[s].used) return;\n"
"    lw_aio[s].dst = 0;\n"          /* 호출자 자리로는 더 안 옮긴다 — 그 태스크는 죽었다 */
"    lw_aio[s].gi = -1;\n"
"    if (aio_cancel(lw_aio[s].cb.aio_fildes, &lw_aio[s].cb) == AIO_CANCELED) { (void)aio_return(&lw_aio[s].cb); lw_aio[s].used = 0; }\n"
"}\n"
// ★ completion 을 기다린다. 스케줄러 안이면 **형제에게 넘기고**, 밖이면 `aio_suspend` 로 잔다.
"static void lw_io_wait_aio(int s) {\n"
"    if (!lw_in_gsched) { const struct aiocb *l[1]; l[0] = &lw_aio[s].cb; (void)aio_suspend(l, 1, (const struct timespec *)0); return; }\n"
"    int gi = lw_cur_gthr; lw_gthr[gi].state = 7; lw_gthr[gi].blk_fd = -1; lw_gthr[gi].blk_aio = s;\n"
"    swapcontext(&lw_gthr[gi].ctx, &lw_sched_ctx);\n"
"}\n"

"static void lw_nonblock(int fd) { int fl = fcntl(fd, F_GETFL, 0); if (fl >= 0) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK); }\n"
// fd 가 읽을 수 있게 될 때까지 이 태스크만 재운다. 스케줄러가 epoll 로 깨운다.
"static void lw_io_wait(int fd) {\n"
"    if (!lw_in_gsched) { struct pollfd pf; pf.fd = fd; pf.events = POLLIN; (void)poll(&pf, 1, -1); return; }\n"
"    lw_rb_pick();\n"
"    if (lw_rb == LW_RB_EPOLL) {\n"
"        struct epoll_event ev; ev.events = EPOLLIN | EPOLLONESHOT; ev.data.fd = fd;\n"
"        if (epoll_ctl(lw_epfd, EPOLL_CTL_ADD, fd, &ev) < 0 && errno == EEXIST) (void)epoll_ctl(lw_epfd, EPOLL_CTL_MOD, fd, &ev);\n"
"    }\n"
"    int gi = lw_cur_gthr; lw_gthr[gi].state = 7; lw_gthr[gi].blk_fd = fd; lw_gthr[gi].blk_aio = -1;\n"
"    swapcontext(&lw_gthr[gi].ctx, &lw_sched_ctx);\n"
"}\n"
// ★ 돌 것이 없을 때 **한 번** 부른다: 하나라도 깨우면 1, 아무도 못 깨우면 0(그러면 진짜 데드락).
"static int lw_io_poll_once(void) {\n"
"    int any = 0; for (int i = 0; i < lw_ngthr; i++) if (lw_gthr[i].state == 7) any = 1;\n"
"    if (!any) return 0;\n"
"    lw_rb_pick();\n"
"    int woke = 0;\n"
// ★ completion 백엔드 — `aio_suspend` 로 **하나라도 끝날 때까지** 잔다. 바쁜대기가 아니다.
"    if (lw_rb == LW_RB_AIO) {\n"
"        const struct aiocb *l[16]; int idx[16]; int nl = 0;\n"
"        for (int i = 0; i < lw_ngthr && nl < 16; i++)\n"
"            if (lw_gthr[i].state == 7 && lw_gthr[i].blk_aio >= 0) { l[nl] = &lw_aio[lw_gthr[i].blk_aio].cb; idx[nl] = i; nl++; }\n"
"        if (nl == 0) return 0;\n"
"        (void)aio_suspend(l, nl, (const struct timespec *)0);\n"
"        for (int q = 0; q < nl; q++)\n"
"            if (aio_error(l[q]) != EINPROGRESS) { lw_gthr[idx[q]].state = 2; woke = 1; }\n"
"        return woke;\n"
"    }\n"
"    if (lw_rb == LW_RB_EPOLL) {\n"
"        struct epoll_event evs[16];\n"
"        int n = epoll_wait(lw_epfd, evs, 16, -1);\n"
"        if (n <= 0) return 0;\n"
"        for (int e = 0; e < n; e++)\n"
"            for (int i = 0; i < lw_ngthr; i++)\n"
"                if (lw_gthr[i].state == 7 && lw_gthr[i].blk_fd == evs[e].data.fd) { lw_gthr[i].state = 2; woke = 1; }\n"
"    } else {\n"
// ★ poll 백엔드 — 의존 0. 막힌 태스크의 fd 를 모아 한 번 잔다. **의미는 epoll 과 같다.**
"        struct pollfd pfs[16]; int idx[16]; int np = 0;\n"
"        for (int i = 0; i < lw_ngthr && np < 16; i++)\n"
"            if (lw_gthr[i].state == 7) { pfs[np].fd = lw_gthr[i].blk_fd; pfs[np].events = POLLIN; pfs[np].revents = 0; idx[np] = i; np++; }\n"
"        if (np == 0) return 0;\n"
"        int n = poll(pfs, (nfds_t)np, -1);\n"
"        if (n <= 0) return 0;\n"
"        for (int q = 0; q < np; q++)\n"
"            if (pfs[q].revents) { lw_gthr[idx[q]].state = 2; woke = 1; }\n"
"    }\n"
"    return woke;\n"
"}\n"

"static int lw_chnew(void) { if (lw_nchan >= 64) lw_panic(\"channel pool\"); int ci = lw_nchan++;\n"
"    lw_chans[ci].head = lw_chans[ci].tail = lw_chans[ci].count = 0; return ci; }\n"
// ★ 채널에서 막힐 땐 상태 5(블록)+무엇에 막혔는지 기록하고 스케줄러로 swap — 채널 op 성공이 깨운다.
"static void lw_gblock(int ci, int recv) {\n"
"    int gi = lw_cur_gthr; lw_gthr[gi].state = 5; lw_gthr[gi].blk_chan = ci; lw_gthr[gi].blk_recv = recv;\n"
"    swapcontext(&lw_gthr[gi].ctx, &lw_sched_ctx);\n"
"}\n"
"static lowv lw_chsend(int ci, lowv v) {\n"
"    if (ci < 0 || ci >= lw_nchan) lw_panic(\"`chsend` needs a channel handle (from `channel`)\");\n"   /* VM 의 E-VM-CHAN 과 같은 게이트 — 조작된 핸들 거절 */
"    while (lw_chans[ci].count >= 64) { if (!lw_in_gsched) lw_panic(\"`chsend` on a full channel with no receiver\"); lw_gblock(ci, 0); }\n"
"    lw_chans[ci].buf[lw_chans[ci].tail] = v; lw_chans[ci].tail = (lw_chans[ci].tail + 1) % 64; lw_chans[ci].count++; lw_gprog++;\n"
"    for (int q = 0; q < lw_ngthr; q++) if (lw_gthr[q].state == 5 && lw_gthr[q].blk_chan == ci && lw_gthr[q].blk_recv) lw_gthr[q].state = 2;\n"   /* recver 를 깨운다 */
"    lowv u = {0}; u.tag = LWV_INT; return u; }\n"
"static lowv lw_chrecv(int ci) {\n"
"    if (ci < 0 || ci >= lw_nchan) lw_panic(\"`chrecv` needs a channel handle (from `channel`)\");\n"
"    while (lw_chans[ci].count == 0) { if (!lw_in_gsched) lw_panic(\"`chrecv` on an empty channel with no sender\"); lw_gblock(ci, 1); }\n"
"    lowv v = lw_chans[ci].buf[lw_chans[ci].head]; lw_chans[ci].head = (lw_chans[ci].head + 1) % 64; lw_chans[ci].count--; lw_gprog++;\n"
"    for (int q = 0; q < lw_ngthr; q++) if (lw_gthr[q].state == 5 && lw_gthr[q].blk_chan == ci && !lw_gthr[q].blk_recv) lw_gthr[q].state = 2;\n"   /* sender 를 깨운다 */
"    return v; }\n"
// ★★★ **await** — job 완료까지 (태스크면 블록·본문이면 스케줄러 구동) 기다렸다 결과를 준다.
"static lowv lw_await(int jid) {\n"
"    if (jid < 0 || jid >= lw_njobs) lw_panic(\"`await` needs a job handle (from `spawn <op>`)\");\n"
"    if (!atomic_load_explicit(&lw_jobs[jid].done, memory_order_acquire)) {\n"
"        if (lw_in_gsched) { while (!atomic_load_explicit(&lw_jobs[jid].done, memory_order_acquire)) { int gi = lw_cur_gthr; lw_gthr[gi].state = 6; lw_gthr[gi].blk_job = jid;\n"
"            swapcontext(&lw_gthr[gi].ctx, &lw_sched_ctx); } }\n"
"        else { lw_schedule(); if (!atomic_load_explicit(&lw_jobs[jid].done, memory_order_acquire)) lw_panic(\"the awaited task never completed\"); }\n"
"    }\n"
"    return lw_jobs[jid].result; }\n"
;
// ── 동시성 런타임 끝. 이 아래는 항상 방출되는 core.
const char LW_PRELUDE2[] =
"static struct { lowv items[128]; size_t n, cap; } lw_stks[LW_STKPOOL]; static int lw_nstk;\n"
"static unsigned long long lw_bsets[LW_BSETPOOL]; static unsigned char lw_bwid[LW_BSETPOOL]; static int lw_nbset;\n"
"static lowv lw_wrap_ok(lowv v) { if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\"); lw_boxes[lw_nbox] = v;\n"
"    lowv r = {0}; r.tag = LWV_OK; r.box = lw_nbox++; return r; }\n"
"static lowv lw_err(int e) { lowv r = {0}; r.tag = LWV_ERR; r.i = e; return r; }\n"
"static lowv lw_make(int mk, int nf, const lowv *vals) { if (lw_nrec >= lw_reclim) lw_panic(\"record pool exhausted — raise it with -DLW_RECPOOL=N (docs/runtime-pools.md)\");\n"
"    lowrec *r = &lw_recs[lw_nrec]; r->mk = mk; r->nf = nf; memcpy(r->f, vals, sizeof(lowv) * (size_t)nf);\n"
"    lowv v = {0}; v.tag = LWV_REC; v.box = lw_nrec++; return v; }\n"
"static lowv lw_snew(lowv cap) { long long c = lw_want_int(cap, \"capacity\"); if (lw_nstk >= LW_STKPOOL) lw_panic(\"stack pool exhausted — raise it with -DLW_STKPOOL=N (docs/runtime-pools.md)\");\n"
"    if (c < 0 || c > 128) lw_panic(\"stack capacity exceeded\");   /* \xe2\x98\x85 \xec\x84\xa0\xec\x96\xb8 capacity \xeb\xa5\xbc \xed\x95\x9c\xeb\x8f\x84\xeb\xa1\x9c(VM \xea\xb3\xbc \xeb\x8c\x80\xec\xb9\xad) */\n"
"    lw_stks[lw_nstk].n = 0; lw_stks[lw_nstk].cap = (size_t)c; lowv v = {0}; v.tag = LWV_STACK; v.box = lw_nstk++; return v; }\n"
"static lowv lw_spush(lowv s, lowv x) { if (s.tag != LWV_STACK) lw_panic(\"push needs a stack\");\n"
"    if (lw_stks[s.box].n >= lw_stks[s.box].cap) lw_panic(\"stack capacity exceeded\");\n"
"    lw_stks[s.box].items[lw_stks[s.box].n++] = x; return lw_int(0); }\n"
/* ★ RFC-0016 총체형 소비자 — VM 핸들러와 **같은 규칙**(diff-sweep 로 VM≡native). */
// ★ `value_or` 의 지연 분기가 쓰는 술어 — some/ok 면 1, none/err 면 0.
//   `is_some` 은 result 를, `is_ok` 는 option 을 거절하므로 **둘 다 받는 것**이 필요했다.
"static long long lw_has_value(lowv v) {\n"
"    if (v.tag == LWV_SOME || v.tag == LWV_OK) return 1;\n"
"    if (v.tag == LWV_NONE || v.tag == LWV_ERR) return 0;\n"
"    lw_panic(\"value_or needs an option or a result\"); return 0; }\n"
"static lowv lw_value_or(lowv v, lowv d) {\n"
"    if (v.tag == LWV_SOME || v.tag == LWV_OK) return lw_boxes[v.box];\n"
"    if (v.tag == LWV_NONE || v.tag == LWV_ERR) return d;\n"
"    lw_panic(\"value_or needs an option or a result\"); return lw_int(0); }\n"
"static lowv lw_poptot(lowv s) { if (s.tag != LWV_STACK) lw_panic(\"pop needs a stack\");\n"
"    if (lw_stks[s.box].n == 0) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
"    lw_boxes[lw_nbox] = lw_stks[s.box].items[--lw_stks[s.box].n];\n"
"    lowv r = {0}; r.tag = LWV_SOME; r.box = lw_nbox++; return r; }\n"
"static lowv lw_spop(lowv s, lowv *into) { if (s.tag != LWV_STACK) lw_panic(\"pop needs a stack\");\n"
"    if (lw_stks[s.box].n == 0) return lw_int(0); *into = lw_stks[s.box].items[--lw_stks[s.box].n]; return lw_int(1); }\n"
"static lowv lw_bnew(lowv n) { long long w = lw_want_int(n, \"bits\"); if (w > 64) lw_panic(\"bitset width > 64\");\n"
"    if (lw_nbset >= LW_BSETPOOL) lw_panic(\"bitset pool exhausted — raise it with -DLW_BSETPOOL=N (docs/runtime-pools.md)\"); lw_bsets[lw_nbset] = 0;\n"
"    lw_bwid[lw_nbset] = (unsigned char)(w <= 0 ? 64 : w);   /* \\xe2\\x98\\x85 \\xed\\x8f\\xad\\xec\\x9d\\x84 \\xea\\xb8\\xb0\\xec\\x96\\xb5(VM \\xea\\xb3\\xbc \\xea\\xb0\\x99\\xec\\x9d\\x80 \\xea\\xb7\\x9c\\xec\\xb9\\x99) */\n"
"    lowv v = {0}; v.tag = LWV_BITSET; v.box = lw_nbset++; return v; }\n"
"static struct { unsigned long long l[16]; int n, esz, flt, sign; } lw_vecs[32]; static int lw_nvecpool;\n"
// ★★★ **결정(F2, 2026-08-04): 워커 스레드 안에서는 벡터 풀을 되감지 않는다 — 그것이 답이다.**
//   박스·레코드는 워커마다 **서로소 구간**을 받지만 벡터 풀은 **32칸**뿐이라 나눌 수가 없다
//   (워커 넷이면 여덟 칸씩 — 16레인 한 줄이 여덟 칸을 쓰는 프로그램이 곧 죽는다). 풀을 키우면
//   프리스탠딩 예산을 건드린다(bss 0 이 계약인 자리다). ⇒ **capability 의 문제가 아니라 크기의
//   문제**이고, 안전은 이미 서 있다: 되감기가 `lw_vec_shared` 를 보고 그 안에서는 안 돈다.
//   ☞ 커서만 스레드 지역으로 만드는 것은 **틀린 수리**다(배열이 공유라 워커끼리 같은 칸을 밟는다).
//     한 번 그렇게 고쳤다가 되돌렸고, 되돌린 것을 여기 적어 둔다.
/* ★★★★ **레인은 자기 폭에 담긴다** (RFC-0040 §9, 2026-08-02 — 축 ②의 첫 증분).
 *   그전까지 빠른 경로의 레인 칸은 전부 `unsigned long long` 이었다. u32 레인 하나가
 *   **칸을 두 배** 먹었고, 그래서 (ㄱ) 메모리 대역이 두 배, (ㄴ) 컴파일러가 되벡터화해도
 *   자연 폭의 **절반 처리량**, (ㄷ) 매 연산마다 폭으로 되자르는 AND 가 붙었다.
 *   ⇒ 64바이트 공용체 하나로 바꾼다. **프레임 크기는 그대로**(8×u64 = 16×u32 = 64 B)인데
 *     레인은 제 폭에 담기고, 되자르기는 C 의 대입이 공짜로 해 준다.
 *   ★ 그리고 u32 의 빠른 경로 레인 상한이 8 → 16 이 된다 — `native_lanes` 가 답하는 폭
 *     (x86_64 simd=16B → u8 16 · u32 4)을 **그대로 쓸 수 있다**. 전에는 8 을 넘으면
 *     조용히 태그 경로로 떨어졌다: 답은 맞고 속도만 죽는, 오라클이 못 보는 종류의 손해. */
"static long long lw_lsx(unsigned long long x, int esz) {   /* \xe2\x98\x85 \xeb\xa0\x88\xec\x9d\xb8 \xeb\xb6\x80\xed\x98\xb8\xed\x99\x95\xec\x9e\xa5 */\n"
"    int b = esz * 8; if (b >= 64) return (long long)x;\n"
"    unsigned long long m = 1ull << (b - 1); return (long long)((x ^ m) - m); }\n"
"static unsigned long long lw_emask(int esz) { return esz >= 8 ? ~0ull : ((1ull << (8 * esz)) - 1); }\n"
"static lowv lw_vecv(int idx) { lowv v = {0}; v.tag = LWV_VEC; v.box = idx; return v; }\n"
// ★ 태그 벡터의 한 레인 비트를 꺼낸다(빠른 경로의 av[] 채널이 경계에서 부른다).
"static unsigned long long lw_vlane(lowv v, int lane) {\n"
"    if (v.tag != LWV_VEC) lw_panic(\"a vector argument\");\n"
"    return lw_vecs[v.box].l[lane]; }\n"
"static int lw_vecslot(void) { if (lw_nvecpool >= 32) lw_panic(\"vector pool exhausted (32 slots — this frame's vectors could not be rewound; see docs/runtime-pools.md. The fast path has no pool at all)\"); return lw_nvecpool++; }\n"
// ★★★ **벡터 풀이 되감긴다 — 태그 경로도** (RFC-0089 R2-2 · 단계 B, 2026-08-03).
//   VM 과 **똑같은 규율**이고, 그래야 하는 이유가 이 단계의 전부다: 한쪽만 되감으면 결함이
//   고쳐지는 것이 아니라 **자리를 옮긴다**(실측 2026-08-03: VM 을 먼저 고치자 이번엔 태그 경로가
//   100회에서 죽었다 — 같은 프로그램, 반대 방향의 같은 발산).
//   뿌리는 이 프레임의 `loc[]`·`st[]` 뿐이다 — 벡터 핸들이 박스·레코드·스택으로 샜으면
//   되감지 않는다(그때는 옛 규율 그대로 단조 증가한다: **정직한 보수**).
"static int lw_vec_outside(int base) {\n"
"    for (int i = 0; i < lw_nbox; i++) if (lw_boxes[i].tag == LWV_VEC && lw_boxes[i].box >= base) return 1;\n"
"    for (int i = 0; i < lw_nrec; i++) { lowrec *r_ = &lw_recs[i];\n"
"        for (int f = 0; f < r_->nf; f++) if (r_->f[f].tag == LWV_VEC && r_->f[f].box >= base) return 1; }\n"
"    for (int i = 0; i < lw_nstk; i++) for (size_t k = 0; k < lw_stks[i].n; k++)\n"
"        if (lw_stks[i].items[k].tag == LWV_VEC && lw_stks[i].items[k].box >= base) return 1;\n"
"    return 0; }\n"
"static int lw_vec_may(int base) {\n"
"    if (lw_nvecpool <= base) return 0;\n"
"    if (lw_vec_shared) return 0;   /* 풀을 나눠 쓰는 흐름 — 다른 흐름의 뿌리가 안 보인다 */\n"
"    return !lw_vec_outside(base); }\n"
"static void lw_vecgc(int base, lowv *loc, int nloc, lowv *st, int sp) {\n"
"    if (!lw_vec_may(base)) return;\n"
"    int map[32]; for (int i = 0; i < 32; i++) map[i] = -1;\n"
"    int top = base;\n"
"    for (int old = base; old < lw_nvecpool; old++) { int live = 0;\n"
"        for (int i = 0; i < nloc && !live; i++) live = (loc[i].tag == LWV_VEC && loc[i].box == old);\n"
"        for (int i = 0; i < sp && !live; i++)   live = (st[i].tag == LWV_VEC && st[i].box == old);\n"
"        if (!live) continue;\n"
"        if (top != old) lw_vecs[top] = lw_vecs[old];\n"
"        map[old] = top++; }\n"
"    for (int i = 0; i < nloc; i++) if (loc[i].tag == LWV_VEC && loc[i].box >= base) loc[i].box = map[loc[i].box];\n"
"    for (int i = 0; i < sp; i++)   if (st[i].tag == LWV_VEC && st[i].box >= base) st[i].box = map[st[i].box];\n"
"    lw_nvecpool = top; }\n"
// ★★★★ **네 풀도 루프에서 마르지 않는다 — 태그 경로에서도** (RFC-0089 후속 F1, 2026-08-04).
//   VM 에 먼저 넣었더니 같은 프로그램이 **네이티브 태그 경로**에서 2× 에 죽었다(단계 B 와 똑같은
//   자리, 똑같은 교훈). 박스·레코드·스택은 서로를 품으므로 **전이 표시 + 압축**이다.
//   ☞ 뿌리는 이 프레임의 `loc`·`st` 와 **바닥 아래의 풀 원소들**(그들이 위를 가리킬 수 있다).
//     C 는 호출자의 프레임을 훑을 수 없으므로, **참조를 쓰는 op 에는 이 코드를 아예 안 낸다**
//     (`mut_ref` 로 남의 지역에 써 넣을 수 있는 유일한 문 — 정적으로 막는다).
"static int lw_pm_bx[LW_BOXPOOL], lw_pm_rc[LW_RECPOOL], lw_pm_sk[LW_STKPOOL], lw_pm_bs[LW_BSETPOOL], lw_pm_vc[32];\n"
"static void lw_gc_mark(lowv v);\n"
"static void lw_gc_slot(int *map, int lo, int hi, int idx, int kind) {\n"
"    if (idx < lo || idx >= hi || map[idx] != -1) return;\n"
"    map[idx] = -2;\n"
"    if (kind == 0) lw_gc_mark(lw_boxes[idx]);\n"
"    else if (kind == 1) { for (int f = 0; f < lw_recs[idx].nf; f++) lw_gc_mark(lw_recs[idx].f[f]); }\n"
"    else if (kind == 2) { for (size_t k = 0; k < lw_stks[idx].n; k++) lw_gc_mark(lw_stks[idx].items[k]); }\n"
"}\n"
"static int lw_gcb_bx, lw_gcb_rc, lw_gcb_sk, lw_gcb_bs, lw_gcb_vc;\n"
"static void lw_gc_mark(lowv v) {\n"
"    switch (v.tag) {\n"
"        case LWV_SOME: case LWV_OK:  lw_gc_slot(lw_pm_bx, lw_gcb_bx, lw_nbox,  v.box, 0); break;\n"
"        case LWV_REC:  case LWV_VIEW: lw_gc_slot(lw_pm_rc, lw_gcb_rc, lw_nrec,  v.box, 1); break;\n"
"        case LWV_STACK: lw_gc_slot(lw_pm_sk, lw_gcb_sk, lw_nstk,  v.box, 2); break;\n"
"        case LWV_BITSET: lw_gc_slot(lw_pm_bs, lw_gcb_bs, lw_nbset, v.box, 3); break;\n"
"        case LWV_VEC:   lw_gc_slot(lw_pm_vc, lw_gcb_vc, lw_nvecpool, v.box, 4); break;\n"
"        default: break;\n"
"    }\n"
"}\n"
"static void lw_gc_fix(lowv *v) {\n"
"    int *m = 0, lo = 0, hi = 0;\n"
"    switch (v->tag) {\n"
"        case LWV_SOME: case LWV_OK:  m = lw_pm_bx; lo = lw_gcb_bx; hi = lw_nbox;  break;\n"
"        case LWV_REC:  case LWV_VIEW: m = lw_pm_rc; lo = lw_gcb_rc; hi = lw_nrec;  break;\n"
"        case LWV_STACK: m = lw_pm_sk; lo = lw_gcb_sk; hi = lw_nstk;  break;\n"
"        case LWV_BITSET: m = lw_pm_bs; lo = lw_gcb_bs; hi = lw_nbset; break;\n"
"        case LWV_VEC:   m = lw_pm_vc; lo = lw_gcb_vc; hi = lw_nvecpool; break;\n"
"        default: return;\n"
"    }\n"
"    if (v->box >= lo && v->box < hi && m[v->box] >= 0) v->box = m[v->box];\n"
"}\n"
"static void lw_pool_gc(int bxb, int rcb, int skb, int bsb, int vcb, lowv *loc, int nloc, lowv *st, int sp) {\n"
"    if (lw_vec_shared) return;   /* 풀을 나눠 쓰는 흐름 — 남의 뿌리가 안 보인다 */\n"
"    if (lw_nbox <= bxb && lw_nrec <= rcb && lw_nstk <= skb && lw_nbset <= bsb && lw_nvecpool <= vcb) return;\n"
"    lw_gcb_bx = bxb; lw_gcb_rc = rcb; lw_gcb_sk = skb; lw_gcb_bs = bsb; lw_gcb_vc = vcb;\n"
"    for (int i = bxb; i < lw_nbox; i++) lw_pm_bx[i] = -1;\n"
"    for (int i = rcb; i < lw_nrec; i++) lw_pm_rc[i] = -1;\n"
"    for (int i = skb; i < lw_nstk; i++) lw_pm_sk[i] = -1;\n"
"    for (int i = bsb; i < lw_nbset; i++) lw_pm_bs[i] = -1;\n"
"    for (int i = vcb; i < lw_nvecpool; i++) lw_pm_vc[i] = -1;\n"
"    for (int i = 0; i < nloc; i++) lw_gc_mark(loc[i]);\n"
"    for (int i = 0; i < sp; i++)   lw_gc_mark(st[i]);\n"
"    for (int i = 0; i < bxb; i++) lw_gc_mark(lw_boxes[i]);\n"
"    for (int i = 0; i < rcb; i++) for (int f = 0; f < lw_recs[i].nf; f++) lw_gc_mark(lw_recs[i].f[f]);\n"
"    for (int i = 0; i < skb; i++) for (size_t k = 0; k < lw_stks[i].n; k++) lw_gc_mark(lw_stks[i].items[k]);\n"
"    int tb = bxb, tr = rcb, ts = skb, tt = bsb, tv = vcb;\n"
"    for (int i = bxb; i < lw_nbox; i++)    if (lw_pm_bx[i] == -2) lw_pm_bx[i] = tb++;\n"
"    for (int i = rcb; i < lw_nrec; i++)    if (lw_pm_rc[i] == -2) lw_pm_rc[i] = tr++;\n"
"    for (int i = skb; i < lw_nstk; i++)    if (lw_pm_sk[i] == -2) lw_pm_sk[i] = ts++;\n"
"    for (int i = bsb; i < lw_nbset; i++)   if (lw_pm_bs[i] == -2) lw_pm_bs[i] = tt++;\n"
"    for (int i = vcb; i < lw_nvecpool; i++) if (lw_pm_vc[i] == -2) lw_pm_vc[i] = tv++;\n"
"    for (int i = bxb; i < lw_nbox; i++)  if (lw_pm_bx[i] >= 0 && lw_pm_bx[i] != i) lw_boxes[lw_pm_bx[i]] = lw_boxes[i];\n"
"    for (int i = rcb; i < lw_nrec; i++)  if (lw_pm_rc[i] >= 0 && lw_pm_rc[i] != i) lw_recs[lw_pm_rc[i]] = lw_recs[i];\n"
"    for (int i = skb; i < lw_nstk; i++)  if (lw_pm_sk[i] >= 0 && lw_pm_sk[i] != i) lw_stks[lw_pm_sk[i]] = lw_stks[i];\n"
"    for (int i = bsb; i < lw_nbset; i++) if (lw_pm_bs[i] >= 0 && lw_pm_bs[i] != i) { lw_bsets[lw_pm_bs[i]] = lw_bsets[i]; lw_bwid[lw_pm_bs[i]] = lw_bwid[i]; }\n"
"    for (int i = vcb; i < lw_nvecpool; i++) if (lw_pm_vc[i] >= 0 && lw_pm_vc[i] != i) lw_vecs[lw_pm_vc[i]] = lw_vecs[i];\n"
"    for (int i = 0; i < nloc; i++) lw_gc_fix(&loc[i]);\n"
"    for (int i = 0; i < sp; i++)   lw_gc_fix(&st[i]);\n"
"    for (int i = 0; i < tb; i++) lw_gc_fix(&lw_boxes[i]);\n"
"    for (int i = 0; i < tr; i++) for (int f = 0; f < lw_recs[i].nf; f++) lw_gc_fix(&lw_recs[i].f[f]);\n"
"    for (int i = 0; i < ts; i++) for (size_t k = 0; k < lw_stks[i].n; k++) lw_gc_fix(&lw_stks[i].items[k]);\n"
"    lw_nbox = tb; lw_nrec = tr; lw_nstk = ts; lw_nbset = tt; lw_nvecpool = tv;\n"
"}\n"
"static lowv lw_vecret(int base, lowv rv) {\n"
"    if (!lw_vec_may(base)) return rv;\n"
"    if (rv.tag == LWV_VEC && rv.box >= base) {\n"
"        if (rv.box != base) lw_vecs[base] = lw_vecs[rv.box];\n"
"        rv.box = base; lw_nvecpool = base + 1; }\n"
"    else lw_nvecpool = base;\n"
"    return rv; }\n"
"static lowv lw_vbin(int op, lowv a, lowv b) {   // 0 add 1 sub 2 mul 3 div; lanewise\n"
"    if (lw_vecs[a.box].n != lw_vecs[b.box].n) lw_panic(\"vector lane counts differ\");\n"
"    int s = lw_vecslot(); int n = lw_vecs[a.box].n, esz = lw_vecs[a.box].esz, fl = lw_vecs[a.box].flt;\n"
"    if (fl != lw_vecs[b.box].flt) lw_panic(\"vector lane kinds differ\");\n"
"    for (int k = 0; k < n; k++) {\n"
"        if (fl) { double x = lw_b2f(lw_vecs[a.box].l[k], esz), y = lw_b2f(lw_vecs[b.box].l[k], esz);\n"
"            double r = op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : x / y;\n"
"            lw_vecs[s].l[k] = lw_f2b(r, esz); }\n"
"        else { unsigned long long x = lw_vecs[a.box].l[k], y = lw_vecs[b.box].l[k];\n"
"            lw_vecs[s].l[k] = (op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : (y ? x / y : (lw_panic(\"divide by zero\"), 0ull))) & lw_emask(esz); } }\n"
"    lw_vecs[s].n = n; lw_vecs[s].esz = esz; lw_vecs[s].flt = fl; lw_vecs[s].sign = lw_vecs[a.box].sign; return lw_vecv(s); }\n"
"static lowv lw_vcmp(int op, lowv a, lowv b) {   // 0 eq 1 ne 2 lt 3 le 4 gt 5 ge → mask\n"
"    if (lw_vecs[a.box].n != lw_vecs[b.box].n) lw_panic(\"vector lane counts differ\");\n"
"    unsigned long long bits = 0; int n = lw_vecs[a.box].n, esz = lw_vecs[a.box].esz, fl = lw_vecs[a.box].flt;\n"
"    for (int k = 0; k < n; k++) { int t;\n"
"        if (fl) { double x = lw_b2f(lw_vecs[a.box].l[k], esz), y = lw_b2f(lw_vecs[b.box].l[k], esz);\n"
"            t = op == 0 ? x == y : op == 1 ? x != y : op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y; }\n"
"        else if (lw_vecs[a.box].sign) {   /* \xe2\x98\x85 \xeb\xb6\x80\xed\x98\xb8\xed\x98\x95 \xeb\xa0\x88\xec\x9d\xb8\xec\x9d\x80 \xeb\xb6\x80\xed\x98\xb8\xeb\xa1\x9c \xeb\xb9\x84\xea\xb5\x90 */\n"
"            long long x = lw_lsx(lw_vecs[a.box].l[k], esz), y = lw_lsx(lw_vecs[b.box].l[k], esz);\n"
"            t = op == 0 ? x == y : op == 1 ? x != y : op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y; }\n"
"        else { unsigned long long x = lw_vecs[a.box].l[k], y = lw_vecs[b.box].l[k];\n"
"            t = op == 0 ? x == y : op == 1 ? x != y : op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y; }\n"
"        if (t) bits |= 1ull << k; }\n"
"    lowv m = {0}; m.tag = LWV_MASK; m.i = (long long)bits; m.box = n; return m; }\n"
"static lowv lw_add(lowv a, lowv b) {\n"
"    if (a.tag == LWV_FLT && b.tag == LWV_FLT) return lw_flt(lw_fval(a) + lw_fval(b));\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vbin(0, a, b);\n"
"    if (a.tag == LWV_BITSET) { long long k = lw_want_int(b, \"element\");\n"
"        if (k < 0 || k >= lw_bwid[a.box]) lw_panic(\"bitset element out of range\");\n"
"        lw_bsets[a.box] |= 1ull << k; return lw_int(0); }\n"
"    return lw_int((long long)((unsigned long long)lw_want_int(a, \"ints\") + (unsigned long long)lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_sub(lowv a, lowv b) { if (a.tag == LWV_FLT && b.tag == LWV_FLT) return lw_flt(lw_fval(a) - lw_fval(b));\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vbin(1, a, b);\n"
"    return lw_int((long long)((unsigned long long)lw_want_int(a, \"ints\") - (unsigned long long)lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_mul(lowv a, lowv b) { if (a.tag == LWV_FLT && b.tag == LWV_FLT) return lw_flt(lw_fval(a) * lw_fval(b));\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vbin(2, a, b);\n"
"    return lw_int((long long)((unsigned long long)lw_want_int(a, \"ints\") * (unsigned long long)lw_want_int(b, \"ints\"))); }\n"
"static lowv lw_cmp(int op, lowv a, lowv b) {\n"
"    if (a.tag == LWV_FLT && b.tag == LWV_FLT) { double x = lw_fval(a), y = lw_fval(b);\n"
"        return lw_int(op == 0 ? x == y : op == 1 ? x != y : op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y); }\n"
"    if (a.tag == LWV_VEC && b.tag == LWV_VEC) return lw_vcmp(op, a, b);\n"
"    long long x = lw_want_int(a, \"ints\"), y = lw_want_int(b, \"ints\");\n"
"    return lw_int(op == 0 ? x == y : op == 1 ? x != y : op == 2 ? x < y : op == 3 ? x <= y : op == 4 ? x > y : x >= y); }\n"
"static lowv lw_neg(lowv a) { if (a.tag == LWV_FLT) return lw_flt(-lw_fval(a));\n"
"    return lw_int((long long)(0ull - (unsigned long long)lw_want_int(a, \"ints\"))); }\n"
"static lowv lw_unm(long long meta, lowv v) {   // 0 sqrt 1 abs 2 floor 3 ceil 4 nonzero_of 5 sum_neumaier 6 sum_seq\n"
"    long long op = meta & 0xf;   /* \xe2\x98\x85 \xec\x84\xa0\xed\x83\x9d\xec\x9e\x90\xeb\x8a\x94 \xed\x95\x98\xec\x9c\x84 4\xeb\xb9\x84\xed\x8a\xb8 \xe2\x80\x94 abs \xeb\x8a\x94 \xec\x83\x81\xec\x9c\x84\xec\x97\x90 \xec\x84\xa0\xec\x96\xb8 \xed\x8f\xad\xec\x9d\x84 \xec\x8b\xa3\xeb\x8a\x94\xeb\x8b\xa4 */\n"
"    if (op == 5 || op == 6) {   // RFC-0053 E5: sum_neumaier(보정) / sum_seq(축차)\n"
"        if (v.tag != LWV_VARRAY) lw_panic(\"sum needs a typed array (view_array)\");\n"
"        double acc = 0.0, comp = 0.0;\n"
"        for (size_t k = 0; k < v.n; k++) {\n"
"            unsigned long long raw = 0; const unsigned char *pb = v.p + k * (size_t)v.box;\n"
"            for (int b = v.box; b-- > 0; ) raw = (raw << 8) | pb[b];\n"
"            double x = v.i ? lw_b2f(raw, v.box) : (double)(long long)raw;\n"
"            if (op == 6) { acc += x; continue; }\n"
"            double t = acc + x;\n"
"            comp += (fabs(acc) >= fabs(x)) ? (acc - t) + x : (x - t) + acc;\n"
"            acc = t; }\n"
"        return lw_flt(op == 6 ? acc : acc + comp); }\n"
"    if (op == 4) {   // D6: 전제조건을 타입에 기록한다\n"
"        if (v.tag != LWV_INT) lw_panic(\"nonzero_of needs an integer\");\n"
"        if (v.i == 0) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
"        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"        lw_boxes[lw_nbox] = v;\n"
"        lowv r = {0}; r.tag = LWV_SOME; r.box = lw_nbox++; return r; }\n"
"    if (v.tag == LWV_INT && op != 0) {\n"
"        if (op == 1) {   /* abs(\xed\x8f\xad-MIN) = +2^(N-1) \xec\x9d\x80 \xea\xb7\xb8 \xed\x8f\xad\xec\x97\x90 \xec\x95\x88 \xeb\xa7\x9e\xeb\x8a\x94\xeb\x8b\xa4 */\n"
"            if ((meta & 0x200000) && (meta & 0x400000)) {\n"
"                int wb = (int)((meta >> 24) & 0xff);\n"
"                if (wb && v.i == lw_lo(wb, 1)) lw_panic(\"abs overflow: |MIN| does not fit the declared width\");\n"
"            }\n"
"            return lw_int(v.i < 0 ? (long long)(0ull - (unsigned long long)v.i) : v.i); }\n"
"        return v; }\n"
"    if (v.tag != LWV_FLT) lw_panic(\"sqrt is float-only: convert explicitly\");\n"
"    double x = lw_fval(v);\n"
"    return lw_flt(op == 0 ? sqrt(x) : op == 1 ? fabs(x) : op == 2 ? floor(x) : ceil(x)); }\n"
"static lowv lw_binm(long long op, lowv a, lowv b) {   // 0 fmod 1 min 2 max\n"
"    if (a.tag == LWV_INT && b.tag == LWV_INT && op != 0)\n"
"        return lw_int(op == 1 ? (a.i < b.i ? a.i : b.i) : (a.i > b.i ? a.i : b.i));\n"
"    if (a.tag != LWV_FLT || b.tag != LWV_FLT)\n"
"        lw_panic(op == 0 ? \"fmod is float-only (use rem/mod for integers)\" : \"min/max need two numbers of one kind\");\n"
"    double x = lw_fval(a), y = lw_fval(b);\n"
"    return lw_flt(op == 0 ? fmod(x, y) : op == 1 ? (x < y ? x : y)\n"
"                : op == 2 ? (x > y ? x : y) : pow(x, y)); }\n"
"static lowv lw_cast(long long meta, lowv v) {\n"
"    if (meta == 0) { if (v.tag == LWV_INT) return lw_flt((double)v.i); if (v.tag == LWV_FLT) return v;\n"
"        lw_panic(\"cast needs a scalar\"); }\n"
"    int nb = (int)(meta & 0xff) * 8;\n"
"    int sgn = (meta & 0x20000) != 0;\n"   // ★ widen 은 대상 부호를 싣는다 → 부호 범위(전역성). narrow 는 무부호(손실 트랩)
"    long long x;\n"
"    if (v.tag == LWV_INT) x = v.i;\n"
"    else if (v.tag == LWV_FLT) x = (long long)lw_fval(v);\n"
"    else { lw_panic(\"cast needs a scalar\"); return lw_int(0); }\n"
"    int fits = (nb >= 64 || lw_fits(x, nb, sgn));\n"
"    if (meta & 0x40000) {   // narrow_try → option\n"
"        if (!fits) { lowv n = {0}; n.tag = LWV_NONE; return n; }\n"
"        if (lw_nbox >= lw_boxlim) lw_panic(\"box pool exhausted — raise it with -DLW_BOXPOOL=N (docs/runtime-pools.md)\");\n"
"        lw_boxes[lw_nbox] = lw_int(x);\n"
"        lowv r2 = {0}; r2.tag = LWV_SOME; r2.box = lw_nbox++; return r2; }\n"
"    if (fits) return lw_int(x);\n"
"    if (meta & 0x4000) return lw_int(lw_wrap(x, nb, sgn));\n"
"    if (meta & 0x8000) return lw_int(lw_satv(x, nb, sgn));\n"
"    lw_panic(\"value does not fit the target width\"); return lw_int(0); }\n"
// ★ 정수 캐스트의 **스칼라 핵** — `lowv` 판본이 이것을 감싼다(교훈 7: 계산은 한 곳에).
// ★★★ **비트 연산의 스칼라 핵** (RFC-0064) — **폭이 계약이다.**
//   C 는 `~(uint8_t)0` 을 **int 로 승격**해 `-1` 을 낸다. 여기서는 **선언된 폭에서** 뒤집는다.
//   그리고 **시프트 양 ≥ 폭은 C 에서 UB** 다 — 여기서는 **트랩**이다(계약이니까).
// ★★★ **`LW_HOT` 다** (WO-0177, 2026-09-03). 산술(`lw_arith_i`)·비교·적재는 인라인 힌트를 받는데 이것만
//   `static` 이라 **호출로 남았다** — 찌르개 churn 의 6.4% 가 이 함수였고, 인라인하니 명령 수가 **−18%**
//   (218.9M → 179.9M, run-2026-09-03-b). WO-0140 이 작은 op 에서 본 것과 같은 병이 헬퍼에도 있었다.
"LW_HOT long long lw_bit2(long long meta, int op, long long a, long long b) {\n"
"    int bits = (int)(meta & 0xff); int known = (meta & 0x1000) != 0; int sg = lw_ty_signed(meta);\n"
"    if (!known || !bits) bits = 64;\n"
"    unsigned long long mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);\n"
"    unsigned long long x = (unsigned long long)a & mask, r = 0;\n"
"    long long n = b;\n"
"    switch (op) {\n"
"      case 0: r = x & ((unsigned long long)b & mask); break;   /* bit_and */\n"
"      case 1: r = x | ((unsigned long long)b & mask); break;   /* bit_or  */\n"
"      case 2: r = x ^ ((unsigned long long)b & mask); break;   /* bit_xor */\n"
"      case 3: case 4:                                          /* shl · shr */\n"
"        if (n < 0 || n >= bits)\n"
"            lw_panic(\"the shift amount is not smaller than the type's width (in C this is UNDEFINED BEHAVIOUR; here it is a contract — say `wrap_shl`/`wrap_shr` if you want the masking)\");\n"
"        if (op == 3) r = (x << n) & mask;\n"
"        else if (sg) r = (unsigned long long)(lw_wrap((long long)x, bits, 1) >> n) & mask;\n"
"        else r = (x >> n) & mask;\n"
"        break;\n"
"      case 5: case 6: {                                        /* wrap_shl · wrap_shr */\n"
"        unsigned long long k = (unsigned long long)n & (unsigned long long)(bits - 1);\n"
"        if (op == 5) r = (x << k) & mask;\n"
"        else if (sg) r = (unsigned long long)(lw_wrap((long long)x, bits, 1) >> k) & mask;\n"
"        else r = (x >> k) & mask;\n"
"        break; }\n"
"      case 7: case 8: {                                        /* rotl · rotr */\n"
"        long long m = n % bits; if (m < 0) m += bits;\n"
"        unsigned long long k = (unsigned long long)m;\n"
"        if (!k) { r = x; break; }\n"
"        r = (op == 7) ? ((x << k) | (x >> (bits - k))) & mask\n"
"                      : ((x >> k) | (x << (bits - k))) & mask;\n"
"        break; }\n"
"      case 9: case 10: {                                       /* clmul_lo · clmul_hi */\n"
"        if (bits != 64) lw_panic(\"clmul is defined on 64-bit words only\");\n"
"        unsigned long long cl = 0, ch = 0; lw_clmul64(x, (unsigned long long)b, &cl, &ch);\n"
"        r = (op == 9) ? cl : ch; break; }\n"
"      default: lw_panic(\"bit op\");\n"
"    }\n"
"    return lw_wrap((long long)r, bits, sg); }\n"
"static long long lw_bit1(long long meta, int op, long long a) {\n"
"    int bits = (int)(meta & 0xff); int known = (meta & 0x1000) != 0; int sg = lw_ty_signed(meta);\n"
"    if (!known || !bits) bits = 64;\n"
"    unsigned long long mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);\n"
"    unsigned long long x = (unsigned long long)a & mask, r = 0;\n"
"    switch (op) {\n"
"      case 0: r = (~x) & mask; break;                          /* bit_not — **폭 안에서** */\n"
"      case 1: r = (unsigned long long)__builtin_popcountll(x); break;\n"
"      case 2: r = x ? (unsigned long long)(__builtin_clzll(x) - (64 - bits)) : (unsigned long long)bits; break;\n"
"      case 3: r = x ? (unsigned long long)__builtin_ctzll(x) : (unsigned long long)bits; break;\n"
"      case 4: { int nb = bits / 8;\n"
"        for (int b2 = 0; b2 < nb; b2++) r |= ((x >> (8 * b2)) & 0xffull) << (8 * (nb - 1 - b2));\n"
"        break; }\n"
"      default: lw_panic(\"bit op\");\n"
"    }\n"
"    if (op == 1 || op == 2 || op == 3) return (long long)r;\n"
"    return lw_wrap((long long)r, bits, sg); }\n"
"static long long lw_cast_i(long long meta, long long x) {\n"
"    int nb = (int)(meta & 0xff) * 8;\n"
"    int sgn = (meta & 0x20000) != 0;\n"   // ★ widen 은 대상 부호를 싣는다 → 부호 범위(전역성). narrow 는 무부호(손실 트랩)
"    int fits = (nb >= 64 || lw_fits(x, nb, sgn));\n"
"    if (fits) return x;\n"
"    if (meta & 0x4000) return lw_wrap(x, nb, sgn);\n"
"    if (meta & 0x8000) return lw_satv(x, nb, sgn);\n"
"    lw_panic(\"value does not fit the target width\"); return 0; }\n"
// ★ narrow_try 의 **스칼라 핵** — 값이 폭에 맞으면 1(some), 아니면 0(none). 폭은 안 바꾼다.
"static int lw_narrow_fits(long long meta, long long x) {\n"
"    int nb = (int)(meta & 0xff) * 8; return (nb >= 64) || lw_fits(x, nb, 0); }\n"
"static lowv lw_landor(int isor, lowv a, lowv b) {\n"
"    if (a.tag == LWV_MASK && b.tag == LWV_MASK) { if (a.box != b.box) lw_panic(\"mask lane counts differ\");\n"
"        lowv m = {0}; m.tag = LWV_MASK; m.box = a.box;\n"
"        m.i = isor ? (a.i | b.i) : (a.i & b.i); return m; }\n"
"    long long x = lw_want_int(a, \"ints\"), y = lw_want_int(b, \"ints\");\n"
"    return lw_int(isor ? (x != 0 || y != 0) : (x != 0 && y != 0)); }\n"
"static lowv lw_splat(long long meta, lowv x) { int s = lw_vecslot();\n"
"    int lanes = (int)((meta >> 8) & 0xff), esz = (int)(meta & 0xff), fl = (meta & 0x10000) ? 1 : 0;\n"
"    int sg = (meta & 0x20000) ? 1 : 0;   /* \xe2\x98\x85 \xeb\xb6\x80\xed\x98\xb8\xed\x98\x95 \xeb\xa0\x88\xec\x9d\xb8(vec iN) */\n"
"    for (int k = 0; k < lanes; k++)\n"
"        lw_vecs[s].l[k] = fl ? lw_f2b(x.tag == LWV_FLT ? lw_fval(x) : (double)lw_want_int(x, \"splat\"), esz)\n"
"                             : ((unsigned long long)lw_want_int(x, \"splat\") & lw_emask(esz));\n"
"    lw_vecs[s].n = lanes; lw_vecs[s].esz = esz; lw_vecs[s].flt = fl; lw_vecs[s].sign = sg; return lw_vecv(s); }\n"
"static lowv lw_vload(long long meta, lowv src, lowv iv) { int s = lw_vecslot();\n"
"    int lanes = (int)((meta >> 8) & 0xff), esz = (int)(meta & 0xff), fl = (meta & 0x10000) ? 1 : 0;\n"
"    int sg = (meta & 0x20000) ? 1 : 0;   /* \xe2\x98\x85 \xeb\xb6\x80\xed\x98\xb8\xed\x98\x95 \xeb\xa0\x88\xec\x9d\xb8(vec iN) */\n"
"    lw_vecs[s].flt = fl; lw_vecs[s].sign = sg;\n"
"    long long i0 = lw_want_int(iv, \"index\");\n"
"    const unsigned char *base; size_t limit;\n"
"    if (src.tag == LWV_VARRAY && src.box == esz) { base = src.p; limit = src.n; }\n"
"    else if (src.tag == LWV_SLICE) { base = src.p; limit = src.n / (size_t)esz; }\n"
"    else { lw_panic(\"load needs a matching typed array or byte slice\"); return lw_int(0); }\n"
"    if (i0 < 0 || (size_t)i0 + (size_t)lanes > limit) lw_panic(\"vector load out of bounds\");\n"
"    for (int k = 0; k < lanes; k++) { unsigned long long x = 0;\n"
"        const unsigned char *p = base + ((size_t)i0 + (size_t)k) * (size_t)esz;\n"
"        for (int b = esz; b-- > 0; ) x = (x << 8) | p[b];\n"
"        lw_vecs[s].l[k] = x; }\n"
"    lw_vecs[s].n = lanes; lw_vecs[s].esz = esz; return lw_vecv(s); }\n"
/* ★ store — VLOAD 의 역. 레인/esz 는 vec 이 실어 온다(meta 없음). VM 과 **같은 바이트**를 쓴다(diff-sweep). */
"static lowv lw_vstore(lowv dst, lowv iv, lowv v) {\n"
"    if (v.tag != LWV_VEC) lw_panic(\"store needs a vector\");\n"
"    int lanes = lw_vecs[v.box].n, esz = lw_vecs[v.box].esz;\n"
"    long long i0 = lw_want_int(iv, \"index\");\n"
"    unsigned char *base; size_t limit;\n"
"    if (dst.tag == LWV_VARRAY && dst.box == esz) { base = (unsigned char *)dst.p; limit = dst.n; }\n"
"    else if (dst.tag == LWV_SLICE) { base = (unsigned char *)dst.p; limit = dst.n / (size_t)esz; }\n"
"    else { lw_panic(\"store needs a matching typed array or byte slice\"); return lw_int(0); }\n"
"    if (i0 < 0 || (size_t)i0 + (size_t)lanes > limit) lw_panic(\"vector store out of bounds\");\n"
"    for (int k = 0; k < lanes; k++) { unsigned long long x = lw_vecs[v.box].l[k];\n"
"        unsigned char *p = base + ((size_t)i0 + (size_t)k) * (size_t)esz;\n"
"        for (int b = 0; b < esz; b++) p[b] = (unsigned char)((x >> (8 * b)) & 0xff); }\n"
"    return lw_int(0); }\n"
/* ★ masked load (merge) — 켜진 lane 만 메모리 읽고(그 lane 은 경계 안 요구), 꺼진 lane = passthrough. VM 과 같은 바이트. */
"static lowv lw_vloadm(long long meta, lowv src, lowv iv, lowv m, lowv pv) { int s = lw_vecslot();\n"
"    int lanes = (int)((meta >> 8) & 0xff), esz = (int)(meta & 0xff), fl = (meta & 0x10000) ? 1 : 0;\n"
"    int sg = (meta & 0x20000) ? 1 : 0;   /* \xe2\x98\x85 \xeb\xb6\x80\xed\x98\xb8\xed\x98\x95 \xeb\xa0\x88\xec\x9d\xb8(vec iN) */\n"
"    if (m.tag != LWV_MASK || pv.tag != LWV_VEC) lw_panic(\"load_masked needs a mask + a passthrough vector\");\n"
"    if (m.box != lanes || lw_vecs[pv.box].n != lanes) lw_panic(\"load_masked mask/passthrough lane count != vec lanes\");\n"
"    lw_vecs[s].flt = fl; lw_vecs[s].sign = sg;\n"
"    long long i0 = lw_want_int(iv, \"index\");\n"
"    const unsigned char *base; size_t limit;\n"
"    if (src.tag == LWV_VARRAY && src.box == esz) { base = src.p; limit = src.n; }\n"
"    else if (src.tag == LWV_SLICE) { base = src.p; limit = src.n / (size_t)esz; }\n"
"    else { lw_panic(\"load_masked needs a matching typed array or byte slice\"); return lw_int(0); }\n"
"    for (int k = 0; k < lanes; k++) {\n"
"        if (!((m.i >> k) & 1)) { lw_vecs[s].l[k] = lw_vecs[pv.box].l[k]; continue; }\n"
"        if (i0 < 0 || (size_t)i0 + (size_t)k >= limit) lw_panic(\"load_masked active lane out of bounds\");\n"
"        unsigned long long x = 0; const unsigned char *p = base + ((size_t)i0 + (size_t)k) * (size_t)esz;\n"
"        for (int b = esz; b-- > 0; ) x = (x << 8) | p[b];\n"
"        lw_vecs[s].l[k] = x; }\n"
"    lw_vecs[s].n = lanes; lw_vecs[s].esz = esz; return lw_vecv(s); }\n"
/* ★ masked store — 켜진 lane 만 쓰고(그 lane 은 경계 안 요구) 나머지 보존. VM 과 같은 바이트. */
"static lowv lw_vstorem(lowv dst, lowv iv, lowv v, lowv m) {\n"
"    if (v.tag != LWV_VEC || m.tag != LWV_MASK) lw_panic(\"store_masked needs a vector + a mask\");\n"
"    int lanes = lw_vecs[v.box].n, esz = lw_vecs[v.box].esz;\n"
"    if (m.box != lanes) lw_panic(\"store_masked mask lane count != vec lanes\");\n"
"    long long i0 = lw_want_int(iv, \"index\");\n"
"    unsigned char *base; size_t limit;\n"
"    if (dst.tag == LWV_VARRAY && dst.box == esz) { base = (unsigned char *)dst.p; limit = dst.n; }\n"
"    else if (dst.tag == LWV_SLICE) { base = (unsigned char *)dst.p; limit = dst.n / (size_t)esz; }\n"
"    else { lw_panic(\"store_masked needs a matching typed array or byte slice\"); return lw_int(0); }\n"
"    for (int k = 0; k < lanes; k++) {\n"
"        if (!((m.i >> k) & 1)) continue;\n"
"        if (i0 < 0 || (size_t)i0 + (size_t)k >= limit) lw_panic(\"store_masked active lane out of bounds\");\n"
"        unsigned long long x = lw_vecs[v.box].l[k];\n"
"        unsigned char *p = base + ((size_t)i0 + (size_t)k) * (size_t)esz;\n"
"        for (int b = 0; b < esz; b++) p[b] = (unsigned char)((x >> (8 * b)) & 0xff); }\n"
"    return lw_int(0); }\n"
/* ★ D5 target intrinsic `avg` — 레인별 라운딩 평균 (a+b+1)>>1 (x86 pavgb/pavgw · ARM vrhadd).
   widening 이라 portable 합성 불가(u8 레인에서 a+b 가 넘친다) — 그래서 특수 명령이다.
   VM 핸들러와 **같은 산술** ⇒ diff-sweep 로 VM≡native (비트-정확 intrinsic 이라 오라클 안에 남는다). */
"static lowv lw_vavg(lowv a, lowv b) {\n"
"    if (a.tag != LWV_VEC || b.tag != LWV_VEC) lw_panic(\"avg needs two vectors\");\n"
"    int n = lw_vecs[a.box].n, esz = lw_vecs[a.box].esz;\n"
"    if (lw_vecs[b.box].n != n || lw_vecs[b.box].esz != esz) lw_panic(\"avg lane counts/widths differ\");\n"
"    if (lw_vecs[a.box].flt || lw_vecs[b.box].flt) lw_panic(\"avg is an INTEGER intrinsic\");\n"
"    unsigned long long em = esz >= 8 ? ~0ull : ((1ull << (8 * esz)) - 1);\n"
"    int s = lw_vecslot();\n"
"    for (int k = 0; k < n; k++) { unsigned long long x = lw_vecs[a.box].l[k] & em, y = lw_vecs[b.box].l[k] & em;\n"
"        lw_vecs[s].l[k] = ((x + y + 1ull) >> 1) & em; }\n"
"    lw_vecs[s].n = n; lw_vecs[s].esz = esz; lw_vecs[s].flt = 0; lw_vecs[s].sign = lw_vecs[a.box].sign; return lw_vecv(s); }\n"
"static lowv lw_vselect(lowv m, lowv a, lowv b) {\n"
"    if (m.tag != LWV_MASK || a.tag != LWV_VEC || b.tag != LWV_VEC) lw_panic(\"select needs mask + two vectors\");\n"
"    int n = lw_vecs[a.box].n; if (lw_vecs[b.box].n != n || m.box != n) lw_panic(\"select lane counts differ\");\n"
"    int s = lw_vecslot();\n"
"    for (int k = 0; k < n; k++) lw_vecs[s].l[k] = ((m.i >> k) & 1) ? lw_vecs[a.box].l[k] : lw_vecs[b.box].l[k];\n"
"    lw_vecs[s].n = n; lw_vecs[s].esz = lw_vecs[a.box].esz; lw_vecs[s].flt = lw_vecs[a.box].flt; lw_vecs[s].sign = lw_vecs[a.box].sign; return lw_vecv(s); }\n"
/* ★ 레인 재배열 — reverse/rotate/shuffle. 새 vec 을 낸다. VM 핸들러와 **같은 순열** ⇒ diff-sweep 로 VM≡native. */
"static lowv lw_vperm(int mode, long long meta, lowv v) {\n"   /* 0 reverse · 1 rotate · 2 shuffle */
"    if (v.tag != LWV_VEC) lw_panic(\"reverse/rotate/shuffle needs a vector\");\n"
"    int n = lw_vecs[v.box].n, s = lw_vecslot();\n"
"    if (mode == 2) { int cnt = (int)((meta >> 32) & 0xf); if (cnt != n) lw_panic(\"shuffle index count != lane count\"); }\n"
"    for (int k = 0; k < n; k++) { int src;\n"
"        if (mode == 0) src = n - 1 - k;\n"
"        else if (mode == 1) src = (int)(((unsigned long long)k + (unsigned long long)meta) % (unsigned)n);\n"
"        else { src = (int)((meta >> (4 * k)) & 0xf); if (src >= n) lw_panic(\"shuffle lane index out of range\"); }\n"
"        lw_vecs[s].l[k] = lw_vecs[v.box].l[src]; }\n"
"    lw_vecs[s].n = n; lw_vecs[s].esz = lw_vecs[v.box].esz; lw_vecs[s].flt = lw_vecs[v.box].flt; lw_vecs[s].sign = lw_vecs[v.box].sign; return lw_vecv(s); }\n"
"static lowv lw_vreduce(int op, lowv v) {   // 0 add 1 mul 2 min 3 max\n"
"    if (v.tag != LWV_VEC) lw_panic(\"reduce needs a vector\");\n"
"    int n = lw_vecs[v.box].n, esz = lw_vecs[v.box].esz;\n"
"    if (lw_vecs[v.box].flt) { double acc = op == 1 ? 1.0 : 0.0;\n"
"        for (int k = 0; k < n; k++) { double x = lw_b2f(lw_vecs[v.box].l[k], esz);\n"
"            if (k == 0 && (op == 2 || op == 3)) { acc = x; continue; }\n"
"            acc = op == 0 ? acc + x : op == 1 ? acc * x : op == 2 ? (x < acc ? x : acc) : (x > acc ? x : acc); }\n"
"        return lw_flt(acc); }\n"
"    if (lw_vecs[v.box].sign) {   /* \xe2\x98\x85 \xeb\xb6\x80\xed\x98\xb8\xed\x98\x95 \xeb\xa0\x88\xec\x9d\xb8\xec\x9d\x80 \xeb\xb6\x80\xed\x98\xb8\xeb\xa1\x9c \xec\xa0\x91\xeb\x8a\x94\xeb\x8b\xa4 */\n"
"        long long sa = op == 1 ? 1 : 0; int first = 1;\n"
"        for (int k = 0; k < n; k++) { long long x = lw_lsx(lw_vecs[v.box].l[k], esz);\n"
"            if (op == 2 || op == 3) { if (first) { sa = x; first = 0; }\n"
"                else sa = op == 2 ? (x < sa ? x : sa) : (x > sa ? x : sa); continue; }\n"
"            sa = op == 0 ? sa + x : sa * x; }\n"
"        return lw_int(sa); }\n"
"    unsigned long long acc = op == 1 ? 1ull : op == 2 ? ~0ull : 0ull;\n"
"    for (int k = 0; k < n; k++) { unsigned long long x = lw_vecs[v.box].l[k];\n"
"        acc = op == 0 ? acc + x : op == 1 ? acc * x : op == 2 ? (x < acc ? x : acc) : (x > acc ? x : acc); }\n"
"    return lw_int((long long)acc); }\n"
"static lowv lw_mred(int isall, lowv m) {\n"
"    if (m.tag != LWV_MASK) lw_panic(\"any/all needs a mask\");\n"
"    unsigned long long lm = m.box >= 64 ? ~0ull : ((1ull << m.box) - 1);\n"
"    unsigned long long bits = (unsigned long long)m.i & lm;\n"
"    return lw_int(isall ? bits == lm : bits != 0); }\n"
"static lowv lw_not(lowv a) {\n"
"    if (a.tag == LWV_MASK) { unsigned long long lm = a.box >= 64 ? ~0ull : ((1ull << a.box) - 1);\n"
"        lowv m = {0}; m.tag = LWV_MASK; m.box = a.box; m.i = (long long)(~(unsigned long long)a.i & lm); return m; }\n"
"    return lw_int(!lw_want_int(a, \"ints\")); }\n"
"static lowv lw_ref(lowv *slot, int mut) { lowv r = {0}; r.tag = LWV_REF; r.q = slot; r.i = mut ? LW_REF_MUT : 0; return r; }\n"
"static lowv lw_deref(lowv r) { if (r.tag != LWV_REF) lw_panic(\"deref needs a reference\"); return *r.q; }\n"
"static void lw_store(lowv *slot, lowv v) {\n"
"    if (slot->tag == LWV_REF && v.tag != LWV_REF) {   // G7 write-through\n"
"        if (!(slot->i & LW_REF_MUT)) lw_panic(\"write through a shared ref (needs mut_ref)\");\n"
"        *slot->q = v;\n"
"    } else *slot = v;\n"
"}\n"
"static lowv lw_contains(lowv s, lowv x) { if (s.tag != LWV_BITSET) lw_panic(\"contains needs a bitset\");\n"
"    long long k = lw_want_int(x, \"element\"); if (k < 0 || k >= lw_bwid[s.box]) lw_panic(\"bitset element out of range\");\n"
"    return lw_int((long long)((lw_bsets[s.box] >> k) & 1u)); }\n"
"static lowv lw_count(lowv v) { if (v.tag == LWV_STACK) return lw_int((long long)lw_stks[v.box].n);\n"
"    if (v.tag != LWV_BITSET) lw_panic(\"count needs a stack/bitset\");\n"
"    unsigned long long bits = lw_bsets[v.box]; long long k = 0; while (bits) { bits &= bits - 1; k++; } return lw_int(k); }\n"
"static void lw_render(lowv v, char *out, size_t cap);\n";

// rendering + main are emitted after the tables (they reference lw_errs/lw_mk_*)
const char LW_RENDER[] =
"static void lw_render(lowv v, char *out, size_t cap) {\n"
"    switch (v.tag) {\n"
"        case LWV_INT: snprintf(out, cap, \"%lld\", v.i); return;\n"
"        case LWV_FLT: lw_fmt_f(out, cap, lw_fval(v)); return;\n"
"        case LWV_SLICE: { size_t o = (size_t)snprintf(out, cap, \"[\");\n"
"            for (size_t i = 0; i < v.n && o + 8 < cap; i++)\n"
"                o += (size_t)snprintf(out + o, cap - o, \"%s%u\", i ? \" \" : \"\", (unsigned)v.p[i]);\n"
"            snprintf(out + o, cap - o, \"]\"); return; }\n"
"        case LWV_REC: { lowrec *r = &lw_recs[v.box];\n"
"            size_t o = (size_t)snprintf(out, cap, \"{\");\n"
"            for (int i = 0; i < r->nf && o + 24 < cap; i++) { char fv[48];\n"
"                lw_render(r->f[i], fv, sizeof fv);\n"
"                o += (size_t)snprintf(out + o, cap - o, \"%s%s %s\", i ? \", \" : \"\", lw_mk_fields[r->mk][i], fv); }\n"
"            snprintf(out + o, cap - o, \"}\"); return; }\n"
"        case LWV_OK: { char pv[96]; lw_render(lw_boxes[v.box], pv, sizeof pv);\n"
"            snprintf(out, cap, \"ok %s\", pv); return; }\n"
"        case LWV_ERR: snprintf(out, cap, \"err %s\", lw_errs[v.i]); return;\n"
"        case LWV_VIEW: { size_t o = (size_t)snprintf(out, cap, \"{\");\n"
"            for (int i = 0; i < lw_st_nf[v.box] && o + 24 < cap; i++)\n"
"                o += (size_t)snprintf(out + o, cap - o, \"%s%s %llu\", i ? \", \" : \"\",\n"
"                                      lw_st_fname[v.box][i], lw_vread(v, i));\n"
"            snprintf(out + o, cap - o, \"}\"); return; }\n"
"        case LWV_SOME: { char pv[96]; lw_render(lw_boxes[v.box], pv, sizeof pv);\n"
"            snprintf(out, cap, \"some %s\", pv); return; }\n"
"        case LWV_NONE: snprintf(out, cap, \"none\"); return;\n"
"        case LWV_REF: snprintf(out, cap, \"%s\", (v.i & LW_REF_MUT) ? \"<mut_ref>\" : \"<ref>\"); return;\n"
"        case LWV_VEC: { size_t o = (size_t)snprintf(out, cap, \"vec[\");\n"
"            for (int k = 0; k < lw_vecs[v.box].n && o + 24 < cap; k++)\n"
"                o += lw_vecs[v.box].flt\n"
"                   ? (size_t)snprintf(out + o, cap - o, \"%s%g\", k ? \" \" : \"\", lw_b2f(lw_vecs[v.box].l[k], lw_vecs[v.box].esz))\n"
"                   : (size_t)snprintf(out + o, cap - o, \"%s%llu\", k ? \" \" : \"\", lw_vecs[v.box].l[k]);\n"
"            snprintf(out + o, cap - o, \"]\"); return; }\n"
"        case LWV_MASK: { size_t o = (size_t)snprintf(out, cap, \"mask[\");\n"
"            for (int k = 0; k < v.box && o + 4 < cap; k++)\n"
"                o += (size_t)snprintf(out + o, cap - o, \"%s%u\", k ? \" \" : \"\", (unsigned)(((unsigned long long)v.i >> k) & 1));\n"
"            snprintf(out + o, cap - o, \"]\"); return; }\n"
"        case LWV_VARRAY: { size_t o = (size_t)snprintf(out, cap, \"[\");\n"
"            for (size_t i = 0; i < v.n && o + 24 < cap; i++) {\n"
"                unsigned long long x = 0; const unsigned char *p = v.p + i * (size_t)v.box;\n"
"                for (int b = v.box; b-- > 0; ) x = (x << 8) | p[b];\n"
"                o += v.i ? (size_t)snprintf(out + o, cap - o, \"%s%g\", i ? \" \" : \"\", lw_b2f(x, v.box))\n"
"                         : (size_t)snprintf(out + o, cap - o, \"%s%llu\", i ? \" \" : \"\", x); }\n"
"            snprintf(out + o, cap - o, \"]\"); return; }\n"
"        case LWV_STACK: snprintf(out, cap, \"stack(%zu)\", lw_stks[v.box].n); return;\n"
"        case LWV_BITSET: { unsigned long long bits = lw_bsets[v.box]; long long k = 0;\n"
"            while (bits) { bits &= bits - 1; k++; }\n"
"            snprintf(out, cap, \"bitset(%lld)\", k); return; }\n"
"    }\n"
"    snprintf(out, cap, \"?\");\n"
"}\n";

// ★ 이 디스패처의 **본문은 하나**다. 이름만 둘이다: 엔트리 op 이 없으면 `main`,
//   있으면 `lw_dispatch`(그러면 `main` 은 프로그램이 되고, 인자가 오면 이리로 넘긴다).
//   본문을 두 벌 만들면 그것이 곧 여덟 번째 갈림이다(교훈 7).
const char LW_DISPATCH[] =
"static int lw_dispatch(int argc, char **argv) {\n"
"    if (argc < 2) { fprintf(stderr, \"usage: %s <op> [args...]   (a slice arg is written [a,b,c])\\n\", argv[0]); return 2; }\n"
"    const struct lw_entry *e = 0;\n"
"    for (size_t i = 0; i < sizeof lw_ops / sizeof lw_ops[0]; i++)\n"
"        if (strcmp(lw_ops[i].name, argv[1]) == 0) { e = &lw_ops[i]; break; }\n"
"    if (!e) { fprintf(stderr, \"no such op: %s\\n\", argv[1]); return 1; }\n"
"    /* an argument is either an int or a slice written [a,b,c]. The flat form (bare ints,\n"
"       a single trailing slice eating the rest) still works; it cannot express TWO slices,\n"
"       which is the shape of every write op — encode(src, mut dst). */\n"
"    lowv args[64]; int nargs = 0, any_bracket = 0;\n"
"    long long ints[64]; int nints = 0;\n"
"    alignas(64) static unsigned char sbufs[16][8192]; int nsb = 0;\n"
"    for (int i = 2; i < argc && nargs < 64; i++) {\n"
"        /* `@file` — a slice argument read from a FILE (X-0034). The bracket form lives in a\n"
"           static 8 KiB cell, and that ceiling was deciding how libraries lay out their\n"
"           workspaces. The bytes of the file ARE the slice; nothing is parsed.\n"
"           ★ It is OFF by default, and that is not timidity: this repository promises that a\n"
"           feature you do not use costs you NOTHING (a gate reads `nm -u` at -O0 and refuses\n"
"           an object that names `fopen` when the program never opens a file). Reading argument\n"
"           files from the dispatcher would put `fopen` into EVERY emitted program. So it is\n"
"           compiled in on request: `cc -DLW_ARGFILE …`. The VM always accepts `@file`. */\n"
"        if (argv[i][0] == '@' && argv[i][1]) {\n"
"#ifndef LW_ARGFILE\n"
"            fprintf(stderr, \"this build was compiled without @file arguments — rebuild the "
"emitted C with -DLW_ARGFILE (it is off by default so a program that never opens a file does "
"not link fopen)\\n\");\n"
"            return 2;\n"
"#else\n"
"            FILE *af = fopen(argv[i] + 1, \"rb\");\n"
"            if (!af) { fprintf(stderr, \"cannot read argument file: %s\\n\", argv[i] + 1); return 2; }\n"
"            if (fseek(af, 0, SEEK_END) != 0) { fclose(af); fprintf(stderr, \"not seekable: %s\\n\", argv[i] + 1); return 2; }\n"
"            long alen = ftell(af); rewind(af);\n"
"            if (alen < 0 || alen > 16L * 1024L * 1024L) { fclose(af); fprintf(stderr, \"argument file too big: %s\\n\", argv[i] + 1); return 2; }\n"
"            unsigned char *ab = (unsigned char *)calloc((size_t)(alen ? alen : 1), 1);\n"
"            if (!ab) { fclose(af); fprintf(stderr, \"out of memory for %s\\n\", argv[i] + 1); return 2; }\n"
"            if (alen > 0 && fread(ab, 1, (size_t)alen, af) != (size_t)alen) { fclose(af); free(ab); fprintf(stderr, \"short read: %s\\n\", argv[i] + 1); return 2; }\n"
"            fclose(af);\n"
"            any_bracket = 1;\n"
"            lowv s = {0}; s.tag = LWV_SLICE; s.p = ab; s.n = (size_t)alen;\n"
"            args[nargs++] = s;\n"
"#endif\n"
"        } else if (argv[i][0] == '[' && nsb < 16) {\n"
"            any_bracket = 1; size_t n = 0; const char *p = argv[i] + 1; int over = 0;\n"
"            while (*p && *p != ']') {\n"
"                if (*p == ',' || *p == ' ') { p++; continue; }\n"
"                char *q = 0; long long v = strtoll(p, &q, 10);\n"
"                if (q == p) break;\n"
"                /* the VM refuses instead of truncating; this copy USED TO TRUNCATE SILENTLY,\n"
"                   which is the same lie in the other back end (the two must answer alike). */\n"
"                if (n >= 8192) { over = 1; break; }\n"
"                sbufs[nsb][n++] = (unsigned char)v; p = q;\n"
"            }\n"
"            if (over) { fprintf(stderr, \"argument %d has more than 8192 elements — refusing rather than truncating (use @file)\\n\", i - 1); return 2; }\n"
"            lowv s = {0}; s.tag = LWV_SLICE; s.p = sbufs[nsb]; s.n = n;\n"
"            args[nargs++] = s; nsb++;\n"
"        } else if ((strchr(argv[i], '.') || strchr(argv[i], 'e') || strchr(argv[i], 'E')) &&\n"
"                   ({ char *e_ = 0; strtod(argv[i], &e_); e_ && *e_ == 0; })) {\n"
"            any_bracket = 1;   /* a f64 arg — `1e10` used to be read as the int 1, SILENTLY */\n"
"            args[nargs++] = lw_flt(strtod(argv[i], 0));\n"
"        } else {\n"
"            /* the upper half of u64 is an argument too (defect #19): strtoll CLIPPED it */\n"
"            char *e_ = 0; long long v = (argv[i][0] == '-') ? strtoll(argv[i], &e_, 10)\n"
"                                                            : (long long)strtoull(argv[i], &e_, 10);\n"
"            /* an argument the tool cannot read must SAY SO — it used to be read as 0/1 silently */\n"
"            if (!e_ || *e_ != 0) { fprintf(stderr, \"cannot read argument: %s\\n\", argv[i]); return 2; }\n"
"            if (nints < 64) ints[nints++] = v;\n"
"            args[nargs++] = lw_int(v);\n"
"        }\n"
"    }\n"
"    /* a `cap` parameter is a STATIC token, not a value: nobody writes it on the command line.\n"
"       Fill those slots here so an op that takes one can be RUN — until 2026-09-19 only `main`\n"
"       could, so every witness that takes `cap io`/`cap allocator` was runnable in ONE back end\n"
"       and the two-back-end oracle was silently broken for it (X-0033). */\n"
"    /* fill ONLY when arguments are MISSING: the old rule put CLI args into parameter slots\n"
"       positionally, cap slots included, and 695 existing calls (plus the differential sweep)\n"
"       depend on it. Filling unconditionally made the two back ends disagree (measured). */\n"
"    if (e->pcap && nargs < e->nparams) {\n"
"        lowv filled[64]; int k = 0;\n"
"        for (int i = 0; i < e->nparams && i < 64; i++) {\n"
"            if ((e->pcap >> i) & 1u) { lowv z = {0}; filled[i] = z; continue; }\n"
"            if (k >= nargs) { fprintf(stderr, \"too few arguments\\n\"); return 1; }\n"
"            filled[i] = args[k++];\n"
"        }\n"
"        if (k != nargs) { fprintf(stderr, \"argument count does not match op arity\\n\"); return 1; }\n"
"        for (int i = 0; i < e->nparams && i < 64; i++) args[i] = filled[i];\n"
"        nargs = e->nparams;\n"
"    }\n"
"    /* the program boundary is the last door: a declared parameter range must be checked\n"
"       here, because the interval analysis already TRUSTED it and removed checks inside. */\n"
"    /* an f32 parameter is rounded HERE — the same door the VM rounds at (defect #83):\n"
"       otherwise a value that came from outside keeps f64 precision and the same type\n"
"       holds two different numbers depending on where the value came from. */\n"
"    for (int i = 0; i < e->nparams && i < 16 && i < nargs; i++)\n"
"        if (((e->pf32 >> i) & 1u) && args[i].tag == LWV_FLT) {\n"
"            double d_; memcpy(&d_, &args[i].i, 8); d_ = (double)(float)d_; memcpy(&args[i].i, &d_, 8);\n"
"        }\n"
"    for (int i = 0; i < e->nparams && i < 16 && i < nargs; i++) {\n"
"        /* the TYPE's range is a contract too: the analysis trusts it and DELETES checks. */\n"
"        if (e->ptype[i].has && args[i].tag == LWV_INT &&\n"
"            (args[i].i < e->ptype[i].lo || args[i].i > e->ptype[i].hi))\n"
"            lw_panic(\"a parameter's TYPE range was violated at the program boundary\");\n"
"        if (e->prng[i].has && args[i].tag == LWV_INT &&\n"
"            (args[i].i < e->prng[i].lo || args[i].i > e->prng[i].hi))\n"
"            lw_panic(\"a parameter's declared range was violated at the program boundary\");\n"
"    }\n"
"    if (!any_bracket) {   /* flat form: one trailing slice eats the remaining ints */\n"
"        int tail_slice = e->nparams > 0 && e->pslice == (1u << (e->nparams - 1));\n"
"        if (tail_slice) {\n"
"            if (nints + 1 < e->nparams) { fprintf(stderr, \"too few arguments\\n\"); return 1; }\n"
"            for (int i = 0; i + 1 < e->nparams; i++) args[i] = lw_int(ints[i]);\n"
"            int n = nints - (e->nparams - 1);\n"
"            if (n > 256) n = 256;\n"
"            for (int i = 0; i < n; i++) sbufs[0][i] = (unsigned char)ints[e->nparams - 1 + i];\n"
"            lowv s = {0}; s.tag = LWV_SLICE; s.p = sbufs[0]; s.n = (size_t)n;\n"
"            args[e->nparams - 1] = s; nargs = e->nparams;\n"
"        }\n"
"    }\n"
"    if (nargs != e->nparams) { fprintf(stderr, \"argument count does not match op arity\\n\"); return 1; }\n"
"    for (int i = 0; i < e->nparams; i++)   /* an int written for a f64 param IS a f64 */\n"
"        if (((e->pflt >> i) & 1u) && args[i].tag == LWV_INT) args[i] = lw_flt((double)args[i].i);\n"
"    for (int i = 0; i < e->nparams; i++)\n"
"        if ((int)(((e->pslice | e->pstruct) >> i) & 1u) != (args[i].tag == LWV_SLICE)) {\n"
"            fprintf(stderr, \"argument %d: the op wants a %s here\\n\", i + 1,\n"
"                    ((e->pslice >> i) & 1u) ? \"slice — write it as [a,b,c]\" : \"scalar\");\n"
"            return 1;\n"
"        }\n""    /* a struct parameter takes bytes at the boundary and is VIEWed — same checks as `view`:\n"
"       short → panic, mis-aligned → panic. The boundary does not get a discount. */\n"
"    for (int i = 0; i < e->nparams; i++)\n"
"        if (((e->pstruct >> i) & 1u) && args[i].tag == LWV_SLICE) {\n"
"            int si = e->psidx[i];\n"
"            if (!lw_st_viewable[si])\n"
"                lw_panic(\"a struct parameter whose fields are not all sized scalars cannot be built from bytes at the boundary\");\n"
"            if (lw_st_align[si] > 1 && ((uintptr_t)args[i].p % (unsigned)lw_st_align[si]))\n"
"                lw_panic(\"struct parameter: the bytes violate the struct's align contract at the program boundary\");\n"
"            if ((int)args[i].n < lw_st_total[si])\n"
"                lw_panic(\"struct parameter: fewer bytes than the layout needs at the program boundary\");\n"
"            lowv v = {0}; v.tag = LWV_VIEW; v.p = args[i].p; v.n = args[i].n; v.box = si;\n"
"            args[i] = v;\n"
"        }\n"
"    lowv r = e->fn(args);\n"
"    char buf[160]; lw_render(r, buf, sizeof buf);\n"
"    /* an UNSIGNED result prints unsigned — the VM does the same (defect #69): the value was\n"
"       right and the printing was wrong, and a tool that says a number two ways is not believed. */\n"
"    if (e->ounsigned && r.tag == LWV_INT && r.i < 0) snprintf(buf, sizeof buf, \"%llu\", (unsigned long long)r.i);\n"
"    printf(\"%s(\", argv[1]);\n"
"    if (!any_bracket) {   /* the flat form echoes the ints as given — the VM does the same,\n"
"                             and the golden C-backend DIFF compares the two texts. */\n"
"        for (int i = 0; i < nints; i++)\n"
"            if (((e->pu64 >> i) & 1u) && ints[i] < 0) printf(\"%s%llu\", i ? \", \" : \"\", (unsigned long long)ints[i]);\n"
"            else printf(\"%s%lld\", i ? \", \" : \"\", ints[i]);\n"
"    } else for (int i = 0; i < nargs; i++) {\n"
"        if (i) printf(\", \");\n"
"        if (args[i].tag == LWV_FLT) { char fb[40]; lw_fmt_f(fb, sizeof fb, lw_fval(args[i])); printf(\"%s\", fb); continue; }\n"
"        if (args[i].tag != LWV_SLICE && args[i].tag != LWV_VIEW) { printf(\"%lld\", (long long)args[i].i); continue; }\n"
"        printf(\"[\");\n"
"        for (size_t j = 0; j < args[i].n; j++)\n"
"            printf(\"%s%u\", j ? \",\" : \"\", (unsigned)((const unsigned char *)args[i].p)[j]);\n"
"        printf(\"]\");\n"
"    }\n"
"    printf(\") = %s\\n\", buf);\n"
"    for (int i = 0; i < nargs && any_bracket; i++) {   /* show what a mut slice was WRITTEN to */\n"
"        if (args[i].tag != LWV_SLICE && args[i].tag != LWV_VIEW) continue;\n"
"        printf(\"  arg%d (written) = [\", i);\n"
"        for (size_t j = 0; j < args[i].n; j++)\n"
"            printf(\"%s%u\", j ? \",\" : \"\", (unsigned)((const unsigned char *)args[i].p)[j]);\n"
"        printf(\"]\\n\");\n"
"    }\n"
"    return 0;\n"
"}\n";

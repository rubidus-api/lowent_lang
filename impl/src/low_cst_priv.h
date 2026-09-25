#ifndef LOW_CST_PRIV_H
#define LOW_CST_PRIV_H
/* low_cst_priv.h — `low_cst.c` 와 `low_mono.c` 가 **나눠 갖는 것만** (WO-0168).
 * ★ 공개 표면이 아니다. 밖에서 include 하지 말 것.
 * ★★ 이 목록에서 **짧아야 하는 것은 「결합」이지 「어휘」가 아니다**(WO-0164 에서 배운 규칙). */
#include "low_cst.h"

/* ★ 단형화의 타입 파라미터 상한 — `mono_gen_t` 가 이 수로 칸을 잡으므로 **타입보다 먼저** 와야 한다. */
#define MONO_MAXT 4

typedef struct {
    const low_token_t  *toks;
    proven_size_t       n;
    proven_size_t       pos;
    proven_allocator_t  node_alloc;  // arena (nodes)
    proven_allocator_t  work;        // heap (temp arrays, diags, form list)
    low_parse_result_t *out;
    // ★★★ **`extern` 선언에는 본문이 없다** (RFC-0063) — 몸이 **C 에** 있으니까.
    //   그래서 `do … end` 를 요구하면 안 된다. 문맥 비트 하나로 그것을 말한다.
    bool in_extern;
    // ★ 2026-09-15 — trait 블록 안인가. 서명에는 `fn`/`proc` 을 적지 않는다(효과 줄이 정한다) — 적으면 분명히 말한다.
    bool in_trait;
    // ★ 2026-09-25 (소유자 결정 «end 는 자기 do 까지만») — 블록을 품은 **값**을 쓰는 문장(`let x be make T do … end .`)이
    //   `end` 뒤에 찍은 점은 그 문장의 닫개다. 그 점의 토큰 자리를 적어 두고, 나머지 `end` 뒤 점만 E-DOT-STRAY 로 거절한다.
    proven_array_t dot_ok;   // proven_size_t — 오름차순(파스가 왼쪽에서 오른쪽으로 간다)
} low_parser_t;

typedef struct {
    proven_u8str_view_t name;      // 제네릭 op 이름
    const low_cst_t    *form;      // 그 선언
    // ★★★ 타입 파라미터는 **여럿일 수 있다.** v1 은 **하나만** 치환했고, 나머지는 남아서
    //   `E-TYPE-UNDEF: 타입 u 가 선언되지 않았다` 라는 **오진**을 냈다 — 진짜 이유는
    //   *"단형화가 아직 하나만 지원한다"* 였다. **못 하는 것을 프로그램의 죄로 돌렸다**(교훈 5).
    proven_u8str_view_t tparam[MONO_MAXT];
    proven_size_t       nt;
    proven_size_t       np;        // 전체 파라미터 수(타입 파라미터 포함)
    // ★★★ **어느 모듈의 틀인가** (2026-07-26). 이게 없으면 같은 이름의 op 이 두 모듈에 있을 때
    //   한정 호출(`growvec.view_of g`)이 **남의 제네릭 틀**과 매칭되어, 첫 인자가 타입 인자로
    //   먹히고 `view_of#g` 같은 유령 인스턴스가 생긴다. 자리가 곧 모듈 소속이다.
    proven_u8str_view_t mod;
} mono_gen_t;


low_cst_t *low_node(low_parser_t *p, low_cst_kind_t kind, low_token_t tok);
void low_pdiag(low_parser_t *p, const char *code, const char *msg, proven_u32 line, proven_u32 col);
low_cst_t *low_refit(low_parser_t *p, low_cst_t *f, low_cst_t **kids, proven_size_t n);
void low_take_kids(low_parser_t *p, low_cst_t *nd, proven_array_t *tmp);
bool low_view_eq_cstr(proven_u8str_view_t v, const char *s);

low_token_t low_adv(low_parser_t *p);
low_tok_kind_t low_curk(low_parser_t *p);
void low_norm_node(low_parser_t *p, low_cst_t *nd);
low_cst_t *low_parse_form(low_parser_t *p);


#endif /* LOW_CST_PRIV_H */

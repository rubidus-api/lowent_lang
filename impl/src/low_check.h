// low_check.h — MVP semantic analysis (S4): static effect discipline.
//
// The flagship MVP rule (SPEC-003, the C++-effect-visibility contrast): a `fn`
// declares purity, so if its body performs an effect (io/alloc/state — directly via
// an effectful builtin, or transitively via a `proc`) the compiler rejects it
// *structurally*. An op's actual effects must be ⊆ its declared effects.
//
// This is a distinct pass over the shared front-end's CST — it runs on MVP source
// and is ignored for dynamic mini (which gates effects by posture instead).
#ifndef LOW_CHECK_H
#define LOW_CHECK_H

#include "proven/array.h"
#include "low_cst.h"

typedef struct {
    proven_array_t diags;  // low_diag_t
    bool           ok;
} low_check_result_t;

// ★★★ RFC-0057 (채택, DECISION-0009) — **하나의 정규화된 의미 요약.**
//
//   효과를 추론하는 자리가 **세 곳**이었다(검사기 · 문서 생성기 · 평가기). 각자 다른 낱말을 알았다:
//     검사기 : print · run · read_file · … · stack_new · panic · raw · spawn/send
//     문서   : print · print_fmt                                   ← **그것뿐이다**
//   ⇒ 같은 프로그램에서 **검사기는 E-EFFECT 로 거절하는데 문서는 "추론된 효과 없음"** 이라 했다.
//     **문서가 순수하다고 거짓말했다.** 경로가 갈리면 그중 하나는 반드시 썩는다.
//
//   ⇒ **요약은 하나다.** 검사기·문서·감사가 **같은 함수**를 부른다.
typedef struct {
    bool     is_calc;         // fn 인가
    unsigned declared;        // `effects` 절이 **전부** (첫 낱말이 아니라)
    unsigned actual;          // 본문에서 **추론된** 효과 (호출을 타고 전파된다)
    bool     writes_caller;   // ★ `mut` 파라미터에 쓴다 = **호출자에게 보인다**(관측적 순수성)
} low_op_summary_t;

// 한 op 의 **정규화된 요약**. 이것이 유일한 진실이다.
[[nodiscard]] low_op_summary_t low_op_summary(const low_parse_result_t *pr, const low_cst_t *opform);
// 효과 비트 → 이름 (하나의 어휘)
const char *low_effect_bit_name(unsigned bit);

// Analyse MVP declarations in `pr`. `work` (heap) backs the op table + diag array.
[[nodiscard]] low_check_result_t low_check(proven_allocator_t work, const low_parse_result_t *pr);

#endif // LOW_CHECK_H

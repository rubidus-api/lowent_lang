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
// ★★★★★ RFC-0120 §5.2-10 — **흡수를 허락한 모듈**을 매니페스트에서 받아 둔다.
//   기본값은 **허락 없음**: 아무것도 주지 않으면 어느 모듈도 `absorbs` 를 쓰지 못한다.
//   ☞ 「누가 보증해도 되는가」는 소스가 아니라 프로젝트의 정체 파일이 정한다.
void low_check_set_absorb_allow(const char *const *names, int n);
// RFC-0135 S3 — `--stack-report`: 진입 op · 태스크마다 가장 깊은 길의 틀 안 나열 바이트와 예산을 찍는다.
void low_check_set_stack_report(bool on);

[[nodiscard]] low_check_result_t low_check(proven_allocator_t work, const low_parse_result_t *pr);

// ★ 모듈 한정 이름(`M.member`)을 bare 로 좁힌다 — **하강도 같은 나무를 본다**(결함 노트 #77).
//   `low_check` 안에서만 돌던 때, `--run` 은 좁혀지지 않은 `case M.v` 를 «이름 묶기»로 읽어
//   그 갈래가 모든 값을 잡았다. 한 나무를 두 층이 다르게 읽으면 그중 하나는 틀린다.
void low_narrow_qualified(const low_parse_result_t *pr);

// 버린 낱말이면 그 까닭(정적 문자열), 아니면 NULL.
const char *low_removed_word_why(proven_u8str_view_t w);

#endif // LOW_CHECK_H

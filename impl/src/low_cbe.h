// low_cbe.h — S5 C backend: emit a self-contained C program from the stack IR.
//
// `lowentc --emit-c <file.low>` prints one translation unit: a small value
// runtime (tagged values, slice/record/result/stack/bitset helpers with
// panic-on-violation semantics mirroring the VM's E-VM-* diags), one C function
// per lowered def (straight-line stack code; branches become gotos), and a
// `main` whose CLI and rendered output are byte-identical to `--run` — so the
// compiled program and the VM can be diffed against each other (golden-locked).
// Unlowered defs are skipped with a comment and excluded from dispatch.
#ifndef LOW_CBE_H
#define LOW_CBE_H

#include <stdio.h>
#include "low_ir.h"

// ★★★ **방출 자격을 한 번 계산한다** (RFC-0089 R1 · 단계 C) — 헤더·래퍼·전방선언이 **읽기만** 한다.
//   같은 IR 로 여러 번 불러도 한 번만 센다. 판정 기록의 뜻은 `low_ir_plan_t`(low_ir.h)에 적혀 있다.
void low_cbe_plan(const low_ir_t *ir);
const low_ir_plan_t *low_cbe_plan_of(proven_size_t di);

// Returns the number of functions emitted (< 0 on invalid input).
int low_cbe_emit(const low_ir_t *ir, FILE *out);
// ★ `--emit-h` — C 가 우리를 부르기 위한 **헤더**. 도구가 낸다(손으로 적으면 갈린다).
int low_cbe_emit_header(const low_ir_t *ir, FILE *out);
// ★ `--no-fast` — 스칼라 타입 하강을 끄는 **대조 스위치**(`--flat` 과 같은 규율).
void low_cbe_set_no_fast(bool v);
// ★ `--no-elemsl` — RFC-0109 단계 1(빠른 경로의 슬라이스 원소 단위)을 끄는 대조 스위치 (WO-0178).
void low_cbe_set_no_elemsl(bool v);
// ★ `--no-carry` — RFC-0111 §8-22 ⓑ(앞 바퀴 값 들고 가기)를 끄는 대조 스위치 (WO-0200).
void low_cbe_set_no_carry(bool v);
// 기계 암호 명령의 범위 — 0 소프트만 · 1 기계 명령만 · 2 둘 다 담고 시작할 때 고른다 (RFC-0119)
void low_cbe_set_hw_clmul(int v);
void low_cbe_set_hw_aes(int v);
void low_cbe_set_hw_simd(int v);   /* ★ ChaCha20 의 폭: 0 없음 · 1 SSE2 · 2 시작할 때 고른다 (RFC-0122) */
void low_cbe_set_hw_avx2(int v);
void low_cbe_set_hw_asm(int v);    /* ★ 손으로 쓴 지름길 — 셈 판·내장함수 다음 셋째 층 */
// ★ `--conc-t0` — 호스트 타깃에도 **프리스탠딩 협력 스케줄러(T0)** 를 내는 대조 스위치 (WO-0206).
//   ☞ 이것이 없으면 T0 는 «짓기만 하고 아무도 안 돌린 코드» 가 된다: 베어메탈에서는 지을 수만
//     있고 돌려 볼 수 없기 때문이다. 호스트에서 돌려 **VM 과 답을 견주는** 것이 이 스위치의 일이다.
void low_cbe_set_conc_t0(bool v);
// ★ `--why-slow` — **어떤 op 이 왜 느린 경로에 남았는가.** 못 하는 일을 말하지 않는 도구는
//   그 목록을 아무도 줄이지 못한다.
void        low_cbe_set_no_main(bool v);   // ★ 라이브러리로 낸다(C 가 main 을 갖는다)
void        low_cbe_why(bool on);
const char *low_cbe_why_of(proven_size_t i);

#endif // LOW_CBE_H

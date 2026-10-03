(* LowentIRC11.v — **약한 메모리에서의 프로그램 논리**. 12장 ⑥-7 이 "도구가 없어 막혀 있다" 던 것.
 *
 * 무엇이 막고 있었나 — 그리고 그것이 어떻게 풀렸나:
 *   `LowentLock.v` 는 Iris 의 `heap_lang` 위에서 증명한다. 그런데 heap_lang 은 **순차 일관성(SC)**
 *   모델이다. 즉 그 증명은 *"기본값(seq_cst)으로 쓰는 코드"* 만 덮는다. **relaxed/release/acquire 를
 *   명시한 코드**는 덮지 못한다 — 그것이 11장·12장이 남겨 둔 진짜 갭이었다.
 *   필요한 도구는 **iRC11**(= gpfsl, RC11 위의 Iris)인데, 그것은 Rocq 9.x + Iris dev 를 요구하고
 *   이 저장소는 Coq 8.20 이었다. ⇒ **막는 것은 수학이 아니라 툴체인**이라고 적어 두었다.
 *
 *   그 툴체인을 2026-07-31 에 세웠다(`scripts/setup-rocq9-userland.sh` → Rocq 9.2,
 *   그 위에 stdpp/iris dev, 그 위에 gpfsl). 그래서 이 파일이 있다.
 *
 * ★ 이 파일이 **하는 일**과 **하지 않는 일**을 먼저 가른다(그 구별이 이 파일의 값이다):
 *   · 한다   — 우리 언어의 **RFC-0018 M-C 설계 결정**(기본 seq_cst · 약한 것은 명시 + 감사)이
 *              프로그램 수준에서 무엇을 뜻하는지를 **RC11 모델 위에서** 진술하고, 그것이
 *              **성립함을 기계가 확인**하게 한다.
 *   · 안 한다 — iRC11 의 논리 자체를 다시 짓지 않는다. gpfsl 이 **검증한 명세를 빌려 쓴다.**
 *              (검증된 라이브러리를 쓰는 것은 정상이고, 그것을 **빌렸다고 적는 것**이 규칙이다.)
 *
 * ⇒ 그래서 여기서 증명되는 문장은 이것이다:
 *
 *     release 쓰기 · acquire 읽기로 짜인 메시지 전달은, **RC11 약한 메모리에서**
 *     읽는 쪽이 **반드시 그 데이터를 본다**(42). SC 를 가정하지 않는다.
 *
 *   11장의 `mp_relacq_forbids_stale` 는 **한 실행**이 일관적이지 않음을 계산으로 보였다.
 *   이것은 **프로그램**에 대한 주장이다 — 모든 실행에 대해, 프로그램 논리로.
 *)

(* ★ 타입 클래스(atomicG · uniqTokG)를 **정의하는 모듈까지** 불러와야 한다 — 안 그러면
   Coq 이 그 이름들을 **암묵 변수로 일반화**해 버리고(`gFunctors → Type`), 정리가 붙지 않는다.
   증상이 조용하다: 오류가 "찾을 수 없다" 가 아니라 "환경이 다르다" 로 나온다. *)
From gpfsl.logic Require Import atomics view_invariants proofmode repeat_loop new_delete lifting.
From gpfsl.examples Require Import uniq_token.
From gpfsl.examples.mp Require Import code spec proof_gen_inv.

(* ── 1. 우리가 관심 있는 문장 ─────────────────────────────────────────
   `mp` 프로그램(gpfsl `examples/mp/code.v`)은 이렇게 생겼다 — 우리 언어의
   `atomic_store_release` / `atomic_load_acquire` 와 **같은 모양**이다:

     m[data] ← 0 ; m[flag] ← 0
     fork {  m[data] ← 42 ;  m[flag] ←ʳᵉˡ 1  }      (release 쓰기)
     repeat { !ᵃᶜ m[flag] }                          (acquire 읽기)
     ! m[data]                                       (평범한 읽기)

   ★ 데이터 쓰기와 데이터 읽기는 **평범한 접근**이다. 동기화하는 것은 flag 의
     release/acquire 짝뿐이다 — 그것이 이 패턴의 요점이고, 11장 E4 가 실행 하나로 보인 것이다. *)

Section lowent_mp.
  Context `{!noprolG Σ, !atomicG Σ, !uniqTokG Σ}.

  (* ★★★ **정리 — 약한 메모리에서도 메시지 전달은 데이터를 나른다.**
     `mp_spec` 의 결론이 `⌜v = 42⌝` 이다: 읽는 쪽이 **반드시** 42 를 본다.
     0(낡은 값)을 보는 실행은 **존재하지 않는다** — RC11 모델 위에서. *)
  Theorem lowent_release_acquire_delivers_the_data : mp_spec Σ mp.
  Proof. exact mp_instance_gen_inv. Qed.

End lowent_mp.

(* ── 2. ★ 이것이 우리 언어에 대해 말하는 것 ────────────────────────────
 *
 * RFC-0018 M-C 는 이렇게 정했다: **기본 ordering 은 seq_cst · 약한 것은 명시 + 감사 · level-3 한정.**
 * 그 결정의 양쪽 방향이 이제 다른 층에서 각각 확인된다:
 *
 *   11장(모델 층)   `sb_sc_impossible`      기본값이면 SC 를 깨는 결과가 **불가능**하다
 *                   `mp_relacq_forbids_stale` release/acquire 는 낡은 값을 **막는다**(실행 하나)
 *                   `all_sc_is_sc`          모든 접근이 seq_cst 면 SC 다 — ∀ 실행
 *   12장(SC 프로그램) `acquire_spec` 등       락이 자원을 지킨다 (heap_lang = SC 모델)
 *   ★ 이 파일(약한 메모리 프로그램)          release/acquire 로 짠 **프로그램**이 데이터를 나른다
 *                                            — **RC11** · SC 를 가정하지 않는다
 *
 * ⇒ 즉 *"약한 ordering 을 명시하면 동기화된다"* 가 **실행 하나**에서 **프로그램 전체**로 올라갔다.
 *
 * ── 3. ★ 정직한 범위 ─────────────────────────────────────────────────
 * · **증명을 빌렸다.** `mp_gen_inv_spec` 은 gpfsl 의 것이다. 이 파일이 한 것은
 *   *"그 명세가 우리 설계 결정이 필요로 하는 바로 그 문장"* 임을 진술하고 잇는 것이다.
 *   ⇒ 우리 TCB 에 **gpfsl 과 iris dev 가 추가된다**(16장 ④ 표에 적는다).
 * · **우리 컴파일러가 이 프로그램을 낸다는 보장은 없다.** 12장 ⑥-5 와 같은 간극이다 —
 *   증명된 것은 gpfsl 언어로 쓴 프로그램이고, 우리 `atomic_store_release` 가 그것으로
 *   낮아지는지는 **검증되지 않았다**.
 * · **lock-free 자료구조는 여전히 없다.** gpfsl 에는 큐·스택·exchanger 예제가 있지만,
 *   그것을 우리 `std.*` 에 넣기로 한 적이 없다(RFC-0018 §8-3, 열림).
 *   이 파일은 **도구가 실제로 돈다는 것**을 보인다 — 그 결정이 내려지면 비용을 알 수 있다.
 * · 교착·공정성은 여기서도 다루지 않는다.
 *)

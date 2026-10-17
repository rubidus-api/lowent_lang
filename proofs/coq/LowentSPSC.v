(* LowentSPSC.v — **lock-free SPSC 링 버퍼의 알고리즘 자체**를 약한 메모리에서.
 *
 * `docs/manual/lib/spsc.md` 의 "아직 없는 것" 이 이렇게 적혀 있었다:
 *   *"**알고리즘 자체의 기계 증명.** 받치는 것은 **패턴**(메시지 전달)의 증명이지,
 *     **이 링 버퍼의 전체 불변식**이 아니다. gpfsl 에 검증된 큐·순환 버퍼 예제가 있으므로
 *     그 길은 열려 있다 — 그러나 **하지 않은 것을 했다고 적지 않는다."*
 *
 * 그 길을 걸었다. 그리고 걸어 보니 — gpfsl 의 `circ_buff` 는 **우리 알고리즘 그 자체**였다:
 *
 *     gpfsl try_prod                            lib/spsc.low  spsc_push
 *     ─────────────────────────────────────────────────────────────────────────
 *     let w = !ᵃᶜ (q + wi)                      let t = atomic_load ctl 1 …
 *     let r = !ᵃᶜ (q + ri)                      let h = atomic_load ctl 0 order acquire
 *     let w' = (w + 1) mod Ns                   let nx = mod (add ti 1) (len buf)
 *     if w' = r then 0                          guard ne nx (mod h (len buf)) . else return 0
 *     else q + (b + w) <- x ;                   set (index buf ti) v .
 *          q + wi <-ʳᵉˡ w' ; 1                  atomic_store ctl 1 nx order release . return 1
 *
 * **한 줄씩 같다.** 인덱스를 `mod` 로 감싸는 것, 가득 참을 `w' = r` 로 판정하는 것(한 칸을
 * 비워 두는 규칙), 데이터를 **먼저** 쓰고 색인을 **release 로** 공개하는 것까지.
 *
 * ★ 그래서 이 파일이 하는 일은 **증명이 아니라 대응**이다: gpfsl 의 검증된 명세를 우리가
 *   기대는 문장으로 진술하고 잇는다. 그리고 **빌렸다고 적는다**(TCB 에 gpfsl 이 추가된다).
 *   ⇒ 우리 라이브러리가 기대는 것은 이제 *패턴* 이 아니라 **이 알고리즘의 전체 불변식**이다.
 *)

From gpfsl.logic Require Import proofmode atomics new_delete repeat_loop.
From gpfsl.examples.circ_buff Require Import code proof_gps.

Section lowent_spsc.
  (* ★ 클래스 이름은 `cbG`(circular buffer)이고 자원 술어는 `lit → vProp` 이다 —
     `cbufG`·`Z → vProp` 로 짐작해 적었다가 둘 다 틀렸다. **원본을 읽고 맞춘다.** *)
  Context `{!noprolG Σ, cirbG : !cbG Σ, !atomicG Σ}.
  Context (Ns : nat) (P : lit → vProp Σ).

  (* ★★★ **정리 — 밀어 넣기.** 생산자 자격(`Prod`)과 값의 자원(`P v`)을 주면,
     성공(b ≠ 0)하면 자원이 **버퍼로 넘어가고**, 실패(b = 0, 가득 참)하면 **되돌아온다.**
     ⇒ *"넣었는데 사라졌다"* 도 *"실패했는데 뺏겼다"* 도 일어나지 않는다. 그것이 이 명세다. *)
  Theorem lowent_spsc_push_is_correct q v tid (nGt1 : 0 < Ns) :
    {{{ Prod Ns P q ∗ P v }}}
      try_prod Ns [ #q; #v] @ tid; ⊤
    {{{ (b : Z), RET #b; Prod Ns P q ∗ (⌜b ≠ 0⌝ ∨ P v) }}}.
  Proof. exact (try_prod_spec Ns P q v tid nGt1). Qed.

  (* ★★★ **정리 — 꺼내기.** 소비자 자격(`Cons`)을 주면, 성공하면 **그 값의 자원**을 받는다.
     ★ 여기가 약한 메모리의 핵심이다: 받은 `P x` 는 **생산자가 넣을 때의 그 자원**이다 —
       release/acquire 짝이 그것을 **날라 준다**(11장 E4 · LowentIRC11.v 가 패턴으로 보인 것). *)
  Theorem lowent_spsc_pop_is_correct q tid (nGt1 : 0 < Ns) :
    {{{ Cons Ns P q }}}
      try_cons Ns [ #q] @ tid; ⊤
    {{{ (x : lit), RET #x; Cons Ns P q ∗ (⌜x = 0⌝ ∨ P x) }}}.
  Proof. exact (try_cons_spec Ns P q tid nGt1). Qed.

End lowent_spsc.

(* ── ★ 정직한 범위 ─────────────────────────────────────────────────────
 * · **증명을 빌렸다** — `try_prod_spec`·`try_cons_spec` 은 gpfsl 의 것이다. 이 파일이 한 일은
 *   그 명세가 **우리 `lib/spsc.low` 가 기대는 바로 그 문장**임을 진술하고 잇는 것이다.
 *   ⇒ TCB 에 gpfsl 이 추가된다(16장 ④ — 이미 `LowentIRC11.v` 로 들어와 있다).
 * · **우리 컴파일러가 이 프로그램을 낸다는 보장은 없다.** 증명된 것은 gpfsl 언어로 쓴
 *   `try_prod`/`try_cons` 이고, 우리 `spsc_push`/`spsc_pop` 이 그것과 **한 줄씩 같다**는 것은
 *   **사람이 읽어서 확인한 것**이다(위 대응표). 그 대응은 기계 검증되지 않았다.
 *   ★ 그것이 이 파일의 가장 큰 간극이고, 12장 ⑥-5 와 **같은 종류**다.
 * · 크기(`Ns`)와 자원 술어(`P`)는 매개변수다. 우리 코드의 `len buf` 가 `Ns` 에 대응하고,
 *   "한 칸을 비워 둔다" 는 규칙이 양쪽에 **똑같이** 있다(`w' = r` 판정).
 * · 여전히 안 한 것: **MPSC/MPMC·seqlock**(RFC-0018 §8-3 의 나머지) · 진행성(progress).
 *)

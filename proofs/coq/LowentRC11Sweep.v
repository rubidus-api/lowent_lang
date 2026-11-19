(* LowentRC11Sweep.v — **litmus 전수 열거**: 손으로 고른 둘에서 이름 있는 여덟로.
 *
 * 왜 이 파일이 있나 — `LowentRC11.v` 의 약점은 정리의 세기가 아니라 **표본**이었다:
 *   다루는 실행이 **손으로 고른 두 개**(SB · MP)다. 그리고 그것이 실제로 위험했다 —
 *   처음 쓴 모델은 공리가 모자라 SB 를 seq_cst 에서도 통과시켰는데, **손으로 고른 표본이
 *   두 개뿐이라 그 사실을 늦게 알았다.** 표본을 늘리는 것이 그 약점에 대한 답이다.
 *
 * 이 파일이 하는 것 둘:
 *   ① **litmus 배터리** — 메모리 모델 문헌의 **이름 있는** 시험들을 모델에 먹인다:
 *      SB(store buffering) · MP(message passing) · LB(load buffering) ·
 *      CoRR(read-read coherence) · CoWR · 2+2W · IRIW(independent reads of independent writes).
 *      각각에 대해 **기본값(seq_cst)이 나쁜 결과를 막는가** 와 **relaxed 는 허용하는가** 를
 *      기계가 계산한다. ⇒ 손으로 고른 둘이 **여덟**이 된다. 그리고 이름이 있으므로
 *      *"우리가 고른 것"* 이 아니라 *"그 분야가 고른 것"* 이다 — 표본 선택의 자의성이 줄어든다.
 *   ② **유계 전수 단조성** — 같은 실행에서 mode 만 **한 칸 약하게** 한 모든 짝에 대해
 *      `consistent(센 쪽) ⟹ consistent(약한 쪽)` 을 **전수로** 확인한다.
 *      `LowentRC11SC.v` 는 이것을 **간선 수준**(sw_weaken)까지만 증명했다. 여기서는 공리 전체에
 *      대해 — 다만 **그 봉투 안에서만**. 그래서 이것은 "증명됨" 이 아니라 **"전수 검사됨"** 이다.
 *
 * ★ 정직하게: ①도 ②도 **증명이 아니다.** 유한한 표본에 대한 계산이다. 그러나 그 표본이
 *   **문헌이 고른 것**이고 **전수**라는 점이 값이다. 그리고 무엇이 표본 밖인지 §4 에 적는다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.
Require Import LowentRC11.
Require Import LowentRC11SC.

(* ── 0. 작은 도구 ─────────────────────────────────────────────── *)

Definition MODES : list mode := [Rlx; Acq; Rel; AcqRel; SC].

(* mode 의 같음 — 모델에 없어서 여기서 준다(다섯 낱말이므로 손으로 적는 것이 정직하다). *)
Definition mode_eqb (a b : mode) : bool :=
  match a, b with
  | Rlx, Rlx | Acq, Acq | Rel, Rel | AcqRel, AcqRel | SC, SC => true
  | _, _ => false
  end.

(* mode 를 이벤트 목록에 씌운다(초기화 둘은 그대로 둔다 — 그것은 프로그램이 아니다). *)
Fixpoint set_modes (E : exec) (ms : list mode) : exec :=
  match E, ms with
  | e :: rE, m :: rm =>
      mk_ev (e_id e) (e_tid e) (e_idx e) (e_kind e) (e_loc e) (e_val e) m :: set_modes rE rm
  | _, _ => E
  end.

Definition all_of (m : mode) (n : nat) : list mode := repeat m n.

(* ── 1. litmus 배터리 — **이름 있는** 시험들 ───────────────────────── *)

(* 각 시험 = (이름, 이벤트 열, rf, mo, "나쁜 결과인가").
   ★ 이벤트의 값(e_val)이 이미 **그 나쁜 결과**를 적어 둔 상태다 — litmus 관례대로,
     실행 하나가 곧 "이 결과가 가능한가?" 라는 물음이다. *)

Definition z : loc := 2.

(* ── SB (store buffering): 둘 다 0 을 읽는다 — SC 가 막아야 한다 ── *)
Definition sb_E : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr x 1 Rlx ; mk_ev 3 1 1 Rd y 0 Rlx
  ; mk_ev 4 2 0 Wr y 1 Rlx ; mk_ev 5 2 1 Rd x 0 Rlx ].
Definition sb_rf : rfmap := [ (3, 1) ; (5, 0) ].
Definition sb_mo : momap := [ (0, 2) ; (1, 4) ].

(* ── LB (load buffering): 각자 상대의 **나중** 쓰기를 읽는다 ── *)
Definition lb_E : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Rd x 1 Rlx ; mk_ev 3 1 1 Wr y 1 Rlx
  ; mk_ev 4 2 0 Rd y 1 Rlx ; mk_ev 5 2 1 Wr x 1 Rlx ].
Definition lb_rf : rfmap := [ (2, 5) ; (4, 3) ].
Definition lb_mo : momap := [ (0, 5) ; (1, 3) ].

(* ── MP (message passing): flag=1 을 보고도 data=0 을 읽는다 ── *)
Definition mp_E : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr x 1 Rlx ; mk_ev 3 1 1 Wr y 1 Rlx
  ; mk_ev 4 2 0 Rd y 1 Rlx ; mk_ev 5 2 1 Rd x 0 Rlx ].
Definition mp_rf : rfmap := [ (4, 3) ; (5, 0) ].
Definition mp_mo : momap := [ (0, 2) ; (1, 3) ].

(* ── CoRR (read-read coherence): 한 스레드가 x 를 1 → 0 순서로 읽는다.
   ★ 이것은 **모든 ordering 에서 금지**되어야 한다 — coherence 는 relaxed 에도 있다.
     즉 여기서 relaxed 가 허용하면 그것이 곧 모델의 결함이다(SB 와 성격이 다르다). ── *)
Definition corr_E : exec :=
  [ ix Rlx
  ; mk_ev 1 1 0 Wr x 1 Rlx
  ; mk_ev 2 2 0 Rd x 1 Rlx ; mk_ev 3 2 1 Rd x 0 Rlx ].
Definition corr_rf : rfmap := [ (2, 1) ; (3, 0) ].
Definition corr_mo : momap := [ (0, 1) ].

(* ── CoWR: 자기가 쓴 값을 못 보고 초기값을 읽는다 — 항상 금지 ── *)
Definition cowr_E : exec :=
  [ ix Rlx
  ; mk_ev 1 1 0 Wr x 1 Rlx ; mk_ev 2 1 1 Rd x 0 Rlx ].
Definition cowr_rf : rfmap := [ (2, 0) ].
Definition cowr_mo : momap := [ (0, 1) ].

(* ── 2+2W: 두 스레드가 서로의 쓰기를 앞서 놓는다(mo 순환) — SC 가 막아야 한다 ── *)
Definition w22_E : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr x 2 Rlx ; mk_ev 3 1 1 Wr y 1 Rlx
  ; mk_ev 4 2 0 Wr y 2 Rlx ; mk_ev 5 2 1 Wr x 1 Rlx ].
Definition w22_rf : rfmap := [].
Definition w22_mo : momap := [ (0, 5) ; (5, 2) ; (1, 3) ; (3, 4) ].

(* ── IRIW: 두 관찰자가 두 쓰기를 **반대 순서로** 본다 — SC 가 막아야 한다 ── *)
Definition iriw_E : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr x 1 Rlx
  ; mk_ev 3 2 0 Wr y 1 Rlx
  ; mk_ev 4 3 0 Rd x 1 Rlx ; mk_ev 5 3 1 Rd y 0 Rlx
  ; mk_ev 6 4 0 Rd y 1 Rlx ; mk_ev 7 4 1 Rd x 0 Rlx ].
Definition iriw_rf : rfmap := [ (4, 2) ; (5, 1) ; (6, 3) ; (7, 0) ].
Definition iriw_mo : momap := [ (0, 2) ; (1, 3) ].

(* ── ★★★ **fence 시험** (2026-07-30) — 배터리의 가장 큰 빈칸이었다 ──────────
   MP 를 **relaxed 접근 + fence** 로 고칠 수 있는가? 그것이 fence 의 존재 이유다.
     T1:  W data 1 (rlx) ; **F_rel** ; W flag 1 (rlx)
     T2:  R flag → 1 (rlx) ; **F_acq** ; R data → 0 (rlx)   ← 나쁜 결과
   ★ 모든 접근이 relaxed 다. fence 만 release/acquire 다. 그래도 막혀야 한다. *)
Definition mpf_E (fm gm : mode) : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr  x 1 Rlx      (* T1: W data 1 (relaxed) *)
  ; mk_ev 3 1 1 Fnc 0 0 fm       (* T1: fence (release 후보) — 자리·값은 뜻이 없다 *)
  ; mk_ev 4 1 2 Wr  y 1 Rlx      (* T1: W flag 1 (relaxed) *)
  ; mk_ev 5 2 0 Rd  y 1 Rlx      (* T2: R flag → 1 (relaxed) *)
  ; mk_ev 6 2 1 Fnc 0 0 gm       (* T2: fence (acquire 후보) *)
  ; mk_ev 7 2 2 Rd  x 0 Rlx ].   (* T2: R data → 0  ← 낡은 값 *)
Definition mpf_rf : rfmap := [ (5, 4) ; (7, 0) ].
Definition mpf_mo : momap := [ (0, 2) ; (1, 4) ].

(* ★★★ **fence 가 실제로 동기화한다** — 접근이 전부 relaxed 인데도 낡은 값이 **불가능**하다. *)
Theorem fences_synchronise_relaxed_accesses :
  consistent (mpf_E Rel Acq) mpf_rf mpf_mo = false.
Proof. vm_compute; reflexivity. Qed.

(* ★★ 그리고 **fence 를 빼면 가능하다** — 짝이 있어야 위 정리가 뜻을 갖는다.
   (fence 를 relaxed 로 두는 것이 곧 "fence 가 없는 것" 이다 — sw 의 세 fence 갈래가
    writes_rel/reads_acq 를 요구하므로.) *)
Theorem without_fences_the_stale_read_is_allowed :
  consistent (mpf_E Rlx Rlx) mpf_rf mpf_mo = true.
Proof. vm_compute; reflexivity. Qed.

(* ★ 한쪽만 있으면? **모자란다** — release fence 만, acquire fence 만. 둘 다 필요하다. *)
Theorem one_fence_is_not_enough :
  consistent (mpf_E Rel Rlx) mpf_rf mpf_mo = true /\
  consistent (mpf_E Rlx Acq) mpf_rf mpf_mo = true.
Proof. split; vm_compute; reflexivity. Qed.

(* 시험 하나를 주어진 mode 로 돌린다. *)
Definition run_at (E : exec) (rf : rfmap) (mo : momap) (m : mode) : bool :=
  consistent (set_modes E (all_of m (length E))) rf mo.

(* ── 2. ★★ 배터리 결과 — 기계가 계산한다 ────────────────────────── *)

(* ★★★ **기본값(seq_cst)은 SC 를 깨는 결과를 전부 막는다.**
   손으로 고른 둘(SB·MP)이 아니라 **문헌이 고른 다섯**에 대해. *)
Theorem sc_forbids_the_battery :
  run_at sb_E   sb_rf   sb_mo   SC = false /\
  run_at lb_E   lb_rf   lb_mo   SC = false /\
  run_at mp_E   mp_rf   mp_mo   SC = false /\
  run_at w22_E  w22_rf  w22_mo  SC = false /\
  run_at iriw_E iriw_rf iriw_mo SC = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ★★ **coherence 시험은 relaxed 에서도 금지된다** — 성격이 다른 종류다.
   SB 를 막는 것은 SC 축이지만, CoRR/CoWR 을 막는 것은 **coherence 축**이고
   그것은 ordering 과 무관하게 늘 있다. ⇒ "relaxed = 아무 보장 없음" 이 **아니다.** *)
Theorem coherence_holds_even_at_relaxed :
  run_at corr_E corr_rf corr_mo Rlx = false /\
  run_at cowr_E cowr_rf cowr_mo Rlx = false /\
  run_at corr_E corr_rf corr_mo SC  = false /\
  run_at cowr_E cowr_rf cowr_mo SC  = false.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ★★ 그리고 **모델이 공허하지 않다** — relaxed 는 그 결과들을 실제로 **허용한다.**
   (허용하지 않으면 위 정리는 "아무것도 통과 못 하는 모델" 의 부산물일 뿐이다.
    ★ 이 짝이 없으면 배터리는 장식이다 — LowentRC11.v 가 SB 에서 배운 그 교훈.) *)
Theorem relaxed_allows_the_weak_ones :
  run_at sb_E   sb_rf   sb_mo   Rlx = true /\
  run_at lb_E   lb_rf   lb_mo   Rlx = true /\
  run_at mp_E   mp_rf   mp_mo   Rlx = true /\
  run_at w22_E  w22_rf  w22_mo  Rlx = true /\
  run_at iriw_E iriw_rf iriw_mo Rlx = true.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ★ 그리고 **acq_rel 도 동기화한다** — MP 를 막는다. 다섯 ordering 중 어느 것이 무엇을
   하는지가 이제 다섯 낱말 모두에 대해 기계 위에 있다(Acq/Rel 은 LowentRC11.v 의 MP). *)
Theorem acqrel_also_synchronises : run_at mp_E mp_rf mp_mo AcqRel = false.
Proof. vm_compute; reflexivity. Qed.

(* ── 3. ★★ 유계 전수 단조성 ──────────────────────────────────── *)

(* 한 칸 약하게 한 것들: 위치 k 의 mode 를 m' 로 바꾼다(단, stronger m m' 이어야 한다). *)
Fixpoint replace_at (ms : list mode) (k : nat) (m' : mode) : list mode :=
  match ms, k with
  | [], _ => []
  | _ :: r, 0 => m' :: r
  | a :: r, S k' => a :: replace_at r k' m'
  end.

Fixpoint idx_upto (n : nat) : list nat :=
  match n with 0 => [] | S k => idx_upto k ++ [k] end.

(* ms 에서 **한 칸만** 약하게 한 모든 목록.
   ★★ 세기 순서는 `stronger` 가 아니라 **`stronger_sc`** 다 (2026-07-31 정정).
     `stronger` 는 sw 간선만 재고 `is_sc` 를 안 보므로 `stronger AcqRel SC = true` 라고 말한다 —
     그것은 **약화가 아니다**(AcqRel→SC 는 psc 축에서 오히려 세지는 방향이다). 그 순서로는
     단조성이 **거짓**이고, 반례가 `LowentRC11Mono.v` 의 `old_order_is_not_a_strength_order` 다.
     이 전수가 그것을 못 본 이유: 아래 두 골격(CoWR·CoRR)이 **psc 축을 건드리지 않는다.**
   ⇒ 검사의 봉투가 좁으면 **정의의 결함까지 통과시킨다.** 그것을 잡은 것은 일반 증명이었다. *)
Definition one_step_weakenings (ms : list mode) : list (list mode) :=
  flat_map (fun k =>
    flat_map (fun m' =>
      match nth_error ms k with
      | Some m => if stronger_sc m m' && negb (mode_eqb m m') then [replace_at ms k m'] else []
      | None => []
      end) MODES) (idx_upto (length ms)).

(* 모든 mode 목록(길이 n) — 봉투를 작게 유지한다. *)
Fixpoint gen_modes (n : nat) : list (list mode) :=
  match n with
  | 0 => [[]]
  | S k => flat_map (fun m => map (fun r => m :: r) (gen_modes k)) MODES
  end.

(* 한 실행 골격에 대해: 모든 mode 배정 × 그 각각의 한 칸 약화에 대해
   `consistent(센 쪽) ⟹ consistent(약한 쪽)` 인가? *)
Definition mono_ok (E : exec) (rf : rfmap) (mo : momap) : bool :=
  forallb (fun ms =>
    forallb (fun ms' =>
      implb (consistent (set_modes E ms) rf mo)
            (consistent (set_modes E ms') rf mo))
      (one_step_weakenings ms))
    (gen_modes (length E)).

(* ★★★ **전수 검사됨**(증명됨이 아니다): 이 봉투 안에서 단조성이 성립한다.
   봉투 = CoWR 골격(3 이벤트) · mode 5종 · 한 칸 약화 전부.
   ⇒ 5³ = 125 배정 × 각 배정의 한 칸 약화들 × consistent 두 번.

   왜 이 검사가 값이 있나: `LowentRC11SC.v` 는 단조성을 **간선 수준**까지만 증명했다
   (약하게 하면 sw 간선이 줄어든다). *"그러므로 consistent 가 보존된다"* 는 잇지 못했다 —
   공리가 넷이고 각각에서 hb 가 서로 다른 자리에 나타나기 때문이다.
   이 전수 검사가 **그 잇는 부분**을 봉투 안에서 확인한다. *)
Theorem mono_bounded_cowr : mono_ok cowr_E cowr_rf cowr_mo = true.
Proof. vm_compute; reflexivity. Qed.

Theorem mono_bounded_corr : mono_ok corr_E corr_rf corr_mo = true.
Proof. vm_compute; reflexivity. Qed.

(* ★★★ **그리고 이 전수는 이제 정리의 특수한 경우다** (2026-07-31):
   `LowentRC11Mono.v` 의 `consistent_monotone` 이 **모든 실행 · 모든 배정 · 모든 약화**에
   대해 같은 것을 증명한다. 그런데 이 두 줄을 **지우지 않는다** — 정리는 모델 안에서 참이고,
   전수는 그 모델을 **계산으로** 한 번 더 두드린다. 둘이 어긋나는 날이 오면 그 자체가 신호다. *)

(* ── ★ 6 이벤트 단조성 — **시도했고, 안 됐다. 그 이유를 적는다** ──────────────
 * §4 는 *"6 이벤트면 5⁶ = 15,625 배정이라 vm_compute 로는 무리"* 라고 적었다. 넘으려고 이렇게
 * 생각했다: 단조성은 **"센 쪽이 통과하면"** 만 요구하므로, 통과 못 하는 배정은 아무것도
 * 요구하지 않는다 ⇒ **일관적인 배정만 걸러서** 돌리면 되지 않나?
 *
 * ★★ **틀렸다.** 거르려면 **모든 배정에 대해 `consistent` 를 한 번씩 계산해야 한다** —
 *   그것이 바로 비싼 부분이다. 필터는 뒤따르는 약화 검사만 줄이고 **주 비용은 그대로**다.
 *   실측: 3 이벤트 봉투가 93 초인데 6 이벤트는 **20 분을 넘겨도 끝나지 않았다**
 *   (배정이 125 배 · 각 `consistent` 도 폐포가 커져 더 비싸다). ⇒ 멈추고 **못 했다고 적는다.**
 *   봉투는 여전히 **3 이벤트**(CoWR·CoRR)다.
 *
 * ★★★ **뒷이야기 (2026-07-31): 봉투를 넓히는 대신 계산을 없앴다.**
 *   `LowentRC11Mono.v` 가 단조성을 **정리로** 증명한다 — ∀ 실행 · ∀ 배정 · ∀ 약화. 그러면
 *   6 이벤트도 60 이벤트도 함께 닫힌다. ⇒ *"계산이 안 끝난다"* 는 때때로 **계산을 그만두라는
 *   신호**다. 그리고 그 증명이 **이 전수가 못 본 결함**을 찾았다(세기 순서가 SC 를 꼭대기에
 *   두지 않았다 — 위 `one_step_weakenings` 주석).
 *
 * ☞ 이 절을 지우지 않는 이유: 다음 사람이 같은 아이디어를 다시 낼 것이고, 그때
 *   *"해 봤고 왜 안 되는지"* 가 적혀 있으면 하루를 아낀다. **실패한 최적화도 결과다.**
 *   (넓히려면 계산을 Coq 밖으로 내야 하고, 그러면 그것이 또 하나의 "믿을 대상" 이 된다.) *)

(* ── 4. ★ 표본 밖에 무엇이 있나 ─────────────────────────────────── *)

(* 정직하게 적는다 — 이것을 빼면 "전수" 라는 낱말이 과장이 된다.
   · 배터리는 **일곱 골격**이다. 문헌의 litmus 모음(herd7 등)은 수백 개다.
     특히 **fence** 를 쓰는 시험이 하나도 없다 — 이 모델에 fence 이벤트가 없다.
   · 단조성 전수는 **3 이벤트 골격 둘**(CoWR·CoRR)에 대해서다. 6 이벤트(SB·MP·LB)로 올리면
     5⁶ = 15,625 배정 × 약화들 × 여섯 이벤트 폐포 — `vm_compute` 로는 무리다.
     ⇒ **못 한 것을 못 했다고 적는다.** 더 넓히려면 계산을 Coq 밖으로 내야 하고(추출),
       그러면 그것은 또 하나의 "믿을 대상" 이 된다. 그 교환을 눈감지 않는다.
   · rf·mo 는 **각 골격당 하나씩**(그 나쁜 결과에 해당하는 것)이다. 모든 rf/mo 후보를
     열거하지 않았다 — 그것이 진짜 "전수" 이고, 지금은 아니다.
   · AcqRel 은 MODES 에 있지만 배터리의 `run_at` 은 **한 mode 를 전부에 씌운다** —
     섞인 배정(예: release 쓰기 + relaxed 읽기)은 `LowentRC11.v` 의 MP 가 손으로 본다. *)

(* ── 5. ★★ rf/mo **전수 열거** — 골격당 하나에서 모든 후보로 ────────────────
 *
 * §4 가 적은 표본의 한계 중 하나: *"rf·mo 는 각 골격당 하나씩(그 나쁜 결과)이다. 모든 rf/mo
 * 후보를 열거하지 않았다 — 그것이 진짜 전수이고, 지금은 아니다."*  그것을 갚는다.
 *
 * 무엇을 여는가: 지금까지는 **우리가 고른 결과**가 막히는지만 봤다. 그런데 모델의 결함은
 * *"막아야 할 것을 안 막는다"* 말고 **"막지 말아야 할 것을 막는다"** 로도 온다 — 그러면
 * 모델은 조용히 **아무것도 허용하지 않는 쪽**으로 썩는다(그리고 모든 "금지" 정리가 공짜로 참이 된다).
 * ⇒ 모든 rf/mo 후보를 돌려 **적어도 하나는 일관적**임을 확인한다. 그것이 모델이 살아 있다는 증거다.
 *)

(* 읽기 하나가 읽을 수 있는 쓰기 후보: 같은 자리의 모든 쓰기(초기화 포함). *)
Definition writers_of (E : exec) (l : loc) : list nat :=
  map e_id (filter (fun e => match e_kind e with
                             | Wr => Nat.eqb (e_loc e) l
                             | _ => false end) E).

Definition reads_of (E : exec) : list event :=
  filter (fun e => match e_kind e with Rd => true | _ => false end) E.

(* 모든 rf 후보 — 읽기마다 독립적으로 고른다(곱집합). *)
Fixpoint all_rf (E : exec) (rs : list event) : list rfmap :=
  match rs with
  | [] => [[]]
  | r :: rest =>
      flat_map (fun tail =>
        map (fun w => (e_id r, w) :: tail) (writers_of E (e_loc r)))
        (all_rf E rest)
  end.

(* 모든 mo 후보 — 자리마다 쓰기들의 **순서 쌍 집합**을 두 방향으로 넣어 본다.
   ★ 완전한 순열 열거가 아니다(쓰기가 셋 이상이면 부분집합이다) — 그렇게 적는다.
     여기서 여는 것은 "우리가 고른 mo 하나" 에서 "양방향 전부" 로 넓히는 것이다. *)
Fixpoint pairs (l : list nat) : list (nat * nat) :=
  match l with
  | [] => []
  | x :: r => map (fun y => (x, y)) r ++ pairs r
  end.

Fixpoint subsets {A} (l : list A) : list (list A) :=
  match l with
  | [] => [[]]
  | x :: r => let s := subsets r in map (fun t => x :: t) s ++ s
  end.

Definition all_mo (E : exec) (ls : list loc) : list momap :=
  map (@concat (nat * nat))
    (fold_right (fun l acc =>
       flat_map (fun rest =>
         map (fun ch => ch :: rest)
             (map (fun ps => ps)
                  (subsets (pairs (writers_of E l) ++ map (fun p => (snd p, fst p))
                                                        (pairs (writers_of E l))))))
         acc) [[]] ls).

(* ★★★ **결과까지 열거한다** — 그리고 그 과정에서 하나 배웠다:
   처음엔 rf 만 열거하고 *"일관적인 rf 가 하나는 있다"* 를 물었는데 **false 가 나왔다.**
   이유: 이벤트에 **읽은 값이 박혀 있다**(litmus 관례). rf_wellformed 가 값을 맞추므로,
   읽기가 0 이라고 적힌 실행에서는 **초기화만** 읽을 수 있다 — 다른 rf 가 애초에 없다.
   ⇒ 결과를 열거하려면 **읽은 값을 함께** 바꿔야 한다. 열거의 대상을 잘못 잡았던 것이다.
     (모델이 아니라 묻는 방법이 틀렸다. 그 구별을 남긴다.) *)

(* SB 를 **결과로 매개변수화**한다: T1 이 읽은 y 값 a, T2 가 읽은 x 값 b. *)
Definition sb_out (a b : val) (m : mode) : exec :=
  [ ix m ; iy m
  ; mk_ev 2 1 0 Wr x 1 m ; mk_ev 3 1 1 Rd y a m
  ; mk_ev 4 2 0 Wr y 1 m ; mk_ev 5 2 1 Rd x b m ].

Definition VALS : list val := [0; 1].

(* (결과 × rf) 전수에서 **일관적인 것의 개수**. *)
Definition count_outcomes (mk : val -> val -> exec) (mo : momap) : nat :=
  length (flat_map (fun a =>
            flat_map (fun b =>
              let E := mk a b in
              filter (fun rf => consistent E rf mo) (all_rf E (reads_of E))) VALS) VALS).

(* ★★ **모델이 살아 있다** — seq_cst 에서도 허용되는 결과가 있다("전부 금지" 로 썩지 않았다). *)
Theorem the_model_is_not_vacuous_sb :
  0 <? count_outcomes (fun a b => sb_out a b SC) sb_mo = true.
Proof. vm_compute; reflexivity. Qed.

(* ★★★ 그리고 **relaxed 는 엄격히 더 허용한다** — 결과와 rf 를 **전수로** 세어서.
   지금까지는 "그 나쁜 결과 하나" 가 막히는지만 봤다. 이제 **허용 집합의 크기**를 비교한다. *)
Theorem relaxed_allows_strictly_more_sb :
  count_outcomes (fun a b => sb_out a b SC) sb_mo <
  count_outcomes (fun a b => sb_out a b Rlx) sb_mo.
Proof. vm_compute; repeat constructor. Qed.

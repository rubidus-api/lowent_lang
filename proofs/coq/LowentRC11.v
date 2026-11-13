(* LowentRC11.v — R7-(3): **RC11 약한 메모리 모델**. 순수 Coq. Iris 가 필요 없다.
 *
 * RFC-0018 M-C: 기준 모델 = **RC11**(repaired C11, Lahav et al. PLDI'17).
 *   노출 ordering = 5종 {relaxed, acquire, release, acq_rel, seq_cst}, **기본 seq_cst**,
 *   약한 ordering 은 **명시 + audit**, level-3 한정.
 *
 * ★ 이 파일이 증명하는 것은 그 결정이 기대는 **두 명제**다:
 *
 *   ① **기본값이면 SC 로 사고해도 된다.**
 *      모든 접근이 seq_cst 이면, SC 를 깨는 결과(store buffering: 둘 다 0 을 읽는다)가
 *      **불가능하다.**  → sb_sc_impossible (Qed)
 *
 *   ② **약한 ordering 은 진짜로 그것을 깬다.**
 *      relaxed 면 바로 그 결과가 **일관성 공리를 모두 만족한다.**
 *      → sb_relaxed_is_consistent (Qed) · sb_relaxed_breaks_sc (Qed)
 *
 *   ①이 없으면 "기본 seq_cst" 는 위안일 뿐이고, ②가 없으면 "명시 + audit" 은 과잉이다.
 *   **둘 다 증명해야 그 설계 결정이 정당해진다.**
 *
 * ★★ 정직한 범위:
 *   RC11 **전체**의 SC-정리(임의 프로그램에 대해 all-sc ⇒ SC)는 여기서 증명하지 않는다 —
 *   그것은 iGPS/Cosmo 급의 작업이고 RFC-0018 §8-1 이 그렇게 적어 뒀다.
 *   여기서 증명하는 것은 **유한 모델에서 결정 가능한 핵심**: 두 표준 litmus test 를
 *   **기계가 확인한다.**
 *     · **SB**(store buffering): 기본 seq_cst 면 SB(0,0) 불가능 · relaxed 면 가능·경합.
 *       SB 를 막는 것은 **SC 축**이다(서로 다른 두 위치).
 *     · **MP**(message passing, 2026-07-20 추가): release/acquire 면 stale read 불가능 ·
 *       relaxed 면 가능. MP 를 막는 것은 **coherence 축(hb;eco, per-location)**이다 —
 *       그래서 이 파일에 그 축을 RC11 표준대로 보탰다(fr 간선을 실제로 쓴다).
 *   ⇒ **양쪽 방향**이 기계 위에 있다: "기본값은 안전"(SB)과 "명시하면 동기화"(MP).
 *   그리고 **경합이 없으면 ordering 이 아예 안 보인다**(drf 방향, LowentDRF.v 와 맞물림).
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 접근과 ordering ────────────────────────────────────────────────────── *)

Definition loc := nat.
Definition tid := nat.
Definition val := nat.

(* RFC-0018 M-C 의 5종. consume 은 없다(함정 회피). *)
Inductive mode := Rlx | Acq | Rel | AcqRel | SC.

Definition is_sc (m : mode) : bool := match m with SC => true | _ => false end.

(* 읽기는 acquire 쪽을, 쓰기는 release 쪽을 본다. *)
Definition reads_acq (m : mode) : bool :=
  match m with Acq | AcqRel | SC => true | _ => false end.
Definition writes_rel (m : mode) : bool :=
  match m with Rel | AcqRel | SC => true | _ => false end.

(* ★★ **fence 를 넣는다** (2026-07-30). 이 모델의 가장 큰 빈칸이었다 — litmus 배터리에
   fence 시험이 하나도 없었고, 그 이유는 **모델에 fence 이벤트가 없었기** 때문이다.
   fence 는 메모리에 접근하지 않는다: 자리(e_loc)·값(e_val)은 뜻이 없고, **동기화만** 한다. *)
Inductive ekind := Rd | Wr | Fnc.

Record event := mk_ev {
  e_id   : nat;      (* 유일 식별자 *)
  e_tid  : tid;
  e_idx  : nat;      (* 스레드 안의 프로그램 순서 *)
  e_kind : ekind;
  e_loc  : loc;
  e_val  : val;      (* 쓰기: 쓴 값 · 읽기: 읽은 값 *)
  e_mode : mode
}.

Definition exec := list event.

(* ── 2. 관계 ──────────────────────────────────────────────────────────────── *)

(* program order: 같은 스레드, 인덱스가 앞선다 *)
Definition po (a b : event) : bool :=
  Nat.eqb (e_tid a) (e_tid b) && Nat.ltb (e_idx a) (e_idx b).

(* reads-from: rf 는 읽기 id → 쓰기 id 의 부분 함수(리스트로 준다) *)
Definition rfmap := list (nat * nat).

Fixpoint rf_of (rf : rfmap) (r : nat) : option nat :=
  match rf with
  | [] => None
  | (x, w) :: t => if Nat.eqb x r then Some w else rf_of t r
  end.

Definition find_ev (E : exec) (i : nat) : option event :=
  find (fun e => Nat.eqb (e_id e) i) E.

(* ★ synchronizes-with: **release 쓰기 → acquire 읽기**, 그 읽기가 그 쓰기를 읽을 때.
   (seq_cst 는 양쪽 다이므로 자동으로 포함된다 — 그것이 기본값이 강한 이유다.) *)
(* 쓰기 a 에서 읽기 b 로 rf 간선이 있고 스레드가 다른가 — fence 규칙들이 공유하는 조각. *)
Definition rf_cross (rf : rfmap) (a b : event) : bool :=
  negb (Nat.eqb (e_tid a) (e_tid b)) &&
  match rf_of rf (e_id b) with
  | Some w => Nat.eqb w (e_id a)
  | None => false
  end.

Definition sw_base (rf : rfmap) (a b : event) : bool :=
  match e_kind a, e_kind b with
  | Wr, Rd => writes_rel (e_mode a) && reads_acq (e_mode b) && rf_cross rf a b
  | _, _ => false
  end.

(* ★★★ **fence 를 통한 동기화** (RC11 의 sw 는 fence 를 양끝에 붙일 수 있게 한다):
     sw = [Rel] ; ([F] ; po)? ; [W] ; rf ; [R] ; (po ; [F])? ; [Acq]
   우리가 노출하는 부분집합에 맞춰 **세 모양**을 그대로 적는다:
     (F1) release **fence** ; po ; W(어떤 모드든) ; rf ; R(acquire)   → sw(F_rel, R)
     (F2) W(release) ; rf ; R(어떤 모드든) ; po ; acquire **fence**   → sw(W, F_acq)
     (F3) release fence ; po ; W ; rf ; R ; po ; acquire fence        → sw(F_rel, F_acq)
   ★ 이것이 fence 의 실제 값이다: **relaxed 접근을 쓰면서도 동기화**할 수 있다.
     그래서 MP 를 `relaxed 쓰기 + release fence` 로도 고칠 수 있다 — 그 시험이 Sweep 에 있다. *)
Definition sw (E : exec) (rf : rfmap) (a b : event) : bool :=
  sw_base rf a b
  || (* F1 *)
  (match e_kind a, e_kind b with
   | Fnc, Rd => writes_rel (e_mode a) && reads_acq (e_mode b) &&
                existsb (fun w => match e_kind w with
                                  | Wr => po a w && rf_cross rf w b
                                  | _ => false end) E
   | _, _ => false end)
  || (* F2 *)
  (match e_kind a, e_kind b with
   | Wr, Fnc => writes_rel (e_mode a) && reads_acq (e_mode b) &&
                existsb (fun r => match e_kind r with
                                  | Rd => rf_cross rf a r && po r b
                                  | _ => false end) E
   | _, _ => false end)
  || (* F3 *)
  (match e_kind a, e_kind b with
   | Fnc, Fnc => writes_rel (e_mode a) && reads_acq (e_mode b) &&
                 existsb (fun w => match e_kind w with
                   | Wr => po a w &&
                           existsb (fun r => match e_kind r with
                                             | Rd => rf_cross rf w r && po r b
                                             | _ => false end) E
                   | _ => false end) E
   | _, _ => false end).

(* happens-before = (po ∪ sw)⁺ — 유한 그래프이므로 **횟수를 정해 닫는다**(길이 ≤ |E|). *)
Definition edge (E : exec) (rf : rfmap) (a b : event) : bool :=
  po a b || sw E rf a b.

(* ★ 전이 폐포는 **간선 목록**으로 계산한다. 함수를 재귀로 닫으면 지수적으로 터진다 —
   처음 쓴 모델이 그랬다(계산이 끝나지 않았다). 관계를 **자료**로 두면 다항이다. *)
Definition rel := list (nat * nat).

Definition has (R : rel) (i j : nat) : bool :=
  existsb (fun p => Nat.eqb (fst p) i && Nat.eqb (snd p) j) R.

(* ★ **중복 제거**가 없으면 한 걸음마다 목록이 **제곱으로** 커진다(10 → 10² → 10⁴ …).
   여섯 걸음이면 10^64 — 계산이 끝나지 않는다. 처음 쓴 폐포가 그랬다.
   관계는 **집합**이다. 집합으로 다루면 크기가 |ids|² 로 묶인다. *)
Definition dedup (R : rel) : rel :=
  fold_right (fun p acc => if has acc (fst p) (snd p) then acc else p :: acc) [] R.

(* 한 걸음 합성: R ∘ R 을 R 에 얹는다(그리고 집합으로 되돌린다). *)
Definition step (R : rel) : rel :=
  dedup (R ++ flat_map (fun p =>
           flat_map (fun q => if Nat.eqb (snd p) (fst q) then [(fst p, snd q)] else []) R) R).

Fixpoint closure (n : nat) (R : rel) : rel :=
  match n with 0 => R | S k => closure k (step R) end.

Definition ids (E : exec) : list nat := map e_id E.

Definition edges (E : exec) (rf : rfmap) : rel :=
  flat_map (fun a => flat_map (fun b =>
     if edge E rf a b then [(e_id a, e_id b)] else []) E) E.

(* hb = (po ∪ sw)⁺ *)
Definition hb_rel (E : exec) (rf : rfmap) : rel :=
  closure (length E) (edges E rf).

Definition hb (E : exec) (rf : rfmap) (a b : event) : bool :=
  has (hb_rel E rf) (e_id a) (e_id b).

(* ── 3. RC11 일관성 공리 (우리가 노출하는 부분집합에 맞춘 핵심) ───────────── *)

(* ★ fence 는 **접근이 아니다** — 충돌의 대상이 아니다. (fence 를 넣으면서 이 정의를 함께
   고쳐야 한다: 안 고치면 fence 가 같은 자리 번호를 가졌다는 이유로 **경합으로 세어진다.**
   자리 번호는 fence 에게 뜻이 없는데.) *)
Definition is_access (k : ekind) : bool :=
  match k with Rd | Wr => true | Fnc => false end.

Definition conflicting (a b : event) : bool :=
  is_access (e_kind a) && is_access (e_kind b) &&
  Nat.eqb (e_loc a) (e_loc b) &&
  negb (Nat.eqb (e_id a) (e_id b)) &&
  match e_kind a, e_kind b with Rd, Rd => false | _, _ => true end.

(* (C1) hb 비반사 — 시간이 자기 앞으로 흐르지 않는다. *)
Definition hb_irreflexive (E : exec) (rf : rfmap) : bool :=
  forallb (fun e => negb (hb E rf e e)) E.

(* (C2) 읽기는 **같은 자리의 쓰기**를 읽고, **그 값**을 읽는다. *)
Definition rf_wellformed (E : exec) (rf : rfmap) : bool :=
  forallb (fun r =>
    match e_kind r with
    | Rd => match rf_of rf (e_id r) with
            | Some wi => match find_ev E wi with
                         | Some w => match e_kind w with
                                     | Wr => Nat.eqb (e_loc w) (e_loc r) &&
                                             Nat.eqb (e_val w) (e_val r)
                                     | Rd | Fnc => false     (* 읽기는 **쓰기**를 읽는다 *)
                                     end
                         | None => false
                         end
            | None => false     (* 초기값도 명시적 쓰기 이벤트로 둔다 — 읽기는 언제나 무언가를 읽는다 *)
            end
    | Wr | Fnc => true          (* fence 는 rf 를 갖지 않는다 *)
    end) E.

(* (C3) **coherence**: 읽기가 자기보다 hb 로 앞선 쓰기를 **덮어쓴** 쓰기를 읽을 수는 없다.
   ★ 이 한 공리가 "읽기는 과거를 본다" 를 말한다. 그리고 이것이 SB 에서 결정적이다:
     같은 스레드의 **앞선 쓰기**(po ⊆ hb)를 읽기가 **못 본 척** 할 수 없다. *)
Definition coherence (E : exec) (rf : rfmap) : bool :=
  forallb (fun r =>
    match e_kind r with
    | Rd =>
        match rf_of rf (e_id r) with
        | Some wi =>
            (* r 이 읽은 쓰기 w. r 보다 hb 로 앞서고 w 와 같은 자리인 쓰기 w' 가 있는데
               w' 가 w 보다 **나중**(w hb w')이면 모순이다 — r 은 낡은 값을 본 것이다. *)
            forallb (fun w' =>
              match e_kind w' with
              | Wr =>
                  negb (Nat.eqb (e_loc w') (e_loc r) &&
                        hb E rf w' r &&
                        match find_ev E wi with
                        | Some w => hb E rf w w'
                        | None => false
                        end)
              | Rd | Fnc => true      (* 쓰기가 아니면 덮어쓸 수 없다 *)
              end) E
        | None => true
        end
    | Wr | Fnc => true
    end) E.

(* ── 3b. ★★★ **SC 공리** — seq_cst 의 힘은 **여기서** 온다 ──────────────────
   coherence 만으로는 seq_cst 가 SB 를 막지 못한다. 처음 쓴 모델이 그것을 보여 줬다:
   SB(0,0) 이 **모든 coherence 공리를 만족했다** — modes 가 전부 SC 여도.
   ★ **그것이 RC11 이 실제로 하는 말이다.** seq_cst 의 강함은 coherence 가 아니라
     **별도의 공리**(psc 비순환)에서 온다. 그 공리를 빼먹으면 "기본 seq_cst" 는
     **아무것도 보장하지 않는다** — 모델이 그것을 기계로 가르쳐 줬다.

   psc = (hb ∪ eco) 를 **seq_cst 이벤트들 위로** 제한한 관계. 그것이 **비순환**이어야 한다.
     eco("extended coherence order") = rf ∪ mo ∪ fr
       rf : 쓰기 → 그것을 읽은 읽기
       mo : 같은 자리 쓰기들의 전순서(modification order)
       fr : 읽기 → 그 읽기가 읽은 쓰기를 **덮어쓴** 쓰기 ("from-read")
   ─────────────────────────────────────────────────────────────────────────── *)

(* modification order: 같은 자리 쓰기들의 순서. (w1, w2) = w1 이 w2 보다 **앞선다**. *)
Definition momap := list (nat * nat).

Definition mo_edge (mo : momap) (a b : event) : bool :=
  existsb (fun p => Nat.eqb (fst p) (e_id a) && Nat.eqb (snd p) (e_id b)) mo.

(* rf 간선: 쓰기 a → 읽기 b *)
Definition rf_edge (rf : rfmap) (a b : event) : bool :=
  match rf_of rf (e_id b) with
  | Some w => Nat.eqb w (e_id a)
  | None => false
  end.

(* ★ from-read: 읽기 a 가 읽은 쓰기 w 를 **덮어쓴** 쓰기 b.  a 는 b 보다 **먼저** 일어난 셈이다
   (b 를 봤다면 b 의 값을 읽었을 것이므로). 약한 메모리의 SB 를 막는 것이 바로 이 간선이다. *)
Definition fr_edge (rf : rfmap) (mo : momap) (a b : event) : bool :=
  match e_kind a with
  | Rd => match rf_of rf (e_id a) with
          | Some w => existsb (fun p => Nat.eqb (fst p) w && Nat.eqb (snd p) (e_id b)) mo
          | None => false
          end
  | Wr | Fnc => false
  end.

Definition eco (rf : rfmap) (mo : momap) (a b : event) : bool :=
  rf_edge rf a b || mo_edge mo a b || fr_edge rf mo a b.

(* ═══ ★★★ **coherence 축 (RC11 표준: hb;eco 비순환)** ═══════════════════════════
   위의 `coherence E rf` 는 두 쓰기의 순서를 **hb 로만** 봤다 — 그래서 초기화가 다른
   스레드에 있어 hb 로 앞서지 않는 MP 같은 패턴을 못 막았다(fr 을 안 봤다).
   RC11 의 진짜 coherence 는 **`hb ; eco` 가 비순환**이라는 것이다(eco = rf∪mo∪fr).
   ★ MP 를 막는 것이 바로 이 fr 간선이다: acquire 읽기가 release 로 hb 앞선 쓰기를
     덮어쓴 초기화를 읽으면 fr 이 그 읽기를 그 쓰기보다 **앞**에 놓아 순환을 만든다.
   ☞ 기존 `coherence E rf` 는 그대로 둔다(SB 가 그 위에 서 있다) — 이건 **더 강한 축을
     보탠 것**이다. 둘 다 요구하면 진짜 RC11 에 더 가깝다. *)
(* hb 간선과 eco 간선을 합치되 — ★ **coherence 는 per-location 이다.** eco 간선은 같은
   위치의 접근만 잇는다(rf/mo/fr 은 정의상 같은 위치다). hb 는 전역 순서이므로 그대로 둔다.
   ☞ 이 per-location 제한이 없으면 SB(서로 다른 두 위치 x·y)의 hb 간선과 eco 간선이 섞여
     거짓 순환이 생겨 **SB relaxed 를 불일치로 오판한다**(SB 를 막는 것은 coherence 가 아니라
     SC 축이다 — 그래서 이 둘은 갈라져 있어야 한다). 실측으로 그 오판을 밟고 이 제한을 넣었다. *)
Definition hb_eco_edges (E : exec) (rf : rfmap) (mo : momap) : rel :=
  flat_map (fun a => flat_map (fun b =>
     if (hb E rf a b && Nat.eqb (e_loc a) (e_loc b)) || eco rf mo a b
     then [(e_id a, e_id b)] else []) E) E.
Definition coh_rel (E : exec) (rf : rfmap) (mo : momap) : rel :=
  closure (length E) (hb_eco_edges E rf mo).
Definition coh_acyclic (E : exec) (rf : rfmap) (mo : momap) : bool :=
  forallb (fun e => negb (has (coh_rel E rf mo) (e_id e) (e_id e))) E.

(* psc 간선 — **양 끝이 seq_cst 일 때만.** relaxed 는 이 관계에 아예 들어오지 않는다. *)
Definition psc_edge (E : exec) (rf : rfmap) (mo : momap) (a b : event) : bool :=
  is_sc (e_mode a) && is_sc (e_mode b) &&
  (hb E rf a b || eco rf mo a b).

Definition psc_edges (E : exec) (rf : rfmap) (mo : momap) : rel :=
  flat_map (fun a => flat_map (fun b =>
     if psc_edge E rf mo a b then [(e_id a, e_id b)] else []) E) E.

Definition psc_rel (E : exec) (rf : rfmap) (mo : momap) : rel :=
  closure (length E) (psc_edges E rf mo).

(* ★★★ SC 공리: psc 는 **비순환**이다. *)
Definition sc_acyclic (E : exec) (rf : rfmap) (mo : momap) : bool :=
  forallb (fun e => negb (has (psc_rel E rf mo) (e_id e) (e_id e))) E.

Definition consistent (E : exec) (rf : rfmap) (mo : momap) : bool :=
  hb_irreflexive E rf && rf_wellformed E rf && coherence E rf &&
  coh_acyclic E rf mo && sc_acyclic E rf mo.

(* ── 4. Store buffering — 약한 메모리의 표준 litmus test ───────────────────── *)
(*
     초기: x = 0, y = 0
     T1:  W x 1 ;  R y → a
     T2:  W y 1 ;  R x → b
     SC 에서는 (a,b) = (0,0) 이 **불가능**하다. 어떤 인터리빙에서도 둘 중 하나는
     상대의 쓰기를 본다. 그런데 **relaxed 에서는 가능하다** — 그것이 이 모델의 요점이다.
*)

Definition x : loc := 0.
Definition y : loc := 1.

(* 초기화 쓰기(스레드 0) — 읽기가 언제나 무언가를 읽도록 명시적 이벤트로 둔다. *)
Definition ix (m : mode) := mk_ev 0 0 0 Wr x 0 m.
Definition iy (m : mode) := mk_ev 1 0 1 Wr y 0 m.

Definition SB (m : mode) : exec :=
  [ ix m ; iy m
  ; mk_ev 2 1 0 Wr x 1 m      (* T1: W x 1 *)
  ; mk_ev 3 1 1 Rd y 0 m      (* T1: R y → 0  ← SC 를 깨는 결과 *)
  ; mk_ev 4 2 0 Wr y 1 m      (* T2: W y 1 *)
  ; mk_ev 5 2 1 Rd x 0 m      (* T2: R x → 0  ← SC 를 깨는 결과 *)
  ].

(* 두 읽기가 **초기값**을 읽는다 = (0,0) *)
Definition rf_00 : rfmap := [ (3, 1) ; (5, 0) ].

(* modification order: 각 자리마다 초기 쓰기가 새 쓰기보다 앞선다.
     x: init(0) <mo Wx1(2)      y: init(1) <mo Wy1(4) *)
Definition mo_SB : momap := [ (0, 2) ; (1, 4) ].

(* ★★ ② **약한 ordering 은 진짜로 SC 를 깬다.**
   relaxed 면 SB(0,0) 이 **일관성 공리를 모두 만족한다** — psc 는 seq_cst 이벤트만 보므로
   여기서는 **공허하게 참**이다. 그것이 relaxed 의 값이자 위험이다. *)
Theorem sb_relaxed_is_consistent : consistent (SB Rlx) rf_00 mo_SB = true.
Proof. vm_compute. reflexivity. Qed.

(* ★★★ ① **기본값(seq_cst)이면 그 결과가 불가능하다.**
   psc 안에 **순환**이 생긴다:
       Wx1 --hb--> Ry0 --fr--> Wy1 --hb--> Rx0 --fr--> Wx1
   (fr: Ry0 은 init y 를 읽었는데 Wy1 이 그것을 덮어쓴다 ⇒ Ry0 이 Wy1 보다 먼저다.)
   순환이면 그런 실행은 **없다.** 그래서 "기본 seq_cst" 가 위안이 아니라 **보장**이다. *)
Theorem sb_sc_impossible : consistent (SB SC) rf_00 mo_SB = false.
Proof. vm_compute. reflexivity. Qed.

(* ★ 그리고 그 순환이 **정확히 SC 공리에서** 나온다 — coherence 는 SB(0,0) 을 막지 못한다.
   처음 쓴 모델이 그것을 보여 줬다(SC 여도 통과했다). **모델이 기계로 가르쳐 준 사실이다.** *)
Theorem coherence_alone_does_not_forbid_sb :
  hb_irreflexive (SB SC) rf_00 && rf_wellformed (SB SC) rf_00 && coherence (SB SC) rf_00 = true.
Proof. vm_compute. reflexivity. Qed.

Theorem it_is_the_sc_axiom_that_forbids_it :
  sc_acyclic (SB SC) rf_00 mo_SB = false.
Proof. vm_compute. reflexivity. Qed.

(* 그리고 seq_cst 에서 **가능한** 결과는 있다 — 모델이 공허하지 않다(모두 거부하는 모델은
   아무것도 말하지 않는다). T1 이 T2 의 쓰기를 본다: R y → 1. *)
Definition SB_sc_ok : exec :=
  [ ix SC ; iy SC
  ; mk_ev 2 1 0 Wr x 1 SC
  ; mk_ev 3 1 1 Rd y 1 SC      (* T2 의 쓰기를 본다 *)
  ; mk_ev 4 2 0 Wr y 1 SC
  ; mk_ev 5 2 1 Rd x 1 SC      (* T1 의 쓰기를 본다 *)
  ].
Definition rf_ok : rfmap := [ (3, 4) ; (5, 2) ].
Definition mo_ok : momap := [ (0, 2) ; (1, 4) ].

Theorem sb_sc_has_a_consistent_execution : consistent SB_sc_ok rf_ok mo_ok = true.
Proof. vm_compute. reflexivity. Qed.

(* ── 5. ★★ 그래서 level 1·2 는 RC11 을 **아예 안 본다** ──────────────────── *)
(*
   LowentDRF.v: 안전 코드(level 1 disjoint · level 2 actor)에는 **경합이 없다**(Qed).
   경합이 없다 = **충돌하는 두 접근이 언제나 hb 로 순서지어져 있다.**
   그러면 **ordering 이 결과를 바꿀 수 없다**: 아래를 증명한다 —
     경합이 없는 실행에서는 각 읽기가 읽을 수 있는 쓰기가 **coherence 로 유일하게 결정된다.**
   즉 **ordering 을 relaxed 로 낮춰도 결과가 같다** ⇒ 메모리 모델이 **안 보인다.**
*)

Definition race_free (E : exec) (rf : rfmap) : bool :=
  forallb (fun a =>
    forallb (fun b =>
      negb (conflicting a b) || hb E rf a b || hb E rf b a) E) E.

(* ★ 경합이 없으면, 읽기가 읽은 쓰기는 **hb 로 그 읽기보다 앞선다.**
   (읽기와 쓰기가 충돌하므로 hb 로 순서지어져 있고, 뒤일 수는 없다 — 그러면 미래를 읽는 것이다.) *)
Lemma rf_is_in_the_past :
  forall E rf mo r w,
    race_free E rf = true ->
    consistent E rf mo = true ->
    In r E -> In w E ->
    e_kind r = Rd ->
    rf_of rf (e_id r) = Some (e_id w) ->
    e_kind w = Wr ->
    e_loc w = e_loc r ->
    e_id r <> e_id w ->
    hb E rf w r = true \/ hb E rf r w = true.
Proof.
  intros E rf mo r w Hrf Hc Hr Hw Kr Hmap Kw Hl Hne.
  unfold race_free in Hrf.
  rewrite forallb_forall in Hrf.
  specialize (Hrf w Hw).
  rewrite forallb_forall in Hrf.
  specialize (Hrf r Hr).
  unfold conflicting in Hrf.
  rewrite Hl, Kr, Kw in Hrf.
  rewrite Nat.eqb_refl in Hrf.
  destruct (Nat.eqb (e_id w) (e_id r)) eqn:Eid.
  - apply Nat.eqb_eq in Eid. lia.
  - simpl in Hrf.
    destruct (hb E rf w r) eqn:H1; [ now left | ].
    destruct (hb E rf r w) eqn:H2; [ now right | ].
    simpl in Hrf. discriminate.
Qed.

(* ★★★ **경합이 없으면 약한 거동이 없다.**
   SB 의 (0,0) 결과는 **경합이 있는** 실행이다 — 그리고 그것이 요점이다:
   level 1·2 는 그런 실행을 **만들 수가 없다**(LowentDRF.v, Qed).
   여기서는 그 SB 실행이 실제로 **경합을 담고 있음**을 기계가 확인한다. *)
Theorem sb_relaxed_has_a_race : race_free (SB Rlx) rf_00 = false.
Proof. vm_compute. reflexivity. Qed.

(* ═══════════════════════════════════════════════════════════════════════════
   ★★★ **MP — message passing**: acquire/release 가 *실제로 작동한다*.

   SB 는 seq_cst 만 다뤘다(그리고 SB 를 막는 것은 SC 축이었다). MP 는 **약한 ordering 이
   무엇을 위해 있는가**를, 그리고 위에서 보탠 **coherence(hb;eco)** 축이 무엇을 하는가를 보인다:

     T1:  W data 1  (평범)          T2:  R flag  (acquire)
          W flag 1  (release)            R data  (평범)

   나쁜 결과 = **flag=1 을 읽고도 data=0 을 읽는다**(release 가 앞선 쓰기를 안 넘겨줬다).

   ① **release/acquire 면 불가능**: sw(3→4)가 hb 2→5 를 만들고, data-읽기 5 가 초기화 0 을
      읽는데 data-쓰기 2 가 0 보다 mo-나중이므로 fr(5→2)이 선다 — hb 2→5 와 fr 5→2 가 **같은
      위치(data)에서 순환**을 이뤄 coh_acyclic 이 막는다.
   ② **relaxed 면 가능**: sw 가 없어 hb 2→5 가 안 생기므로 순환이 없다.

   ★ 이것이 RFC-0018 M-C 의 *"약한 ordering 을 **명시하면** 동기화된다"* 를 세운다.
     SB(기본 seq_cst 가 안전)와 **짝**이다 — 이제 양쪽 방향이 기계 위에 있다.
     그리고 위에서 보탠 coherence 축이 **거저 얹힌 게 아니라 이 정리가 그것을 실제로 쓴다.**
   ═══════════════════════════════════════════════════════════════════════════ *)

Definition MP (wm rm : mode) : exec :=
  [ ix Rlx ; iy Rlx
  ; mk_ev 2 1 0 Wr x 1 Rlx     (* T1: W data 1 (평범) *)
  ; mk_ev 3 1 1 Wr y 1 wm      (* T1: W flag 1 (release 후보) *)
  ; mk_ev 4 2 0 Rd y 1 rm      (* T2: R flag → 1 (acquire 후보) *)
  ; mk_ev 5 2 1 Rd x 0 Rlx     (* T2: R data → 0  ← 나쁜 결과 *)
  ].
(* flag-읽기(4)←flag-쓰기(3); data-읽기(5)←초기화(0). data-쓰기(2)는 초기화(0)보다 mo-나중. *)
Definition rf_MP : rfmap := [ (4, 3) ; (5, 0) ].
Definition mo_MP : momap := [ (0, 2) ; (1, 3) ].

Theorem mp_relacq_forbids_stale : consistent (MP Rel Acq) rf_MP mo_MP = false.
Proof. vm_compute. reflexivity. Qed.

Theorem mp_relaxed_allows_stale : consistent (MP Rlx Rlx) rf_MP mo_MP = true.
Proof. vm_compute. reflexivity. Qed.

(* ③ 차이를 만드는 것은 **동기화(sw)** 다 — release 쓰기와 acquire 읽기 사이에만 선다. *)
Theorem mp_sync_is_what_forbids_it :
  sw (MP Rel Acq) rf_MP (mk_ev 3 1 1 Wr y 1 Rel) (mk_ev 4 2 0 Rd y 1 Acq) = true /\
  sw (MP Rlx Rlx) rf_MP (mk_ev 3 1 1 Wr y 1 Rlx) (mk_ev 4 2 0 Rd y 1 Rlx) = false.
Proof. split; vm_compute; reflexivity. Qed.

(*
   ⇒ 세 조각이 맞물린다:

     LowentDRF.v      안전 코드(level 1·2)에는 **경합이 없다**                     (Qed)
     여기 (RC11)      약한 거동(SB 0,0)은 **경합이 있는 실행에서만** 나온다        (Qed)
     여기 (RC11)      **기본값 seq_cst 면 그 결과가 불가능하다**                   (Qed)
     여기 (RC11)      **relaxed 는 그것을 허용한다** — 그래서 명시 + audit 이다    (Qed)
     여기 (RC11)      **release/acquire 면 stale read 불가능**(MP · coherence 축)     (Qed)
     여기 (RC11)      **relaxed 면 동기화가 없어 가능**(MP)                         (Qed)

   ⇒ RFC-0018 의 결정(**기본 seq_cst · 약한 ordering 은 명시+audit · level-3 한정**)이
     **정리 위에 선다.**

   ★ 남는 것(정직하게): RC11 **전체**의 SC-정리(임의 프로그램에 대해 all-sc ⇒ SC)는
     여기서 증명하지 않았다. 그것은 iGPS/Cosmo 급의 작업이다(RFC-0018 §8-1 이 옳다).
     여기서 한 것은 **유한 모델에서 결정 가능한 핵심을 기계가 확인**하는 것이다 —
     그리고 그 핵심이 바로 **설계 결정이 기대는 명제**다.
*)

(* LowentRC11Mono.v — ★★★ **단조성을 전수 검사에서 정리로 올린다.** 순수 Coq.
 *
 * 무엇이 남아 있었나. `LowentRC11Sweep.v` §3 은 단조성을 **유계 전수**로만 확인했다
 * (3 이벤트 골격 둘 · mode 5종 · 한 칸 약화 전부), 그리고 §"6 이벤트 단조성" 은 그것을
 * 6 이벤트로 넓히려다 **계산이 닫히지 않았다**고 적었다(20 분을 넘겨도 끝나지 않았다).
 * `LowentRC11SC.v` §4 는 그 간극을 이렇게 적어 두었다:
 *
 *     "§3 의 단조성은 **간선 수준**까지다(sw_base_weaken). 그것을 '그러므로 consistent 가
 *      보존된다' 로 잇는 것도 남은 일이다 — 다만 방향은 확인됐다: 모든 공리에서
 *      **hb 간선이 늘면 더 어려워진다**."
 *
 * ★ 이 파일이 그것을 **잇는다.** 그리고 잇고 나면 봉투가 사라진다:
 *   6 이벤트도, 60 이벤트도, **모든 실행 · 모든 rf · 모든 mo · 모든 mode 배정**이다.
 *   ⇒ *"계산이 안 끝나서 못 한다"* 를 **계산하지 않는 것**으로 푼다. 이 저장소에서
 *     두 번째로 만나는 모양이다(`LowentOrderExt.v` 의 전이성 → `stable_on` 판정).
 *
 * ★★ 잇는 도중에 **모델의 결함을 하나 찾았다** (§1). 옛 세기 순서 `stronger` 는
 *   `stronger AcqRel SC = true` 라고 말한다 — 그런데 SB 는 AcqRel 에서 **일관**이고
 *   SC 에서 **불일관**이다. 즉 그 순서로는 단조성이 **거짓**이다. 유계 전수가 그것을
 *   못 본 이유도 분명하다: 전수의 골격(CoWR·CoRR)이 **psc 축을 건드리지 않는다.**
 *   ⇒ 15장 ②의 짝이 여기서도 성립한다 — **검사가 못 보는 자리를 증명이 본다.**
 *
 * 증명하는 것(§4):
 *
 *     weakens E E'  →  consistent E rf mo = true  →  consistent E' rf mo = true
 *
 *   평서문: **약하게 하면 거동이 늘어난다(줄지 않는다).** 세게 적은 프로그램이 허용하는
 *   실행은 약하게 적어도 전부 허용된다. ⇒ *"ordering 을 세게 적는 것은 언제나 안전한
 *   방향이다"* 가 **정리**가 된다. RFC-0018 이 "기본 seq_cst" 를 고른 근거가 이 문장이다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.
Require Import LowentRC11 LowentRC11SC LowentRC11Sweep.

(* ── 0. 관계를 **원소로** 다루는 다리 ──────────────────────────────────── *)

(* `has` 는 계산용(bool)이고 `In` 은 증명용이다. 이 둘을 잇는 한 줄이 이 파일의 바닥이다. *)
Lemma has_In : forall R i j, has R i j = true <-> In (i, j) R.
Proof.
  intros R i j. unfold has. rewrite existsb_exists. split.
  - intros [p [Hin Hp]]. apply andb_true_iff in Hp as [H1 H2].
    apply Nat.eqb_eq in H1. apply Nat.eqb_eq in H2.
    destruct p as [x y]; simpl in *; subst; exact Hin.
  - intros Hin. exists (i, j). split; [ exact Hin | ].
    simpl. rewrite !Nat.eqb_refl. reflexivity.
Qed.

(* 간선 목록의 원소 판정 — `flat_map` 을 벗긴다. *)
Lemma In_pair_flat : forall (E : exec) (f : event -> event -> bool) i j,
  In (i, j) (flat_map (fun a => flat_map (fun b =>
       if f a b then [(e_id a, e_id b)] else []) E) E) <->
  (exists a b, In a E /\ In b E /\ f a b = true /\ e_id a = i /\ e_id b = j).
Proof.
  intros E f i j. split.
  - intros H. apply in_flat_map in H as [a [Ha H]].
    apply in_flat_map in H as [b [Hb H]].
    destruct (f a b) eqn:Ef; simpl in H; [ | contradiction ].
    destruct H as [H | H]; [ | contradiction ].
    inversion H; subst. exists a, b. repeat split; assumption.
  - intros [a [b [Ha [Hb [Hf [Hi Hj]]]]]].
    apply in_flat_map. exists a. split; [ exact Ha | ].
    apply in_flat_map. exists b. split; [ exact Hb | ].
    rewrite Hf. simpl. left. rewrite Hi, Hj. reflexivity.
Qed.

(* ── 1. ★★★ 옛 세기 순서는 세기 순서가 아니다 ───────────────────────── *)

(* `stronger` 는 **sw 간선만** 재고 `is_sc` 를 안 본다(그 파일이 그렇게 적어 두었다).
   그래서 AcqRel 을 SC 위에 놓는다. 아래가 그것이 왜 문제인지의 **계산된 반례**다. *)
Theorem old_order_is_not_a_strength_order :
  stronger AcqRel SC = true /\                              (* 옛 순서: AcqRel ⊒ SC *)
  run_at sb_E sb_rf sb_mo AcqRel = true /\                  (* 그런데 SB 는 AcqRel 에서 일관 *)
  run_at sb_E sb_rf sb_mo SC = false.                       (* SC 에서는 불일관 *)
Proof. repeat split; vm_compute; reflexivity. Qed.

(* ★ 고친 순서는 그 짝을 **거부한다** — 그래서 아래 §4 가 성립할 수 있다. *)
Theorem fixed_order_rejects_it : stronger_sc AcqRel SC = false.
Proof. reflexivity. Qed.

(* ── 2. 약화 관계 — "mode 만 약하게 한 같은 실행" ─────────────────────── *)

Definition same_shape (a b : event) : Prop :=
  e_id a = e_id b /\ e_tid a = e_tid b /\ e_idx a = e_idx b /\
  e_kind a = e_kind b /\ e_loc a = e_loc b /\ e_val a = e_val b.

Definition weaker_ev (a b : event) : Prop :=
  same_shape a b /\ stronger_sc (e_mode a) (e_mode b) = true.

(* E 가 **센 쪽**, E' 가 **약한 쪽**. *)
Definition weakens (E E' : exec) : Prop := Forall2 weaker_ev E E'.

Lemma same_shape_sym : forall a b, same_shape a b -> same_shape b a.
Proof. intros a b [H1 [H2 [H3 [H4 [H5 H6]]]]]; repeat split; congruence. Qed.

Lemma weakens_length : forall E E', weakens E E' -> length E = length E'.
Proof. intros; eapply Forall2_length; eauto. Qed.

Lemma weakens_In_r : forall E E' b,
  weakens E E' -> In b E' -> exists a, In a E /\ weaker_ev a b.
Proof.
  intros E E' b H. induction H as [| x y r r' Hxy Hr IH]; intros Hin; [ contradiction | ].
  destruct Hin as [Heq | Hin].
  - subst y. exists x. split; [ left; reflexivity | exact Hxy ].
  - destruct (IH Hin) as [a [Ha Hw]]. exists a. split; [ right; exact Ha | exact Hw ].
Qed.

Lemma weakens_In_l : forall E E' a,
  weakens E E' -> In a E -> exists b, In b E' /\ weaker_ev a b.
Proof.
  intros E E' a H. induction H as [| x y r r' Hxy Hr IH]; intros Hin; [ contradiction | ].
  destruct Hin as [Heq | Hin].
  - subst x. exists y. split; [ left; reflexivity | exact Hxy ].
  - destruct (IH Hin) as [b [Hb Hw]]. exists b. split; [ right; exact Hb | exact Hw ].
Qed.

(* find_ev 는 **id 로** 찾는다. id 가 자리마다 같으므로 두 목록이 같은 자리에서 멈춘다. *)
Lemma find_ev_weaken : forall E E' i,
  weakens E E' ->
  match find_ev E i, find_ev E' i with
  | Some a, Some b => same_shape a b
  | None, None => True
  | _, _ => False
  end.
Proof.
  intros E E' i H. induction H as [| x y r r' Hxy Hr IH]; simpl; [ exact I | ].
  destruct Hxy as [Hs Hm]. assert (Hid : e_id x = e_id y) by apply Hs.
  unfold find_ev in *. simpl. rewrite Hid.
  destruct (Nat.eqb (e_id y) i); [ | exact IH ].
  exact Hs.
Qed.

(* ── 3. 간선이 줄어든다 ─────────────────────────────────────────────── *)

Lemma reads_acq_weaken : forall m m',
  stronger m m' = true -> reads_acq m' = true -> reads_acq m = true.
Proof.
  intros m m' H Hr. apply andb_true_iff in H as [H _]. rewrite Hr in H. exact H.
Qed.

Lemma writes_rel_weaken : forall m m',
  stronger m m' = true -> writes_rel m' = true -> writes_rel m = true.
Proof.
  intros m m' H Hw. apply andb_true_iff in H as [_ H]. rewrite Hw in H. exact H.
Qed.

Lemma po_shape : forall a a' b b',
  same_shape a a' -> same_shape b b' -> po a b = po a' b'.
Proof.
  intros a a' b b' [_ [Hta [Hia _]]] [_ [Htb [Hib _]]].
  unfold po. rewrite Hta, Htb, Hia, Hib. reflexivity.
Qed.

Lemma rf_cross_shape : forall rf a a' b b',
  same_shape a a' -> same_shape b b' -> rf_cross rf a b = rf_cross rf a' b'.
Proof.
  intros rf a a' b b' [Hia [Hta _]] [Hib [Htb _]].
  unfold rf_cross. rewrite Hta, Htb, Hia, Hib. reflexivity.
Qed.

(* ★★ **sw 전체**(기본 + fence 세 모양)에 대한 약화 보조정리.
   `LowentRC11SC.v` 의 `sw_base_weaken` 은 fence 갈래를 **의도적으로 뺐다** — fence 갈래가
   *다른* 이벤트를 보기 때문에 "이벤트 열 전체가 mode 만 다르다" 는 가정이 필요해서다.
   ⇒ 여기서는 그 가정(`weakens E E'`)이 **있으므로** 네 갈래를 다 덮는다. *)
Lemma sw_weaken : forall E E' rf a b a' b',
  weakens E E' -> weaker_ev a a' -> weaker_ev b b' ->
  sw E' rf a' b' = true -> sw E rf a b = true.
Proof.
  intros E E' rf a b a' b' HW [Hsa Hma] [Hsb Hmb] H.
  assert (Hsa' := Hsa). assert (Hsb' := Hsb).
  destruct Hsa as [Hida [Htda [Hixa [Hka [Hla Hva]]]]].
  destruct Hsb as [Hidb [Htdb [Hixb [Hkb [Hlb Hvb]]]]].
  apply stronger_sc_stronger in Hma. apply stronger_sc_stronger in Hmb.
  unfold sw in *.
  apply orb_true_iff in H as [H | H3].
  apply orb_true_iff in H as [H | H2].
  apply orb_true_iff in H as [H0 | H1].

  - (* 기본 갈래 *)
    apply orb_true_iff; left. apply orb_true_iff; left. apply orb_true_iff; left.
    eapply sw_base_weaken; eauto.

  - (* F1: release fence ; po ; W ; rf ; R(acq) *)
    apply orb_true_iff; left. apply orb_true_iff; left. apply orb_true_iff; right.
    rewrite Hka, Hkb.
    destruct (e_kind a') eqn:Ea; destruct (e_kind b') eqn:Eb; try discriminate.
    apply andb_true_iff in H1 as [H1 Hex]. apply andb_true_iff in H1 as [Hw Hr].
    rewrite (writes_rel_weaken _ _ Hma Hw), (reads_acq_weaken _ _ Hmb Hr). simpl.
    apply existsb_exists in Hex as [w' [Hw'in Hw'p]].
    destruct (e_kind w') eqn:Ekw; try discriminate.
    destruct (weakens_In_r _ _ _ HW Hw'in) as [w [Hwin [Hsw _]]].
    apply existsb_exists. exists w. split; [ exact Hwin | ].
    assert (Hkw : e_kind w = Wr) by (destruct Hsw as [_ [_ [_ [Hk _]]]]; congruence).
    rewrite Hkw.
    apply andb_true_iff in Hw'p as [Hpo Hrfc].
    rewrite (po_shape a a' w w' Hsa' Hsw), (rf_cross_shape rf w w' b b' Hsw Hsb').
    rewrite Hpo, Hrfc. reflexivity.

  - (* F2: W(rel) ; rf ; R ; po ; acquire fence *)
    apply orb_true_iff; left. apply orb_true_iff; right.
    rewrite Hka, Hkb.
    destruct (e_kind a') eqn:Ea; destruct (e_kind b') eqn:Eb; try discriminate.
    apply andb_true_iff in H2 as [H2 Hex]. apply andb_true_iff in H2 as [Hw Hr].
    rewrite (writes_rel_weaken _ _ Hma Hw), (reads_acq_weaken _ _ Hmb Hr). simpl.
    apply existsb_exists in Hex as [r' [Hr'in Hr'p]].
    destruct (e_kind r') eqn:Ekr; try discriminate.
    destruct (weakens_In_r _ _ _ HW Hr'in) as [r [Hrin [Hsr _]]].
    apply existsb_exists. exists r. split; [ exact Hrin | ].
    assert (Hkr : e_kind r = Rd) by (destruct Hsr as [_ [_ [_ [Hk _]]]]; congruence).
    rewrite Hkr.
    apply andb_true_iff in Hr'p as [Hrfc Hpo].
    rewrite (rf_cross_shape rf a a' r r' Hsa' Hsr), (po_shape r r' b b' Hsr Hsb').
    rewrite Hpo, Hrfc. reflexivity.

  - (* F3: release fence ; po ; W ; rf ; R ; po ; acquire fence *)
    apply orb_true_iff; right.
    rewrite Hka, Hkb.
    destruct (e_kind a') eqn:Ea; destruct (e_kind b') eqn:Eb; try discriminate.
    apply andb_true_iff in H3 as [H3 Hex]. apply andb_true_iff in H3 as [Hw Hr].
    rewrite (writes_rel_weaken _ _ Hma Hw), (reads_acq_weaken _ _ Hmb Hr). simpl.
    apply existsb_exists in Hex as [w' [Hw'in Hw'p]].
    destruct (e_kind w') eqn:Ekw; try discriminate.
    destruct (weakens_In_r _ _ _ HW Hw'in) as [w [Hwin [Hsw _]]].
    apply existsb_exists. exists w. split; [ exact Hwin | ].
    assert (Hkw : e_kind w = Wr) by (destruct Hsw as [_ [_ [_ [Hk _]]]]; congruence).
    rewrite Hkw.
    apply andb_true_iff in Hw'p as [Hpo Hex2].
    rewrite (po_shape a a' w w' Hsa' Hsw), Hpo. simpl.
    apply existsb_exists in Hex2 as [r' [Hr'in Hr'p]].
    destruct (e_kind r') eqn:Ekr; try discriminate.
    destruct (weakens_In_r _ _ _ HW Hr'in) as [r [Hrin [Hsr _]]].
    apply existsb_exists. exists r. split; [ exact Hrin | ].
    assert (Hkr : e_kind r = Rd) by (destruct Hsr as [_ [_ [_ [Hk _]]]]; congruence).
    rewrite Hkr.
    apply andb_true_iff in Hr'p as [Hrfc Hpo2].
    rewrite (rf_cross_shape rf w w' r r' Hsw Hsr), (po_shape r r' b b' Hsr Hsb').
    rewrite Hrfc, Hpo2. reflexivity.
Qed.

Lemma edge_weaken : forall E E' rf a b a' b',
  weakens E E' -> weaker_ev a a' -> weaker_ev b b' ->
  edge E' rf a' b' = true -> edge E rf a b = true.
Proof.
  intros E E' rf a b a' b' HW Ha Hb H. unfold edge in *.
  apply orb_true_iff in H as [H | H]; apply orb_true_iff.
  - left. rewrite (po_shape a a' b b') by (apply Ha || apply Hb). exact H.
  - right. eapply sw_weaken; eauto.
Qed.

(* ★ 약한 쪽의 간선은 **센 쪽에도 있다** — 이 파일의 엔진. *)
Lemma edges_sub : forall E E' rf,
  weakens E E' -> sub (edges E' rf) (edges E rf).
Proof.
  intros E E' rf HW i j Hij.
  apply has_In in Hij. apply In_pair_flat in Hij as [a' [b' [Ha' [Hb' [He [Hi Hj]]]]]].
  destruct (weakens_In_r _ _ _ HW Ha') as [a [Ha Hwa]].
  destruct (weakens_In_r _ _ _ HW Hb') as [b [Hb Hwb]].
  apply has_In. apply In_pair_flat.
  exists a, b. repeat split.
  - exact Ha.
  - exact Hb.
  - eapply edge_weaken; eauto.
  - destruct Hwa as [[Hid _] _]. congruence.
  - destruct Hwb as [[Hid _] _]. congruence.
Qed.

Lemma hb_weaken : forall E E' rf a b a' b',
  weakens E E' -> same_shape a a' -> same_shape b b' ->
  hb E' rf a' b' = true -> hb E rf a b = true.
Proof.
  intros E E' rf a b a' b' HW [Hida _] [Hidb _] H.
  unfold hb, hb_rel in *. rewrite Hida, Hidb.
  rewrite (weakens_length _ _ HW).
  eapply closure_sub; [ apply (edges_sub _ _ rf HW) | exact H ].
Qed.

(* ── 4. ★★★ 네 공리가 각각 보존된다 ─────────────────────────────────── *)

(* (C1) hb 비반사 — hb 가 줄면 자기 순환도 줄어든다. *)
Lemma hb_irreflexive_weaken : forall E E' rf,
  weakens E E' -> hb_irreflexive E rf = true -> hb_irreflexive E' rf = true.
Proof.
  intros E E' rf HW H. unfold hb_irreflexive in H |- *. apply forallb_forall. intros e' He'.
  destruct (weakens_In_r _ _ _ HW He') as [e [He [Hs _]]].
  rewrite forallb_forall in H. specialize (H e He).
  apply negb_true_iff. apply negb_true_iff in H.
  destruct (hb E' rf e' e') eqn:Eh; [ | reflexivity ].
  rewrite (hb_weaken E E' rf e e e' e' HW Hs Hs Eh) in H. discriminate.
Qed.

(* (C2) rf 적격성 — mode 를 아예 보지 않는다(모양만 본다). *)
Lemma rf_wellformed_weaken : forall E E' rf,
  weakens E E' -> rf_wellformed E rf = true -> rf_wellformed E' rf = true.
Proof.
  intros E E' rf HW H. unfold rf_wellformed in H |- *. apply forallb_forall. intros r' Hr'.
  destruct (weakens_In_r _ _ _ HW Hr') as [r [Hr Hw]].
  destruct Hw as [Hs _]. assert (Hs' := Hs).
  destruct Hs as [Hid [_ [_ [Hk [Hl Hv]]]]].
  rewrite forallb_forall in H. specialize (H r Hr).
  rewrite <- Hk, <- Hid.
  destruct (e_kind r); [ | exact H | exact H ].
  destruct (rf_of rf (e_id r)) as [wi | ]; [ | exact H ].
  pose proof (find_ev_weaken E E' wi HW) as Hf.
  destruct (find_ev E wi) as [w | ]; destruct (find_ev E' wi) as [w' | ];
    try contradiction; [ | exact H ].
  destruct Hf as [_ [_ [_ [Hkw [Hlw Hvw]]]]].
  rewrite <- Hkw, <- Hlw, <- Hvw, <- Hl, <- Hv.
  exact H.
Qed.

(* (C3) coherence — hb 가 **부정 아래** 나타난다. 줄면 통과하기 쉬워진다. *)
Lemma coherence_weaken : forall E E' rf,
  weakens E E' -> coherence E rf = true -> coherence E' rf = true.
Proof.
  intros E E' rf HW H. unfold coherence in H |- *. apply forallb_forall. intros r' Hr'.
  destruct (weakens_In_r _ _ _ HW Hr') as [r [Hr Hwr]].
  destruct Hwr as [Hsr _]. assert (Hsr' := Hsr).
  destruct Hsr as [Hidr [_ [_ [Hkr [Hlr _]]]]].
  rewrite forallb_forall in H. specialize (H r Hr).
  rewrite <- Hkr, <- Hidr.
  destruct (e_kind r); [ | reflexivity | reflexivity ].
  destruct (rf_of rf (e_id r)) as [wi | ] eqn:Erf; [ | reflexivity ].
  apply forallb_forall. intros v' Hv'.
  destruct (weakens_In_r _ _ _ HW Hv') as [v [Hv Hwv]].
  destruct Hwv as [Hsv _]. assert (Hsv' := Hsv).
  destruct Hsv as [_ [_ [_ [Hkv [Hlv _]]]]].
  rewrite forallb_forall in H. specialize (H v Hv).
  rewrite <- Hkv.
  destruct (e_kind v); [ reflexivity | | reflexivity ].
  apply negb_true_iff. apply negb_true_iff in H.
  rewrite <- Hlv, <- Hlr.
  destruct (Nat.eqb (e_loc v) (e_loc r)); simpl in H |- *; [ | reflexivity ].
  destruct (hb E' rf v' r') eqn:Ehb1; simpl; [ | reflexivity ].
  rewrite (hb_weaken E E' rf v r v' r' HW Hsv' Hsr' Ehb1) in H. simpl in H.
  pose proof (find_ev_weaken E E' wi HW) as Hf.
  destruct (find_ev E wi) as [w | ]; destruct (find_ev E' wi) as [w' | ];
    try contradiction; [ | reflexivity ].
  destruct (hb E' rf w' v') eqn:Ehb2; [ | reflexivity ].
  rewrite (hb_weaken E E' rf w v w' v' HW Hf Hsv' Ehb2) in H. discriminate.
Qed.

(* eco 는 **mode 를 보지 않는다** — id 와 kind 만 본다. *)
Lemma eco_shape : forall rf mo a a' b b',
  same_shape a a' -> same_shape b b' -> eco rf mo a' b' = true -> eco rf mo a b = true.
Proof.
  intros rf mo a a' b b' Ha Hb H.
  destruct Ha as [Hida [_ [_ [Hka _]]]]. destruct Hb as [Hidb [_ [_ [Hkb _]]]].
  unfold eco, rf_edge, mo_edge, fr_edge in *.
  rewrite Hida, Hidb, Hka. exact H.
Qed.

(* (C4) coherence 축(hb;eco 비순환) — 간선이 줄어든다. *)
Lemma coh_acyclic_weaken : forall E E' rf mo,
  weakens E E' -> coh_acyclic E rf mo = true -> coh_acyclic E' rf mo = true.
Proof.
  intros E E' rf mo HW H.
  assert (Hsub : sub (hb_eco_edges E' rf mo) (hb_eco_edges E rf mo)).
  { intros i j Hij. apply has_In in Hij.
    apply In_pair_flat in Hij as [a' [b' [Ha' [Hb' [He [Hi Hj]]]]]].
    destruct (weakens_In_r _ _ _ HW Ha') as [a [Ha [Hsa _]]].
    destruct (weakens_In_r _ _ _ HW Hb') as [b [Hb [Hsb _]]].
    apply has_In. apply In_pair_flat. exists a, b.
    assert (Hida : e_id a = e_id a') by apply Hsa.
    assert (Hidb : e_id b = e_id b') by apply Hsb.
    split; [ exact Ha | ]. split; [ exact Hb | ]. split; [ | split; congruence ].
    apply orb_true_iff in He as [He | He]; apply orb_true_iff.
    - left. apply andb_true_iff in He as [Hhb Hloc].
      rewrite (hb_weaken E E' rf a b a' b' HW Hsa Hsb Hhb).
      assert (Hla : e_loc a = e_loc a') by apply Hsa.
      assert (Hlb : e_loc b = e_loc b') by apply Hsb.
      rewrite Hla, Hlb, Hloc. reflexivity.
    - right. eapply eco_shape; eauto. }
  unfold coh_acyclic, coh_rel in *.
  apply forallb_forall. intros e' He'.
  destruct (weakens_In_r _ _ _ HW He') as [e [He [Hs _]]].
  rewrite forallb_forall in H. specialize (H e He).
  apply negb_true_iff. apply negb_true_iff in H.
  assert (Hid : e_id e = e_id e') by apply Hs.
  rewrite <- Hid, <- (weakens_length _ _ HW).
  destruct (has (closure (length E) (hb_eco_edges E' rf mo)) (e_id e) (e_id e)) eqn:Ec;
    [ | reflexivity ].
  rewrite (closure_sub (length E) _ _ Hsub _ _ Ec) in H. discriminate.
Qed.

(* (C5) ★ SC 축 — **여기서 `is_sc` 가 필요하다.** 고친 순서가 그것을 준다. *)
Lemma sc_acyclic_weaken : forall E E' rf mo,
  weakens E E' -> sc_acyclic E rf mo = true -> sc_acyclic E' rf mo = true.
Proof.
  intros E E' rf mo HW H.
  assert (Hsub : sub (psc_edges E' rf mo) (psc_edges E rf mo)).
  { intros i j Hij. apply has_In in Hij.
    apply In_pair_flat in Hij as [a' [b' [Ha' [Hb' [He [Hi Hj]]]]]].
    destruct (weakens_In_r _ _ _ HW Ha') as [a [Ha [Hsa Hma]]].
    destruct (weakens_In_r _ _ _ HW Hb') as [b [Hb [Hsb Hmb]]].
    apply has_In. apply In_pair_flat. exists a, b.
    assert (Hida : e_id a = e_id a') by apply Hsa.
    assert (Hidb : e_id b = e_id b') by apply Hsb.
    split; [ exact Ha | ]. split; [ exact Hb | ]. split; [ | split; congruence ].
    unfold psc_edge in *.
    apply andb_true_iff in He as [He Hrest].
    apply andb_true_iff in He as [Hsca Hscb].
    rewrite (stronger_sc_is_sc _ _ Hma Hsca), (stronger_sc_is_sc _ _ Hmb Hscb). simpl.
    apply orb_true_iff in Hrest as [Hhb | Heco]; apply orb_true_iff.
    - left. eapply hb_weaken; eauto.
    - right. eapply eco_shape; eauto. }
  unfold sc_acyclic, psc_rel in *.
  apply forallb_forall. intros e' He'.
  destruct (weakens_In_r _ _ _ HW He') as [e [He [Hs _]]].
  rewrite forallb_forall in H. specialize (H e He).
  apply negb_true_iff. apply negb_true_iff in H.
  assert (Hid : e_id e = e_id e') by apply Hs.
  rewrite <- Hid, <- (weakens_length _ _ HW).
  destruct (has (closure (length E) (psc_edges E' rf mo)) (e_id e) (e_id e)) eqn:Ec;
    [ | reflexivity ].
  rewrite (closure_sub (length E) _ _ Hsub _ _ Ec) in H. discriminate.
Qed.

(* ── 5. ★★★ 단조성 정리 — 봉투가 없다 ───────────────────────────────── *)

(* **약하게 하면 거동이 늘어난다(줄지 않는다).**
   ∀ 실행 · ∀ rf · ∀ mo · ∀ mode 배정. 유계 전수가 아니라 **정리**다. *)
Theorem consistent_monotone : forall E E' rf mo,
  weakens E E' ->
  consistent E rf mo = true ->
  consistent E' rf mo = true.
Proof.
  intros E E' rf mo HW H.
  unfold consistent in *.
  apply andb_true_iff in H as [H Hsc].
  apply andb_true_iff in H as [H Hcoh].
  apply andb_true_iff in H as [H Hcohe].
  apply andb_true_iff in H as [Hirr Hrf].
  rewrite (hb_irreflexive_weaken _ _ _ HW Hirr).
  rewrite (rf_wellformed_weaken _ _ _ HW Hrf).
  rewrite (coherence_weaken _ _ _ HW Hcohe).
  rewrite (coh_acyclic_weaken _ _ _ _ HW Hcoh).
  rewrite (sc_acyclic_weaken _ _ _ _ HW Hsc).
  reflexivity.
Qed.

(* ★ 대우 — 실무에서 쓰는 방향이다.
   **약한 쪽이 금지하면 센 쪽도 금지한다.** ⇒ ordering 을 **세게 적는 것은 언제나 안전**하다.
   (반대는 거짓이다 — 그것이 `LowentRC11Sweep.v` 의 `relaxed_allows_strictly_more_sb`.) *)
Corollary forbidding_is_monotone : forall E E' rf mo,
  weakens E E' ->
  consistent E' rf mo = false ->
  consistent E rf mo = false.
Proof.
  intros E E' rf mo HW H.
  destruct (consistent E rf mo) eqn:Ec; [ | reflexivity ].
  rewrite (consistent_monotone _ _ _ _ HW Ec) in H. discriminate.
Qed.

(* ★★ 그리고 **모든 실행은 all-SC 를 약화한 것**이다 — SC 가 꼭대기이므로.
   ⇒ *"seq_cst 로 적은 프로그램이 허용하는 실행은, 무엇으로 바꿔 적어도 전부 허용된다."*
     RFC-0018 의 "기본 seq_cst" 가 기대는 문장이 이것이다. *)
Lemma all_sc_weakens_to_anything : forall E E',
  length E = length E' ->
  Forall2 (fun a b => same_shape a b) E E' ->
  all_sc E = true ->
  weakens E E'.
Proof.
  intros E E' _ HF Hsc. induction HF as [| x y r r' Hxy Hr IH]; [ constructor | ].
  simpl in Hsc. apply andb_true_iff in Hsc as [Hx Hr'].
  constructor.
  - split; [ exact Hxy | ].
    destruct (e_mode x); try discriminate. apply sc_is_the_top.
  - apply IH. exact Hr'.
Qed.

(* ── 6. ★ 이 정리가 무엇을 **대체했나** ──────────────────────────────────
 *
 * `LowentRC11Sweep.v` §3 의 유계 전수(`mono_ok`)는 이제 **정리의 특수한 경우**다:
 *   전수: 3 이벤트 골격 둘 · 5³ 배정 · 한 칸 약화 · vm_compute 93 초
 *   정리: **모든** 이벤트 열 · **모든** 배정 · **모든** 약화(한 칸이 아니라 전부) · Qed
 * 그리고 6 이벤트가 "계산이 안 끝나서 못 했다" 던 자리도 함께 닫힌다 — 계산하지 않으므로.
 *
 * ★ 남는 것(정직하게):
 *   · 이 정리는 **한 실행에 대한 것**이다. "프로그램이 낼 수 있는 실행 집합" 으로 옮기려면
 *     프로그램→실행 사상이 필요하고, 그 사상은 이 모델에 없다(11장 ⑥).
 *   · `weakens` 는 **mode 만** 바꾼다. 코드를 바꾸는 최적화(재배치·병합)는 다른 문제다.
 *   · 유계 전수를 **지우지 않는다**: 정리는 모델 안의 정리이고, 전수는 그 모델을 **계산으로**
 *     한 번 더 두드린다. 둘이 어긋나면 그 자체가 신호다(15장 ①의 두 벌 원리).
 *)

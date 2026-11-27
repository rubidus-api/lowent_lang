(* LowentPlace.v — V3: **place 일반화**. 지역(region)을 사영 경로(projection path)로 올린다.
 *
 * LowentEXCL.v 는 지역을 평평한 `nat` 으로 봤다. 그래서 `x.a` 와 `x.b` 를 **구별할 수 없다** —
 * 둘 다 그냥 "x" 다. 그 모델에서는 서로 다른 필드의 두 mut 차용이 충돌로 보인다(거짓 양성).
 * 실제 언어는 필드를 구별하고, 구현도 그렇게 한다. 증명이 못 따라가고 있었다.
 *
 * 여기서 지역을 **place** 로 일반화한다:
 *     place = base :: fields      [0]     = 통째로 x
 *                                 [0;1]   = x.f1
 *                                 [0;1;2] = x.f1.f2
 * 그리고 지역 동등성(=)을 **겹침**(overlaps)으로 바꾼다:
 *     두 place 가 겹친다  ⟺  한쪽이 다른 쪽의 접두사다
 *     x 와 x.a 는 겹친다(포함).  x.a 와 x.b 는 **겹치지 않는다**(분리).
 *
 * 그러면 정리 A 가 그대로 성립하는가? — 성립한다. 이 파일이 그것을 기계 증명한다.
 * 평평한 모델은 place 길이 1 의 특수 경우이고(ov = 동등), 그래서 LowentEXCL.v 는
 * 이 정리의 **따름**이다.
 *
 * 갚는 것: UNPROVEN R1 의 "남은 것: … place 일반화(proj)".
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. place 와 겹침 ──────────────────────────────────────────────────────── *)

Definition place := list nat.      (* base :: 필드 경로 *)

Fixpoint peqb (p q : place) : bool :=
  match p, q with
  | [], [] => true
  | a :: p', b :: q' => Nat.eqb a b && peqb p' q'
  | _, _ => false
  end.

Lemma peqb_refl : forall p, peqb p p = true.
Proof. induction p as [| a p IH]; simpl; [ reflexivity | rewrite Nat.eqb_refl, IH; reflexivity ]. Qed.

Lemma peqb_eq : forall p q, peqb p q = true -> p = q.
Proof.
  induction p as [| a p IH]; intros [| b q] H; simpl in H; try discriminate; [ reflexivity | ].
  apply andb_true_iff in H as [Ha Hp]. apply Nat.eqb_eq in Ha; subst b.
  f_equal. apply IH; exact Hp.
Qed.

Lemma peqb_sym : forall p q, peqb p q = peqb q p.
Proof.
  induction p as [| a p IH]; intros [| b q]; simpl; try reflexivity.
  rewrite Nat.eqb_sym, IH; reflexivity.
Qed.

(* 접두사 = **포함**. p ⊑ q ⟺ q 는 p 안에 있다 (x ⊑ x.a). *)
Fixpoint pre (p q : place) : bool :=
  match p, q with
  | [], _ => true
  | _, [] => false
  | a :: p', b :: q' => Nat.eqb a b && pre p' q'
  end.

(* ★ 겹침 — 한쪽이 다른 쪽을 포함하면 겹친다. 형제 필드는 겹치지 않는다. *)
Definition ov (p q : place) : bool := pre p q || pre q p.

Lemma pre_refl : forall p, pre p p = true.
Proof. induction p as [| a p IH]; simpl; [ reflexivity | rewrite Nat.eqb_refl, IH; reflexivity ]. Qed.

Lemma ov_refl : forall p, ov p p = true.
Proof. intros p; unfold ov; rewrite pre_refl; reflexivity. Qed.

Lemma ov_sym : forall p q, ov p q = ov q p.
Proof. intros p q; unfold ov; apply orb_comm. Qed.

Lemma peqb_ov : forall p q, peqb p q = true -> ov p q = true.
Proof. intros p q H; apply peqb_eq in H; subst q; apply ov_refl. Qed.

(* 실증 — 이것이 이 파일의 존재 이유다. *)
Example sibling_fields_are_disjoint : ov [0;1] [0;2] = false.
Proof. reflexivity. Qed.

Example whole_overlaps_field : ov [0] [0;1] = true.
Proof. reflexivity. Qed.

Example nested_overlaps : ov [0;1] [0;1;7] = true.
Proof. reflexivity. Qed.

Example different_bases_are_disjoint : ov [0;1] [9;1] = false.
Proof. reflexivity. Qed.

(* ── 2. 이벤트 ─────────────────────────────────────────────────────────────── *)

Inductive ev : Type :=
  | Create (tag : nat) (p : place) (mut : bool)
  | Use    (tag : nat) (wr : bool)
  | Own    (p : place) (wr : bool).

Fixpoint tag_info (evs : list ev) (t : nat) : option (place * bool) :=
  match evs with
  | [] => None
  | Create t' p m :: r => if Nat.eqb t t' then Some (p, m) else tag_info r t
  | _ :: r => tag_info r t
  end.

Definition wf (evs : list ev) : Prop :=
  forall t p m, In (Create t p m) evs -> tag_info evs t = Some (p, m).

Definition is_mut (evs : list ev) (t : nat) : bool :=
  match tag_info evs t with Some (_, m) => m | None => false end.

(* ── 3. 동적 의미 — place 로 색인된 borrow stack ────────────────────────────── *)
(* 상태를 **함수**로 둔다: place → 태그 스택. 한 걸음이 *겹치는 모든 place* 를 건드리므로
   연관 리스트보다 함수가 정직하고 다루기 쉽다. *)

Definition state := place -> list nat.
Definition empty : state := fun _ => [].

Fixpoint pop_above (st : list nat) (t : nat) : list nat :=
  match st with
  | [] => []
  | u :: r => if Nat.eqb u t then u :: r else pop_above r t
  end.

Fixpoint drop_muts_above (evs : list ev) (st : list nat) (t : nat) : list nat :=
  match st with
  | [] => []
  | u :: r => if Nat.eqb u t then u :: r
              else if is_mut evs u then drop_muts_above evs r t
                   else u :: drop_muts_above evs r t
  end.

Definition drop_muts (evs : list ev) (st : list nat) : list nat :=
  filter (fun u => negb (is_mut evs u)) st.

(* D1 — 차용 생성: 그 place 의 스택에 얹는다. *)
Definition st_create (s : state) (p : place) (t : nat) : state :=
  fun q => if peqb q p then t :: s p else s q.

(* D3 — 차용을 통한 **쓰기**: 자기 place 에서는 t 위를 전부 버리고(t 가 top),
   **겹치는 다른 place** 의 차용은 전부 무효화한다(x 에 쓰면 x.a 의 차용이 죽는다). *)
Definition st_use_wr (s : state) (p : place) (t : nat) : state :=
  fun q => if peqb q p then pop_above (s p) t
           else if ov p q then [] else s q.

(* D2 — 차용을 통한 **읽기**: 자기 place 에서는 t 위의 exc 만, 겹치는 곳에서는 exc 를 버린다. *)
Definition st_use_rd (evs : list ev) (s : state) (p : place) (t : nat) : state :=
  fun q => if peqb q p then drop_muts_above evs (s p) t
           else if ov p q then drop_muts evs (s q) else s q.

(* D5 / D4 — 소유자 접근: 겹치는 모든 place 에 적용. *)
Definition st_own (evs : list ev) (s : state) (p : place) (wr : bool) : state :=
  fun q => if ov p q then (if wr then [] else drop_muts evs (s q)) else s q.

Definition step (evs : list ev) (s : state) (e : ev) : option state :=
  match e with
  | Create t p _ => Some (st_create s p t)
  | Use t wr =>
      match tag_info evs t with
      | None => None
      | Some (p, _) =>
          if existsb (Nat.eqb t) (s p)
          then Some (if wr then st_use_wr s p t else st_use_rd evs s p t)
          else None                                    (* ⚡ 무효화된 차용 사용 *)
      end
  | Own p wr => Some (st_own evs s p wr)
  end.

Fixpoint run (evs : list ev) (s : state) (rest : list ev) : option state :=
  match rest with
  | [] => Some s
  | e :: r => match step evs s e with None => None | Some s' => run evs s' r end
  end.

Definition dyn_clean (evs : list ev) : bool :=
  match run evs empty evs with Some _ => true | None => false end.

(* ── 4. 정적 판정 — 동등성 대신 **겹침** ───────────────────────────────────── *)

Definition live := list (nat * (place * bool)).

Fixpoint alive_of (l : live) (t : nat) : option (place * bool) :=
  match l with
  | [] => None
  | (t', i) :: r => if Nat.eqb t t' then Some i else alive_of r t
  end.

Lemma alive_of_In : forall l t v, alive_of l t = Some v -> In (t, v) l.
Proof.
  induction l as [| [t' v'] r IH]; simpl; intros t v H; [ discriminate | ].
  destruct (Nat.eqb t t') eqn:E.
  - apply Nat.eqb_eq in E; subst t'. inversion H; subst. left; reflexivity.
  - right; apply IH; exact H.
Qed.

(* S2 — 소유자 접근이 **겹치는** 차용을 무효화한다. *)
Definition kill_at (l : live) (p : place) (only_mut : bool) : live :=
  filter (fun e => let '(_, (q, m)) := e in
                   negb (ov p q && (if only_mut then m else true))) l.

(* S1 — 새 차용이 **겹치는** 살아 있는 차용과 부딪히고 어느 한쪽이 exc → 거부. *)
Definition has_conflict (l : live) (p : place) (new_mut : bool) : bool :=
  existsb (fun e => let '(_, (q, m)) := e in ov p q && (m || new_mut)) l.

Definition sstep (s : live) (e : ev) : option live :=
  match e with
  | Create t p m => if has_conflict s p m then None else Some ((t, (p, m)) :: s)
  | Use t wr =>
      match alive_of s t with
      | None => None
      | Some (_, m) => if wr && negb m then None else Some s
      end
  | Own p wr => Some (kill_at s p (negb wr))
  end.

Fixpoint srun (s : live) (evs : list ev) : option live :=
  match evs with
  | [] => Some s
  | e :: r => match sstep s e with None => None | Some s' => srun s' r end
  end.

Definition static_green (evs : list ev) : bool :=
  match srun [] evs with Some _ => true | None => false end.

(* ── 5. 불변식 ────────────────────────────────────────────────────────────── *)

Definition INV (evs : list ev) (l : live) (s : state) : Prop :=
  forall t p m, In (t, (p, m)) l -> tag_info evs t = Some (p, m) /\ In t (s p).

(* SINV — **겹치는** place 의 서로 다른 살아 있는 차용은 모두 shr.
   평평한 모델에서는 "같은 지역"이었다. 겹침으로 올려도 S1 이 그대로 보존한다. *)
Definition SINV (l : live) : Prop :=
  forall t1 p1 m1 t2 p2 m2,
    In (t1, (p1, m1)) l -> In (t2, (p2, m2)) l -> t1 <> t2 -> ov p1 p2 = true ->
    m1 = false /\ m2 = false.

(* ── 6. 보조정리 ─────────────────────────────────────────────────────────── *)

Lemma in_pop_above : forall st t, In t st -> In t (pop_above st t).
Proof.
  induction st as [| u r IH]; simpl; intros t H; [ contradiction | ].
  destruct (Nat.eqb u t) eqn:E.
  - apply Nat.eqb_eq in E; subst u. left; reflexivity.
  - destruct H as [H | H]; [ subst u; rewrite Nat.eqb_refl in E; discriminate | apply IH; exact H ].
Qed.

Lemma existsb_In : forall t st, existsb (Nat.eqb t) st = true -> In t st.
Proof.
  induction st as [| u r IH]; simpl; intros H; [ discriminate | ].
  apply orb_true_iff in H as [H | H].
  - apply Nat.eqb_eq in H; subst u; left; reflexivity.
  - right; apply IH; exact H.
Qed.

Lemma In_existsb : forall t st, In t st -> existsb (Nat.eqb t) st = true.
Proof.
  induction st as [| u r IH]; simpl; intros H; [ contradiction | ].
  destruct H as [H | H].
  - subst u; rewrite Nat.eqb_refl; reflexivity.
  - apply orb_true_iff; right; apply IH; exact H.
Qed.

Lemma in_drop_muts_above : forall evs st t t0,
  is_mut evs t0 = false -> In t0 st -> In t0 (drop_muts_above evs st t).
Proof.
  induction st as [| u r IH]; simpl; intros t t0 Hshr Hin; [ contradiction | ].
  destruct (Nat.eqb u t) eqn:Eut; [ exact Hin | ].
  destruct (is_mut evs u) eqn:Emu.
  - destruct Hin as [Hin | Hin]; [ subst u; congruence | apply IH; assumption ].
  - destruct Hin as [Hin | Hin]; [ subst u; left; reflexivity | right; apply IH; assumption ].
Qed.

Lemma in_drop_muts_above_self : forall evs st t,
  In t st -> In t (drop_muts_above evs st t).
Proof.
  induction st as [| u r IH]; simpl; intros t Hin; [ contradiction | ].
  destruct (Nat.eqb u t) eqn:Eut; [ exact Hin | ].
  assert (Hne : u <> t) by (intro; subst; rewrite Nat.eqb_refl in Eut; discriminate).
  destruct Hin as [Hin | Hin]; [ congruence | ].
  destruct (is_mut evs u); [ apply IH; exact Hin | right; apply IH; exact Hin ].
Qed.

Lemma in_drop_muts : forall evs st t0,
  is_mut evs t0 = false -> In t0 st -> In t0 (drop_muts evs st).
Proof.
  intros evs st t0 Hshr Hin. unfold drop_muts. apply filter_In. split; [ exact Hin | ].
  rewrite Hshr; reflexivity.
Qed.

Lemma In_kill_at : forall l p om e, In e (kill_at l p om) -> In e l.
Proof. intros l p om e H; unfold kill_at in H; apply filter_In in H as [H _]; exact H. Qed.

Lemma kill_at_pred : forall l p om t q m,
  In (t, (q, m)) (kill_at l p om) ->
  ov p q && (if om then m else true) = false.
Proof.
  intros l p om t q m H. unfold kill_at in H. apply filter_In in H as [_ H].
  apply negb_true_iff in H. exact H.
Qed.

(* SINV 는 sstep 을 보존한다 — Create 의 has_conflict(겹침 판정)가 그 일을 한다. *)
Lemma sinv_step : forall l e l', SINV l -> sstep l e = Some l' -> SINV l'.
Proof.
  intros l e l' Hs Hstep. destruct e as [t p m | t wr | p wr]; simpl in Hstep.
  - destruct (has_conflict l p m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    assert (Hnc : forall t' q m', In (t', (q, m')) l -> ov p q && (m' || m) = false).
    { intros t' q m' Hin. unfold has_conflict in Ec.
      destruct (ov p q && (m' || m)) eqn:E; [ | reflexivity ].
      assert (existsb (fun e => let '(_, (q0, m0)) := e in ov p q0 && (m0 || m)) l = true).
      { apply existsb_exists. exists (t', (q, m')). split; [ exact Hin | exact E ]. }
      congruence. }
    intros t1 p1 mm1 t2 p2 mm2 H1 H2 Hne Hov. simpl in H1, H2.
    destruct H1 as [H1 | H1]; destruct H2 as [H2 | H2].
    + inversion H1; inversion H2; subst; contradiction.
    + inversion H1; subst t1 p1 mm1.
      pose proof (Hnc t2 p2 mm2 H2) as He. rewrite Hov in He. simpl in He.
      apply orb_false_iff in He as [Hm2 Hm]. split; [ exact Hm | exact Hm2 ].
    + assert (Ht2 : t2 = t) by congruence.
      assert (Hp2 : p2 = p) by congruence.
      assert (Hm2 : mm2 = m) by congruence.
      subst t2 p2 mm2.
      pose proof (Hnc t1 p1 mm1 H1) as He.
      assert (Hov' : ov p p1 = true) by (rewrite ov_sym; exact Hov).
      rewrite Hov' in He. simpl in He.
      apply orb_false_iff in He as [Hm1 Hm]. split; [ exact Hm1 | exact Hm ].
    + eapply Hs; eauto.
  - destruct (alive_of l t) as [[q m] | ]; [ | discriminate ].
    destruct (wr && negb m); [ discriminate | ].
    inversion Hstep; subst l'; exact Hs.
  - inversion Hstep; subst l'.
    intros t1 p1 m1 t2 p2 m2 H1 H2 Hne Hov.
    eapply Hs; [ eapply In_kill_at; exact H1 | eapply In_kill_at; exact H2 | exact Hne | exact Hov ].
Qed.

(* ── 7. 정리 A(place 판) — 한 걸음 ────────────────────────────────────────── *)

Lemma agreement_step : forall evs l s e l',
  wf evs -> In e evs ->
  INV evs l s -> SINV l ->
  sstep l e = Some l' ->
  exists s', step evs s e = Some s' /\ INV evs l' s'.
Proof.
  intros evs l s e l' Hwf Hine Hinv Hsinv Hstep.
  destruct e as [t p m | t wr | p wr]; simpl in Hstep |- *.

  - (* Create — D1 *)
    destruct (has_conflict l p m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 p0 m0 Hin. simpl in Hin.
    unfold st_create.
    destruct Hin as [Hin | Hin].
    + inversion Hin; subst t0 p0 m0.
      split; [ apply Hwf; exact Hine | ].
      rewrite peqb_refl. left; reflexivity.
    + pose proof (Hinv t0 p0 m0 Hin) as [Hti Hex].
      split; [ exact Hti | ].
      destruct (peqb p0 p) eqn:Ep.
      * apply peqb_eq in Ep; subst p0. right; exact Hex.
      * exact Hex.

  - (* Use — D2 (읽기) / D3 (쓰기) *)
    destruct (alive_of l t) as [[q m] | ] eqn:Ea; [ | discriminate ].
    destruct (wr && negb m) eqn:Ew; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    apply alive_of_In in Ea.
    pose proof (Hinv t q m Ea) as [Hti Hex].
    rewrite Hti.
    rewrite (In_existsb t (s q) Hex).
    eexists; split; [ reflexivity | ].
    intros t0 p0 m0 Hin0.
    pose proof (Hinv t0 p0 m0 Hin0) as [Hti0 Hex0].
    split; [ exact Hti0 | ].
    destruct wr.
    + (* 쓰기 — D3 *)
      unfold st_use_wr.
      destruct (peqb p0 q) eqn:Ep.
      * apply peqb_eq in Ep; subst p0.
        destruct (Nat.eq_dec t0 t) as [Ht0 | Ht0].
        -- subst t0. apply in_pop_above; exact Hex.
        -- (* 다른 태그가 같은 place 에 살아 있다 → SINV 로 둘 다 shr → wr 불가 *)
           pose proof (Hsinv t q m t0 q m0 Ea Hin0 (fun H => Ht0 (eq_sym H)) (ov_refl q))
             as [Hm _]. subst m. simpl in Ew. discriminate.
      * destruct (ov q p0) eqn:Eov.
        -- (* 겹치는 다른 place — SINV 가 둘 다 shr 이라 하므로 wr 불가 *)
           assert (Ht0 : t0 <> t).
           { intro; subst t0. rewrite Hti in Hti0. inversion Hti0; subst p0.
             rewrite peqb_refl in Ep; discriminate. }
           pose proof (Hsinv t q m t0 p0 m0 Ea Hin0 (fun H => Ht0 (eq_sym H)) Eov) as [Hm _].
           subst m. simpl in Ew. discriminate.
        -- exact Hex0.
    + (* 읽기 — D2 *)
      unfold st_use_rd.
      destruct (peqb p0 q) eqn:Ep.
      * apply peqb_eq in Ep; subst p0.
        destruct (Nat.eq_dec t0 t) as [Ht0 | Ht0].
        -- subst t0. apply in_drop_muts_above_self; exact Hex.
        -- pose proof (Hsinv t q m t0 q m0 Ea Hin0 (fun H => Ht0 (eq_sym H)) (ov_refl q))
             as [_ Hm0]. subst m0.
           apply in_drop_muts_above; [ unfold is_mut; rewrite Hti0; reflexivity | exact Hex0 ].
      * destruct (ov q p0) eqn:Eov.
        -- assert (Ht0 : t0 <> t).
           { intro; subst t0. rewrite Hti in Hti0. inversion Hti0; subst p0.
             rewrite peqb_refl in Ep; discriminate. }
           pose proof (Hsinv t q m t0 p0 m0 Ea Hin0 (fun H => Ht0 (eq_sym H)) Eov) as [_ Hm0].
           subst m0.
           apply in_drop_muts; [ unfold is_mut; rewrite Hti0; reflexivity | exact Hex0 ].
        -- exact Hex0.

  - (* Own — D4 (읽기) / D5 (쓰기) *)
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 p0 m0 Hin0.
    pose proof (kill_at_pred l p (negb wr) t0 p0 m0 Hin0) as Hk.
    apply In_kill_at in Hin0.
    pose proof (Hinv t0 p0 m0 Hin0) as [Hti0 Hex0].
    split; [ exact Hti0 | ].
    unfold st_own.
    destruct (ov p p0) eqn:Eov.
    + simpl in Hk.
      destruct wr; simpl in Hk.
      * (* 소유자 쓰기: 겹치는 차용은 전부 죽는다 → 살아남은 것이 없다 *)
        discriminate.
      * destruct m0; [ discriminate | ].
        apply in_drop_muts; [ unfold is_mut; rewrite Hti0; reflexivity | exact Hex0 ].
    + exact Hex0.
Qed.

(* ── 8. 정리 A(place 판) — 전체 실행 ──────────────────────────────────────── *)

Lemma agreement_run : forall evs rest l s,
  wf evs ->
  (forall e, In e rest -> In e evs) ->
  INV evs l s -> SINV l ->
  (exists l', srun l rest = Some l') ->
  exists s', run evs s rest = Some s'.
Proof.
  intros evs rest. induction rest as [| e r IH]; intros l s Hwf Hsub Hinv Hsinv [l' Hsrun].
  - eexists; reflexivity.
  - simpl in Hsrun |- *.
    destruct (sstep l e) as [l1 | ] eqn:Es; [ | discriminate ].
    assert (Hine : In e evs) by (apply Hsub; left; reflexivity).
    pose proof (agreement_step evs l s e l1 Hwf Hine Hinv Hsinv Es) as [s1 [Hstep Hinv1]].
    rewrite Hstep.
    apply (IH l1 s1); auto.
    + intros e0 He0; apply Hsub; right; exact He0.
    + eapply sinv_step; eauto.
    + exists l'; exact Hsrun.
Qed.

(* ★★ 정리 A — **place 판**. 사영 경로와 겹침 위에서도 합치가 성립한다. ★★ *)
Theorem agreement : forall evs,
  wf evs -> static_green evs = true -> dyn_clean evs = true.
Proof.
  intros evs Hwf Hg.
  unfold static_green in Hg. destruct (srun [] evs) as [l' | ] eqn:Es; [ | discriminate ].
  unfold dyn_clean.
  assert (Hinv : INV evs [] empty) by (intros t p m H; simpl in H; contradiction).
  assert (Hsinv : SINV []) by (intros t1 p1 m1 t2 p2 m2 H; simpl in H; contradiction).
  pose proof (agreement_run evs evs [] empty Hwf (fun e H => H) Hinv Hsinv (ex_intro _ l' Es))
    as [s' Hrun].
  rewrite Hrun. reflexivity.
Qed.

Corollary no_violation_in_green : forall evs,
  wf evs -> dyn_clean evs = false -> static_green evs = false.
Proof.
  intros evs Hwf Hd.
  destruct (static_green evs) eqn:Eg; [ | reflexivity ].
  rewrite (agreement evs Hwf Eg) in Hd. discriminate.
Qed.

(* ── 9. 정밀도 — 이것이 place 로 올린 **이유**다 ──────────────────────────── *)
(* 평평한 모델에서는 x.a 와 x.b 의 두 mut 차용이 "같은 지역 x" 라서 거부됐다.
   place 모델에서는 겹치지 않으므로 **둘 다 살아 있을 수 있다.** 그리고 정리 A 가
   그것을 여전히 덮는다 — 정밀도를 얻으면서 건전성을 잃지 않았다. *)

Definition two_fields : list ev :=
  [ Create 1 [0;1] true;      (* &mut x.a *)
    Create 2 [0;2] true;      (* &mut x.b — 형제 필드 *)
    Use 1 true;
    Use 2 true ].

Example two_mut_borrows_of_sibling_fields_are_green : static_green two_fields = true.
Proof. reflexivity. Qed.

Example two_fields_is_dyn_clean : dyn_clean two_fields = true.
Proof. reflexivity. Qed.

(* 그리고 **겹치면** 여전히 거부된다 — 정밀도가 건전성을 먹지 않았다. *)
Definition whole_and_field : list ev :=
  [ Create 1 [0] true;        (* &mut x *)
    Create 2 [0;1] true;      (* &mut x.a — x 안이다. 겹친다. *)
    Use 1 true ].

Example overlapping_mut_borrows_are_rejected : static_green whole_and_field = false.
Proof. reflexivity. Qed.

(* 소유자 쓰기는 **겹치는** 차용만 죽인다 — 형제 필드의 차용은 산다. *)
Definition own_one_field : list ev :=
  [ Create 1 [0;1] true;      (* &mut x.a *)
    Own [0;2] true;           (* x.b = … — x.a 와 겹치지 않는다 *)
    Use 1 true ].             (* 그래서 이 사용은 여전히 유효하다 *)

Example writing_a_sibling_field_does_not_kill_the_borrow : static_green own_one_field = true.
Proof. reflexivity. Qed.

Example own_one_field_dyn_clean : dyn_clean own_one_field = true.
Proof. reflexivity. Qed.

(* 반면 **전체**에 쓰면 안쪽 필드의 차용이 죽는다. *)
Definition own_whole : list ev :=
  [ Create 1 [0;1] true;      (* &mut x.a *)
    Own [0] true;             (* x = … — x.a 를 포함한다 *)
    Use 1 true ].             (* ⚡ *)

Example writing_the_whole_kills_the_field_borrow : static_green own_whole = false.
Proof. reflexivity. Qed.

Example own_whole_is_dyn_dirty : dyn_clean own_whole = false.
Proof. reflexivity. Qed.

(* ── 10. 평평한 모델은 특수 경우다 ────────────────────────────────────────── *)
(* place 길이가 1 이면 겹침 = 동등. 그래서 LowentEXCL.v 의 모델은
   이 모델의 **부분**이고, 정리 A(place 판)가 그것을 덮는다. *)

Lemma ov_singleton : forall x y, ov [x] [y] = Nat.eqb x y.
Proof.
  intros x y; unfold ov, pre; simpl.
  destruct (Nat.eqb x y) eqn:E; simpl.
  - reflexivity.
  - rewrite Nat.eqb_sym in E. rewrite E. reflexivity.
Qed.

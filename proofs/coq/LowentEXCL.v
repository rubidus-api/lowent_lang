(* LowentEXCL.v — V3 기계화: 합치 정리(정리 A)의 **무계** 기계 증명.
 *
 * 종이 증명: docs/proofs/lambda-lowent-core-agreement.md §4
 * 구현     : impl/src/low_region.c (정적 EXCL) · impl/src/low_ir.c (동적 borrow-stack)
 * 유계 검증: impl/tests/run_tests.c (V3-lite — 920,918 시퀀스 전수)
 *
 * 증명하는 것:
 *     static_green evs = true  →  dyn_clean evs = true
 *   "정적 검사를 통과한 프로그램은 실행 중 차용 위반(⚡)을 일으키지 않는다."
 *
 * V3-lite 는 이것을 길이 ≤6·태그 ≤4 로 전수 확인했다. 이 파일은 **그 유계를 없앤다** —
 * 임의 길이·임의 태그 수의 모든 이벤트 열에 대해.
 *
 * 범위: 직선 단편(루프 없음). 루프는 종이 §5(보조정리 B) — 후속 기계화 대상.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 이벤트 (종이 §1) ──────────────────────────────────────────────────── *)

Inductive ev : Type :=
  | Create (tag : nat) (x : nat) (mut : bool)   (* 차용 생성 *)
  | Use    (tag : nat) (wr : bool)              (* 차용을 통한 접근 *)
  | Own    (x : nat)   (wr : bool).             (* 소유자 직접 접근 *)

(* 태그 → (대상 지역, 배타 여부) *)
Fixpoint tag_info (evs : list ev) (t : nat) : option (nat * bool) :=
  match evs with
  | [] => None
  | Create t' x m :: r => if Nat.eqb t t' then Some (x, m) else tag_info r t
  | _ :: r => tag_info r t
  end.

(* 적형(well-formed): 각 Create 의 태그가 tag_info 로 자기 자신을 찾는다.
   태그가 서로 다르면 성립한다(구현은 신선한 태그를 발행한다). *)
Definition wf (evs : list ev) : Prop :=
  forall t x m, In (Create t x m) evs -> tag_info evs t = Some (x, m).

Definition is_mut (evs : list ev) (t : nat) : bool :=
  match tag_info evs t with Some (_, m) => m | None => false end.

(* ── 2. 동적 의미 — borrow stack (종이 §3 = low_ir.c 의 bstk 함수들) ───────────── *)

Definition state := list (nat * list nat).      (* 지역 → 태그 스택(head = top) *)

Fixpoint get_stk (s : state) (x : nat) : list nat :=
  match s with
  | [] => []
  | (y, st) :: r => if Nat.eqb x y then st else get_stk r x
  end.

Fixpoint set_stk (s : state) (x : nat) (st : list nat) : state :=
  match s with
  | [] => [(x, st)]
  | (y, st') :: r => if Nat.eqb x y then (x, st) :: r else (y, st') :: set_stk r x st
  end.

Lemma get_set_same : forall s x st, get_stk (set_stk s x st) x = st.
Proof.
  induction s as [| [y st'] r IH]; intros x st; simpl.
  - rewrite Nat.eqb_refl; reflexivity.
  - destruct (Nat.eqb x y) eqn:E; simpl.
    + rewrite Nat.eqb_refl; reflexivity.
    + rewrite E; apply IH.
Qed.

Lemma get_set_other : forall s x y st,
  Nat.eqb x y = false -> get_stk (set_stk s x st) y = get_stk s y.
Proof.
  induction s as [| [z st'] r IH]; intros x y st H; simpl.
  - rewrite Nat.eqb_sym in H; rewrite H; reflexivity.
  - destruct (Nat.eqb x z) eqn:Exz; simpl.
    + apply Nat.eqb_eq in Exz; subst z.
      rewrite Nat.eqb_sym in H; rewrite H; reflexivity.
    + destruct (Nat.eqb y z) eqn:Eyz; [ reflexivity | apply IH; exact H ].
Qed.

(* D3 (차용을 통한 쓰기): t 위의 항목을 **전부** 버린다 — t 가 top 이 된다. *)
Fixpoint pop_above (st : list nat) (t : nat) : list nat :=
  match st with
  | [] => []
  | u :: r => if Nat.eqb u t then u :: r else pop_above r t
  end.

(* D2 (차용을 통한 읽기): t 위의 **exc 항목만** 버린다. shr 은 남는다.
   (impl 의 bstk 읽기 경로와 1:1 — 아래를 보존하고, 위에서는 mut 만 걸러낸다.) *)
Fixpoint drop_muts_above (evs : list ev) (st : list nat) (t : nat) : list nat :=
  match st with
  | [] => []
  | u :: r => if Nat.eqb u t then u :: r
              else if is_mut evs u then drop_muts_above evs r t
                   else u :: drop_muts_above evs r t
  end.

(* D4 (소유자 읽기): exc 항목을 전부 무효화한다(오류가 아니다). *)
Definition drop_muts (evs : list ev) (st : list nat) : list nat :=
  filter (fun u => negb (is_mut evs u)) st.

(* 소단계. None = ⚡ (E-VM-EXCL). *)
Definition step (evs : list ev) (s : state) (e : ev) : option state :=
  match e with
  | Create t x _ => Some (set_stk s x (t :: get_stk s x))                     (* D1 *)
  | Use t wr =>
      match tag_info evs t with
      | None => None
      | Some (x, _) =>
          let st := get_stk s x in
          if existsb (Nat.eqb t) st
          then Some (set_stk s x (if wr then pop_above st t                   (* D3 *)
                                        else drop_muts_above evs st t))       (* D2 *)
          else None                                                (* ⚡ 무효화된 차용 사용 *)
      end
  | Own x wr => Some (set_stk s x (if wr then [] else drop_muts evs (get_stk s x)))  (* D5 / D4 *)
  end.

Fixpoint run (evs : list ev) (s : state) (rest : list ev) : option state :=
  match rest with
  | [] => Some s
  | e :: r => match step evs s e with None => None | Some s' => run evs s' r end
  end.

Definition dyn_clean (evs : list ev) : bool :=
  match run evs [] evs with Some _ => true | None => false end.

(* ── 3. 정적 판정 (종이 §2 = low_region.c 의 excl2_conflicts) ─────────────── *)
(* 직선 단편에서 구간 판정은 아래의 전방 판정과 같은 것을 본다:
     S1  새 차용이 같은 지역의 살아 있는 차용과 겹치고 어느 한쪽이 exc → 거부
     S2  소유자 접근이 차용을 무효화한다(쓰기=전부, 읽기=exc) → 이후 사용 시 거부
   (구현과의 1:1 대응은 유계 전수 모델 체크가 확인한다.) *)

Definition live := list (nat * (nat * bool)).   (* 태그 → (지역, mut) *)

Fixpoint alive_of (l : live) (t : nat) : option (nat * bool) :=
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

Definition kill_at (l : live) (x : nat) (only_mut : bool) : live :=
  filter (fun p => let '(_, (y, m)) := p in
                   negb (Nat.eqb x y && (if only_mut then m else true))) l.

Definition has_conflict (l : live) (x : nat) (new_mut : bool) : bool :=
  existsb (fun p => let '(_, (y, m)) := p in Nat.eqb x y && (m || new_mut)) l.

Definition sstep (s : live) (e : ev) : option live :=
  match e with
  | Create t x m => if has_conflict s x m then None                 (* S1 *)
                    else Some ((t, (x, m)) :: s)
  | Use t wr =>
      match alive_of s t with
      | None => None                                                (* 죽은 차용 사용 *)
      | Some (_, m) => if wr && negb m then None                    (* shr 로 쓰기(E-TYPE-REF) *)
                       else Some s
      end
  | Own x wr => Some (kill_at s x (negb wr))                        (* S2 *)
  end.

Fixpoint srun (s : live) (evs : list ev) : option live :=
  match evs with
  | [] => Some s
  | e :: r => match sstep s e with None => None | Some s' => srun s' r end
  end.

Definition static_green (evs : list ev) : bool :=
  match srun [] evs with Some _ => true | None => false end.

(* ── 4. 불변식 ────────────────────────────────────────────────────────────── *)

(* INV: 정적으로 살아 있는 차용은 동적 스택에도 있고, 정보가 일치한다. *)
Definition INV (evs : list ev) (l : live) (s : state) : Prop :=
  forall t x m, In (t, (x, m)) l ->
    tag_info evs t = Some (x, m) /\ In t (get_stk s x).

(* SINV: **같은 지역의 서로 다른 살아 있는 차용은 모두 shr 이다.**
   S1(Create 의 has_conflict)이 정확히 이것을 보존한다.
   이 사실이 D3(쓰기)에서 pop_above 가 다른 살아 있는 차용을 지우지 않음을 보장한다. *)
Definition SINV (l : live) : Prop :=
  forall t1 x m1 t2 m2,
    In (t1, (x, m1)) l -> In (t2, (x, m2)) l -> t1 <> t2 -> m1 = false /\ m2 = false.

(* ── 5. 보조정리 ─────────────────────────────────────────────────────────── *)

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

(* D2 는 shr 항목을 보존한다 — 위에 있든 아래에 있든. *)
Lemma in_drop_muts_above : forall evs st t t0,
  is_mut evs t0 = false -> In t0 st -> In t0 (drop_muts_above evs st t).
Proof.
  induction st as [| u r IH]; simpl; intros t t0 Hshr Hin; [ contradiction | ].
  destruct (Nat.eqb u t) eqn:Eut; [ exact Hin | ].
  destruct (is_mut evs u) eqn:Emu.
  - destruct Hin as [Hin | Hin]; [ subst u; congruence | apply IH; assumption ].
  - destruct Hin as [Hin | Hin]; [ subst u; left; reflexivity | right; apply IH; assumption ].
Qed.

(* D2 는 **t 자신**을 보존한다 — mut 이든 아니든. drop_muts_above 가 t 에서 멈추기 때문. *)
Lemma in_drop_muts_above_self : forall evs st t,
  In t st -> In t (drop_muts_above evs st t).
Proof.
  induction st as [| u r IH]; simpl; intros t Hin; [ contradiction | ].
  destruct (Nat.eqb u t) eqn:Eut; [ exact Hin | ].
  assert (Hne : u <> t) by (intro; subst; rewrite Nat.eqb_refl in Eut; discriminate).
  destruct Hin as [Hin | Hin]; [ congruence | ].
  destruct (is_mut evs u); [ apply IH; exact Hin | right; apply IH; exact Hin ].
Qed.

(* D4 도 shr 항목을 보존한다. *)
Lemma in_drop_muts : forall evs st t0,
  is_mut evs t0 = false -> In t0 st -> In t0 (drop_muts evs st).
Proof.
  intros evs st t0 Hshr Hin. unfold drop_muts. apply filter_In. split; [ exact Hin | ].
  rewrite Hshr; reflexivity.
Qed.

Lemma In_kill_at : forall l x om p, In p (kill_at l x om) -> In p l.
Proof. intros l x om p H; unfold kill_at in H; apply filter_In in H as [H _]; exact H. Qed.

Lemma kill_at_pred : forall l x om t y m,
  In (t, (y, m)) (kill_at l x om) ->
  Nat.eqb x y && (if om then m else true) = false.
Proof.
  intros l x om t y m H. unfold kill_at in H. apply filter_In in H as [_ H].
  apply negb_true_iff in H. exact H.
Qed.

(* SINV 는 sstep 을 보존한다 — Create 의 has_conflict 가 그 일을 한다. *)
Lemma sinv_step : forall l e l', SINV l -> sstep l e = Some l' -> SINV l'.
Proof.
  intros l e l' Hs Hstep. destruct e as [t x m | t wr | x wr]; simpl in Hstep.
  - destruct (has_conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    (* has_conflict = false → 같은 지역의 기존 차용은 (m' || m) 가 거짓 *)
    assert (Hnc : forall t' y m', In (t', (y, m')) l -> Nat.eqb x y && (m' || m) = false).
    { intros t' y m' Hin. unfold has_conflict in Ec.
      destruct (Nat.eqb x y && (m' || m)) eqn:E; [ | reflexivity ].
      assert (existsb (fun p => let '(_, (y0, m0)) := p in Nat.eqb x y0 && (m0 || m)) l = true).
      { apply existsb_exists. exists (t', (y, m')). split; [ exact Hin | exact E ]. }
      congruence. }
    intros t1 y1 mm1 t2 mm2 H1 H2 Hne. simpl in H1, H2.
    destruct H1 as [H1 | H1]; destruct H2 as [H2 | H2].
    + inversion H1; inversion H2; subst; contradiction.
    + inversion H1; subst t1 y1 mm1.
      pose proof (Hnc t2 x mm2 H2) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm2 Hm]. split; [ exact Hm | exact Hm2 ].
    + assert (Hx : y1 = x) by congruence.
      assert (Ht2 : t2 = t) by congruence.
      assert (Hm2 : mm2 = m) by congruence.
      subst y1 t2 mm2.
      pose proof (Hnc t1 x mm1 H1) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm1 Hm]. split; [ exact Hm1 | exact Hm ].
    + eapply Hs; eauto.
  - destruct (alive_of l t) as [[y m] | ]; [ | discriminate ].
    destruct (wr && negb m); [ discriminate | ].
    inversion Hstep; subst l'; exact Hs.
  - inversion Hstep; subst l'.
    intros t1 y1 m1 t2 m2 H1 H2 Hne.
    eapply Hs; [ eapply In_kill_at; exact H1 | eapply In_kill_at; exact H2 | exact Hne ].
Qed.

(* ── 6. 정리 A — 한 걸음 ─────────────────────────────────────────────────── *)

Lemma agreement_step : forall evs l s e l',
  wf evs -> In e evs ->
  INV evs l s -> SINV l ->
  sstep l e = Some l' ->
  exists s', step evs s e = Some s' /\ INV evs l' s'.
Proof.
  intros evs l s e l' Hwf Hine Hinv Hsinv Hstep.
  destruct e as [t x m | t wr | x wr]; simpl in Hstep |- *.

  - (* Create — D1 *)
    destruct (has_conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin. simpl in Hin.
    destruct Hin as [Hin | Hin].
    + inversion Hin; subst t0 x0 m0.
      split; [ apply Hwf; exact Hine | ].
      rewrite get_set_same. left; reflexivity.
    + pose proof (Hinv t0 x0 m0 Hin) as [Hti Hex].
      split; [ exact Hti | ].
      destruct (Nat.eqb x x0) eqn:Exx.
      * apply Nat.eqb_eq in Exx; subst x0. rewrite get_set_same. right; exact Hex.
      * rewrite get_set_other by exact Exx. exact Hex.

  - (* Use — D2 (읽기) / D3 (쓰기) *)
    destruct (alive_of l t) as [[y m] | ] eqn:Ea; [ | discriminate ].
    destruct (wr && negb m) eqn:Ew; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    apply alive_of_In in Ea.
    pose proof (Hinv t y m Ea) as [Hti Hex].
    rewrite Hti.
    rewrite (In_existsb t (get_stk s y) Hex).
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    pose proof (Hinv t0 x0 m0 Hin0) as [Hti0 Hex0].
    split; [ exact Hti0 | ].
    destruct (Nat.eqb y x0) eqn:Eyx.
    + apply Nat.eqb_eq in Eyx; subst x0. rewrite get_set_same.
      destruct (Nat.eq_dec t0 t) as [Ht0 | Ht0].
      * (* 같은 태그 — 두 경우 모두 t 는 남는다 (D3 은 t 를 top 으로, D2 는 t 에서 멈춘다) *)
        subst t0. destruct wr.
        -- apply in_pop_above; exact Hex.
        -- apply in_drop_muts_above_self; exact Hex.
      * (* 다른 태그 — SINV 가 둘 다 shr 임을 보장한다 *)
        pose proof (Hsinv t y m t0 m0 Ea Hin0 (fun H => Ht0 (eq_sym H))) as [Hm Hm0].
        subst m m0.
        destruct wr; [ simpl in Ew; discriminate | ].
        apply in_drop_muts_above; [ unfold is_mut; rewrite Hti0; reflexivity | exact Hex0 ].
    + rewrite get_set_other by exact Eyx. exact Hex0.

  - (* Own — D4 (읽기) / D5 (쓰기) *)
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    pose proof (kill_at_pred l x (negb wr) t0 x0 m0 Hin0) as Hk.
    apply In_kill_at in Hin0.
    pose proof (Hinv t0 x0 m0 Hin0) as [Hti0 Hex0].
    split; [ exact Hti0 | ].
    destruct (Nat.eqb x x0) eqn:Exx.
    + simpl in Hk.
      apply Nat.eqb_eq in Exx. subst x0.
      rewrite get_set_same.
      destruct wr; simpl in Hk.
      * (* 소유자 쓰기: 그 지역의 차용은 전부 죽는다 → 살아남은 것이 없다 *)
        discriminate.
      * (* 소유자 읽기: exc 만 죽는다 → 살아남은 것은 shr *)
        destruct m0; [ discriminate | ].
        apply in_drop_muts; [ unfold is_mut; rewrite Hti0; reflexivity | exact Hex0 ].
    + rewrite get_set_other by exact Exx. exact Hex0.
Qed.

(* ── 7. 정리 A — 전체 실행 ───────────────────────────────────────────────── *)

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

(* ★★ 정리 A (무계) — 정적 검사를 통과하면 실행 중 차용 위반이 없다. ★★ *)
Theorem agreement : forall evs,
  wf evs -> static_green evs = true -> dyn_clean evs = true.
Proof.
  intros evs Hwf Hg.
  unfold static_green in Hg. destruct (srun [] evs) as [l' | ] eqn:Es; [ | discriminate ].
  unfold dyn_clean.
  assert (Hinv : INV evs [] []) by (intros t x m H; simpl in H; contradiction).
  assert (Hsinv : SINV []) by (intros t1 x m1 t2 m2 H; simpl in H; contradiction).
  pose proof (agreement_run evs evs [] [] Hwf (fun e H => H) Hinv Hsinv (ex_intro _ l' Es))
    as [s' Hrun].
  rewrite Hrun. reflexivity.
Qed.

(* 따름정리 — 대우: 실행 중 ⚡ 가 나면 정적 검사가 반드시 거부했다. *)
Corollary no_violation_in_green : forall evs,
  wf evs -> dyn_clean evs = false -> static_green evs = false.
Proof.
  intros evs Hwf Hd.
  destruct (static_green evs) eqn:Eg; [ | reflexivity ].
  rewrite (agreement evs Hwf Eg) in Hd. discriminate.
Qed.

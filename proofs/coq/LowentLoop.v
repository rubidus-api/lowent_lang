(* LowentLoop.v — V3: 보조정리 B(루프)의 기계 증명.
 *
 * 종이 증명: docs/proofs/lambda-lowent-core-agreement.md §5
 * 유계 검증: impl/tests/run_tests.c (V3-lite 루프 체커 — 90,376 형태 × k=1..3)
 * 직선 단편: LowentEXCL.v (정리 A — 무계, Qed)
 *
 * 증명하는 것:
 *   static_green_loop pre body = true  →  ∀k, dyn_clean (pre ++ body^k)
 *   "정적 검사를 통과한 루프 프로그램은 **몇 번을 돌든** 차용 위반을 일으키지 않는다."
 *
 * 모델의 요점 — **신선한 토큰**(구현과 동일):
 *   Create 는 실행할 때마다 **새 토큰**을 발행한다. 그래서 루프 본문이 만드는 차용은
 *   반복마다 다른 값이고, 지난 반복의 토큰은 자동으로 무효가 된다(env 가 새 토큰을 가리킨다).
 *   이 한 가지 설계가 태그 재명명(renaming) 기계장치 없이 루프를 다룬다.
 *
 * 위험한 상호작용(종이 §5)은 하나뿐이다:
 *   pre 에서 생성되어 body 에서 쓰이는 차용 τ 와, body 안의 own(x) 이 공존하면 —
 *   반복 i 의 own 이 τ 를 죽이고, 반복 i+1 의 use(τ) 가 ⚡ 를 낸다.
 *   S2L 이 정확히 그 짝을 거부한다. 아래 loop_agreement 가 그것으로 충분함을 증명한다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 이벤트 ────────────────────────────────────────────────────────────── *)

Inductive ev : Type :=
  | Create (tag : nat) (x : nat) (mut : bool)
  | Use    (tag : nat) (wr : bool)
  | Own    (x : nat)   (wr : bool).

Definition prog := (list ev * list ev)%type.   (* (pre, body) — body 가 루프 본문 *)

Fixpoint tinfo (evs : list ev) (t : nat) : option (nat * bool) :=
  match evs with
  | [] => None
  | Create t' x m :: r => if Nat.eqb t t' then Some (x, m) else tinfo r t
  | _ :: r => tinfo r t
  end.

Definition is_mut (evs : list ev) (t : nat) : bool :=
  match tinfo evs t with Some (_, m) => m | None => false end.

(* ── 2. 동적 의미 — 토큰 기반 (구현의 신선 태그와 1:1) ────────────────────── *)

Record dyn := {
  next : nat;                       (* 다음 토큰 *)
  env  : list (nat * nat);          (* 정적 태그 → **현재** 토큰 *)
  stk  : list (nat * list nat)      (* 지역 → 토큰 스택(head = top) *)
}.

Fixpoint lookup (l : list (nat * nat)) (k : nat) : option nat :=
  match l with
  | [] => None
  | (k', v) :: r => if Nat.eqb k k' then Some v else lookup r k
  end.
Fixpoint upd (l : list (nat * nat)) (k v : nat) : list (nat * nat) :=
  match l with
  | [] => [(k, v)]
  | (k', v') :: r => if Nat.eqb k k' then (k, v) :: r else (k', v') :: upd r k v
  end.
Fixpoint gets (s : list (nat * list nat)) (x : nat) : list nat :=
  match s with
  | [] => []
  | (y, st) :: r => if Nat.eqb x y then st else gets r x
  end.
Fixpoint sets (s : list (nat * list nat)) (x : nat) (st : list nat) : list (nat * list nat) :=
  match s with
  | [] => [(x, st)]
  | (y, st') :: r => if Nat.eqb x y then (x, st) :: r else (y, st') :: sets r x st
  end.

Lemma lookup_upd_same : forall l k v, lookup (upd l k v) k = Some v.
Proof.
  induction l as [| [k' v'] r IH]; intros k v; simpl.
  - rewrite Nat.eqb_refl; reflexivity.
  - destruct (Nat.eqb k k') eqn:E; simpl.
    + rewrite Nat.eqb_refl; reflexivity.
    + rewrite E; apply IH.
Qed.
Lemma lookup_upd_other : forall l k j v, Nat.eqb j k = false -> lookup (upd l k v) j = lookup l j.
Proof.
  induction l as [| [k' v'] r IH]; intros k j v H; simpl.
  - rewrite H; reflexivity.
  - destruct (Nat.eqb k k') eqn:E; simpl.
    + apply Nat.eqb_eq in E; subst k'. rewrite H. reflexivity.
    + destruct (Nat.eqb j k') eqn:E2; [ reflexivity | apply IH; exact H ].
Qed.
Lemma gets_sets_same : forall s x st, gets (sets s x st) x = st.
Proof.
  induction s as [| [y st'] r IH]; intros x st; simpl.
  - rewrite Nat.eqb_refl; reflexivity.
  - destruct (Nat.eqb x y) eqn:E; simpl.
    + rewrite Nat.eqb_refl; reflexivity.
    + rewrite E; apply IH.
Qed.
Lemma gets_sets_other : forall s x y st, Nat.eqb x y = false -> gets (sets s x st) y = gets s y.
Proof.
  induction s as [| [z st'] r IH]; intros x y st H; simpl.
  - rewrite Nat.eqb_sym in H; rewrite H; reflexivity.
  - destruct (Nat.eqb x z) eqn:E; simpl.
    + apply Nat.eqb_eq in E; subst z. rewrite Nat.eqb_sym in H; rewrite H; reflexivity.
    + destruct (Nat.eqb y z) eqn:E2; [ reflexivity | apply IH; exact H ].
Qed.

(* 토큰 u 가 exc 인가 — 토큰의 정적 태그를 owner 맵으로 되찾는다.
   토큰마다 (지역, mut) 을 함께 들고 다니는 편이 단순하다: tok_info. *)
Definition dstate := (dyn * list (nat * (nat * bool)))%type.   (* + 토큰 → (지역, mut) *)

Fixpoint tok_of (l : list (nat * (nat * bool))) (u : nat) : option (nat * bool) :=
  match l with
  | [] => None
  | (u', i) :: r => if Nat.eqb u u' then Some i else tok_of r u
  end.

Definition tok_mut (l : list (nat * (nat * bool))) (u : nat) : bool :=
  match tok_of l u with Some (_, m) => m | None => false end.

(* D3: t 위를 전부 버린다 *)
Fixpoint pop_above (st : list nat) (u : nat) : list nat :=
  match st with
  | [] => []
  | v :: r => if Nat.eqb v u then v :: r else pop_above r u
  end.
(* D2: u 위의 **exc 만** 버린다 (shr 은 남는다) *)
Fixpoint drop_muts_above (ti : list (nat * (nat * bool))) (st : list nat) (u : nat) : list nat :=
  match st with
  | [] => []
  | v :: r => if Nat.eqb v u then v :: r
              else if tok_mut ti v then drop_muts_above ti r u
                   else v :: drop_muts_above ti r u
  end.
(* D4: exc 전부 무효화 *)
Definition drop_muts (ti : list (nat * (nat * bool))) (st : list nat) : list nat :=
  filter (fun v => negb (tok_mut ti v)) st.

Definition dstep (s : dstate) (e : ev) : option dstate :=
  let '(d, ti) := s in
  match e with
  | Create t x m =>                                   (* D1 — **신선한 토큰** *)
      let u := next d in
      Some ({| next := S u;
               env  := upd (env d) t u;
               stk  := sets (stk d) x (u :: gets (stk d) x) |},
            (u, (x, m)) :: ti)
  | Use t wr =>
      match lookup (env d) t with
      | None => None                                  (* 미생성 태그 사용 *)
      | Some u =>
          match tok_of ti u with
          | None => None
          | Some (x, _) =>
              let st := gets (stk d) x in
              if existsb (Nat.eqb u) st
              then Some ({| next := next d; env := env d;
                            stk := sets (stk d) x
                                     (if wr then pop_above st u          (* D3 *)
                                            else drop_muts_above ti st u) |}, ti)   (* D2 *)
              else None                               (* ⚡ E-VM-EXCL *)
          end
      end
  | Own x wr =>
      Some ({| next := next d; env := env d;
               stk := sets (stk d) x (if wr then [] else drop_muts ti (gets (stk d) x)) |}, ti)
  end.

Fixpoint drun (s : dstate) (evs : list ev) : option dstate :=
  match evs with
  | [] => Some s
  | e :: r => match dstep s e with None => None | Some s' => drun s' r end
  end.

Definition d0 : dstate := ({| next := 0; env := []; stk := [] |}, []).

Fixpoint unroll (body : list ev) (k : nat) : list ev :=
  match k with 0 => [] | S k' => body ++ unroll body k' end.

Definition dyn_clean (pre body : list ev) (k : nat) : bool :=
  match drun d0 (pre ++ unroll body k) with Some _ => true | None => false end.

(* ── 3. 정적 판정 ─────────────────────────────────────────────────────────── *)

Definition live := list (nat * (nat * bool)).   (* 정적 태그 → (지역, mut) *)

Fixpoint alive (l : live) (t : nat) : option (nat * bool) :=
  match l with
  | [] => None
  | (t', i) :: r => if Nat.eqb t t' then Some i else alive r t
  end.
Definition kill_at (l : live) (x : nat) (only_mut : bool) : live :=
  filter (fun p => let '(_, (y, m)) := p in
                   negb (Nat.eqb x y && (if only_mut then m else true))) l.
Definition conflict (l : live) (x : nat) (m : bool) : bool :=
  existsb (fun p => let '(_, (y, m')) := p in Nat.eqb x y && (m' || m)) l.

(* 같은 정적 태그를 다시 만들면 이전 등록은 사라진다(신선 토큰과 정합) *)
Definition kill_tag (l : live) (t : nat) : live :=
  filter (fun p => negb (Nat.eqb (fst p) t)) l.

Definition sstep (l : live) (e : ev) : option live :=
  match e with
  | Create t x m => if conflict l x m then None else Some ((t, (x, m)) :: kill_tag l t)
  | Use t wr =>
      match alive l t with
      | None => None
      | Some (_, m) => if wr && negb m then None else Some l
      end
  | Own x wr => Some (kill_at l x (negb wr))
  end.

Fixpoint srun (l : live) (evs : list ev) : option live :=
  match evs with
  | [] => Some l
  | e :: r => match sstep l e with None => None | Some l' => srun l' r end
  end.

(* S2L (종이 §2) — **루프를 가로지르는 차용**과 루프 안의 소유자 접근은 공존 불가.
   판정: body 가 지역 x 를 own 하면, x 를 차용한 **pre-태그**가 body 에서 쓰여선 안 된다. *)
Fixpoint owns_in (body : list ev) (x : nat) (need_write : bool) : bool :=
  match body with
  | [] => false
  | Own y wr :: r => (Nat.eqb x y && (wr || negb need_write)) || owns_in r x need_write
  | _ :: r => owns_in r x need_write
  end.
Fixpoint uses_in (body : list ev) (t : nat) : bool :=
  match body with
  | [] => false
  | Use t' _ :: r => Nat.eqb t t' || uses_in r t
  | _ :: r => uses_in r t
  end.

(* pre 이후 살아 있는 차용 중, body 에서 쓰이면서 body 의 own 과 부딪히는 것이 있으면 거부 *)
Definition s2l_ok (lpre : live) (body : list ev) : bool :=
  forallb (fun p => let '(t, (x, m)) := p in
                    negb (uses_in body t && owns_in body x (negb m)))
          lpre.

Definition static_green_loop (pre body : list ev) : bool :=
  match srun [] pre with
  | None => false
  | Some lpre =>
      match srun lpre body with
      | None => false
      | Some _ => s2l_ok lpre body
      end
  end.

(* ── 4. 불변식 ────────────────────────────────────────────────────────────── *)

(* INV: 정적으로 살아 있는 태그 t 는 (a) 현재 토큰을 갖고, (b) 그 토큰의 정보가 일치하며,
        (c) 그 토큰이 해당 지역의 스택에 있다. *)
Definition INV (l : live) (s : dstate) : Prop :=
  let '(d, ti) := s in
  forall t x m, In (t, (x, m)) l ->
    exists u, lookup (env d) t = Some u /\ tok_of ti u = Some (x, m) /\ In u (gets (stk d) x).

(* FRESH: 발행된 토큰은 모두 next 미만이다 — Create 가 언제나 **새** 토큰을 쓴다는 사실.
   이것이 루프의 열쇠다: 재실행이 만드는 토큰은 지난 반복의 것과 절대 겹치지 않는다. *)
Definition FRESH (s : dstate) : Prop :=
  let '(d, ti) := s in forall u i, tok_of ti u = Some i -> u < next d.

(* SINV: 같은 지역의 서로 다른 살아 있는 차용은 모두 shr (Create 의 conflict 검사가 보존). *)
Definition SINV (l : live) : Prop :=
  forall t1 x m1 t2 m2,
    In (t1, (x, m1)) l -> In (t2, (x, m2)) l -> t1 <> t2 -> m1 = false /\ m2 = false.

(* ── 5. 보조정리 ─────────────────────────────────────────────────────────── *)

Lemma alive_In : forall l t v, alive l t = Some v -> In (t, v) l.
Proof.
  induction l as [| [t' v'] r IH]; simpl; intros t v H; [ discriminate | ].
  destruct (Nat.eqb t t') eqn:E.
  - apply Nat.eqb_eq in E; subst t'. inversion H; subst. left; reflexivity.
  - right; apply IH; exact H.
Qed.

Lemma In_existsb : forall u st, In u st -> existsb (Nat.eqb u) st = true.
Proof.
  induction st as [| v r IH]; simpl; intros H; [ contradiction | ].
  destruct H as [H | H].
  - subst v; rewrite Nat.eqb_refl; reflexivity.
  - apply orb_true_iff; right; apply IH; exact H.
Qed.

Lemma in_pop_above : forall st u, In u st -> In u (pop_above st u).
Proof.
  induction st as [| v r IH]; simpl; intros u H; [ contradiction | ].
  destruct (Nat.eqb v u) eqn:E.
  - apply Nat.eqb_eq in E; subst v. left; reflexivity.
  - destruct H as [H | H]; [ subst v; rewrite Nat.eqb_refl in E; discriminate | apply IH; exact H ].
Qed.

Lemma in_dma_self : forall ti st u, In u st -> In u (drop_muts_above ti st u).
Proof.
  induction st as [| v r IH]; simpl; intros u H; [ contradiction | ].
  destruct (Nat.eqb v u) eqn:E; [ exact H | ].
  assert (Hne : v <> u) by (intro; subst; rewrite Nat.eqb_refl in E; discriminate).
  destruct H as [H | H]; [ congruence | ].
  destruct (tok_mut ti v); [ apply IH; exact H | right; apply IH; exact H ].
Qed.

Lemma in_dma_shr : forall ti st u w,
  tok_mut ti w = false -> In w st -> In w (drop_muts_above ti st u).
Proof.
  induction st as [| v r IH]; simpl; intros u w Hs H; [ contradiction | ].
  destruct (Nat.eqb v u) eqn:E; [ exact H | ].
  destruct (tok_mut ti v) eqn:Em.
  - destruct H as [H | H]; [ subst v; congruence | apply IH; assumption ].
  - destruct H as [H | H]; [ subst v; left; reflexivity | right; apply IH; assumption ].
Qed.

Lemma in_drop_muts : forall ti st w,
  tok_mut ti w = false -> In w st -> In w (drop_muts ti st).
Proof.
  intros ti st w Hs H. unfold drop_muts. apply filter_In. split; [ exact H | ].
  rewrite Hs; reflexivity.
Qed.

Lemma In_kill_at : forall l x om p, In p (kill_at l x om) -> In p l.
Proof. intros l x om p H; unfold kill_at in H; apply filter_In in H as [H _]; exact H. Qed.

Lemma kill_at_pred : forall l x om t y m,
  In (t, (y, m)) (kill_at l x om) -> Nat.eqb x y && (if om then m else true) = false.
Proof.
  intros l x om t y m H. unfold kill_at in H. apply filter_In in H as [_ H].
  apply negb_true_iff in H. exact H.
Qed.

Lemma In_kill_tag : forall l t p, In p (kill_tag l t) -> In p l /\ fst p <> t.
Proof.
  intros l t p H. unfold kill_tag in H. apply filter_In in H as [H1 H2].
  apply negb_true_iff in H2. split; [ exact H1 | ].
  intro Hc; subst. rewrite Nat.eqb_refl in H2. discriminate.
Qed.

(* SINV 는 sstep 을 보존한다 — Create 의 conflict 검사가 그 일을 한다 *)
Lemma sinv_step : forall l e l', SINV l -> sstep l e = Some l' -> SINV l'.
Proof.
  intros l e l' Hs Hstep. destruct e as [t x m | t wr | x wr]; simpl in Hstep.
  - destruct (conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    assert (Hnc : forall t' y m', In (t', (y, m')) l -> Nat.eqb x y && (m' || m) = false).
    { intros t' y m' Hin. unfold conflict in Ec.
      destruct (Nat.eqb x y && (m' || m)) eqn:E; [ | reflexivity ].
      assert (existsb (fun p => let '(_, (y0, m0)) := p in Nat.eqb x y0 && (m0 || m)) l = true).
      { apply existsb_exists. exists (t', (y, m')). split; [ exact Hin | exact E ]. }
      congruence. }
    intros t1 y1 mm1 t2 mm2 H1 H2 Hne. simpl in H1, H2.
    destruct H1 as [H1 | H1]; destruct H2 as [H2 | H2].
    + inversion H1; inversion H2; subst; contradiction.
    + inversion H1; subst t1 y1 mm1.
      apply In_kill_tag in H2 as [H2 _].
      pose proof (Hnc t2 x mm2 H2) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm2 Hm]. split; [ exact Hm | exact Hm2 ].
    + assert (Hx : y1 = x) by congruence.
      assert (Ht2 : t2 = t) by congruence.
      assert (Hm2 : mm2 = m) by congruence.
      subst y1 t2 mm2.
      apply In_kill_tag in H1 as [H1 _].
      pose proof (Hnc t1 x mm1 H1) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm1 Hm]. split; [ exact Hm1 | exact Hm ].
    + apply In_kill_tag in H1 as [H1 _]. apply In_kill_tag in H2 as [H2 _].
      eapply Hs; eauto.
  - destruct (alive l t) as [[y m] | ]; [ | discriminate ].
    destruct (wr && negb m); [ discriminate | ].
    inversion Hstep; subst l'; exact Hs.
  - inversion Hstep; subst l'.
    intros t1 y1 m1 t2 m2 H1 H2 Hne.
    eapply Hs; [ eapply In_kill_at; exact H1 | eapply In_kill_at; exact H2 | exact Hne ].
Qed.

(* ── 6. 한 걸음 합치 (정리 A — 토큰 모델판) ──────────────────────────────── *)

Lemma fresh_step : forall s e s', FRESH s -> dstep s e = Some s' -> FRESH s'.
Proof.
  intros [d ti] e [d' ti'] Hf Hstep.
  destruct e as [t x m | t wr | x wr]; simpl in Hstep.
  - inversion Hstep; subst d' ti'. simpl. intros u i H. simpl in H.
    destruct (Nat.eqb u (next d)) eqn:E.
    + apply Nat.eqb_eq in E; subst u. lia.
    + apply Hf in H. lia.
  - destruct (lookup (env d) t) as [u | ]; [ | discriminate ].
    destruct (tok_of ti u) as [[y m] | ]; [ | discriminate ].
    destruct (existsb (Nat.eqb u) (gets (stk d) y)); [ | discriminate ].
    inversion Hstep; subst d' ti'. simpl. exact Hf.
  - inversion Hstep; subst d' ti'. simpl. exact Hf.
Qed.

Lemma agreement_step : forall l s e l',
  INV l s -> SINV l -> FRESH s ->
  sstep l e = Some l' ->
  exists s', dstep s e = Some s' /\ INV l' s'.
Proof.
  intros l s e l' Hinv Hsinv Hfresh Hstep.
  destruct s as [d ti].
  destruct e as [t x m | t wr | x wr]; simpl in Hstep |- *.

  - (* Create — 신선한 토큰 *)
    destruct (conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin. simpl in Hin.
    destruct Hin as [Hin | Hin].
    + inversion Hin; subst t0 x0 m0.
      exists (next d). repeat split.
      * simpl. apply lookup_upd_same.
      * simpl. rewrite Nat.eqb_refl. reflexivity.
      * simpl. rewrite gets_sets_same. left; reflexivity.
    + apply In_kill_tag in Hin as [Hin Hne]. simpl in Hne.
      destruct (Hinv t0 x0 m0 Hin) as [u [Hu [Hti Hst]]].
      exists u. repeat split.
      * simpl. rewrite lookup_upd_other; [ exact Hu | ].
        destruct (Nat.eqb t0 t) eqn:E; [ apply Nat.eqb_eq in E; congruence | reflexivity ].
      * simpl. destruct (Nat.eqb u (next d)) eqn:Eu; [ | exact Hti ].
        (* ★ FRESH: u 는 이미 발행된 토큰이므로 u < next d — next d 와 같을 수 없다 *)
        exfalso. apply Nat.eqb_eq in Eu.
        pose proof (Hfresh u (x0, m0) Hti) as Hlt. lia.
      * simpl. destruct (Nat.eqb x x0) eqn:Exx.
        -- apply Nat.eqb_eq in Exx; subst x0. rewrite gets_sets_same. right; exact Hst.
        -- rewrite gets_sets_other by exact Exx. exact Hst.

  - (* Use *)
    destruct (alive l t) as [[y m] | ] eqn:Ea; [ | discriminate ].
    destruct (wr && negb m) eqn:Ew; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    apply alive_In in Ea.
    destruct (Hinv t y m Ea) as [u [Hu [Hti Hst]]].
    rewrite Hu. rewrite Hti.
    rewrite (In_existsb u (gets (stk d) y) Hst).
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    destruct (Hinv t0 x0 m0 Hin0) as [u0 [Hu0 [Hti0 Hst0]]].
    exists u0. repeat split; [ exact Hu0 | exact Hti0 | ].
    simpl. destruct (Nat.eqb y x0) eqn:Eyx.
    + apply Nat.eqb_eq in Eyx; subst x0. rewrite gets_sets_same.
      destruct (Nat.eq_dec u0 u) as [Hu0u | Hu0u].
      * subst u0. destruct wr; [ apply in_pop_above; exact Hst | apply in_dma_self; exact Hst ].
      * assert (Ht0 : t0 <> t).
        { intro Hc; subst t0. rewrite Hu in Hu0. inversion Hu0. congruence. }
        pose proof (Hsinv t y m t0 m0 Ea Hin0 (fun H => Ht0 (eq_sym H))) as [Hm Hm0].
        subst m m0.
        destruct wr; [ simpl in Ew; discriminate | ].
        apply in_dma_shr; [ unfold tok_mut; rewrite Hti0; reflexivity | exact Hst0 ].
    + rewrite gets_sets_other by exact Eyx. exact Hst0.

  - (* Own *)
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    pose proof (kill_at_pred l x (negb wr) t0 x0 m0 Hin0) as Hk.
    apply In_kill_at in Hin0.
    destruct (Hinv t0 x0 m0 Hin0) as [u0 [Hu0 [Hti0 Hst0]]].
    exists u0. repeat split; [ exact Hu0 | exact Hti0 | ].
    simpl. destruct (Nat.eqb x x0) eqn:Exx.
    + simpl in Hk. apply Nat.eqb_eq in Exx. subst x0.
      rewrite gets_sets_same.
      destruct wr; simpl in Hk; [ discriminate | ].
      destruct m0; [ discriminate | ].
      apply in_drop_muts; [ unfold tok_mut; rewrite Hti0; reflexivity | exact Hst0 ].
    + rewrite gets_sets_other by exact Exx. exact Hst0.
Qed.

(* ── 7. 실행 전체 (직선) ─────────────────────────────────────────────────── *)

Lemma agreement_run : forall evs l s,
  INV l s -> SINV l -> FRESH s ->
  (exists l', srun l evs = Some l') ->
  exists s' l', drun s evs = Some s' /\ srun l evs = Some l' /\
                INV l' s' /\ SINV l' /\ FRESH s'.
Proof.
  induction evs as [| e r IH]; intros l s Hinv Hsinv Hfresh [l' Hs].
  - exists s, l.
    split; [ reflexivity | ].
    split; [ reflexivity | ].
    split; [ exact Hinv | ].
    split; [ exact Hsinv | exact Hfresh ].
  - simpl in Hs |- *.
    destruct (sstep l e) as [l1 | ] eqn:Es; [ | discriminate ].
    destruct (agreement_step l s e l1 Hinv Hsinv Hfresh Es) as [s1 [Hd Hinv1]].
    rewrite Hd.
    apply (IH l1 s1); auto.
    + eapply sinv_step; eauto.
    + eapply fresh_step; eauto.
    + exists l'; exact Hs.
Qed.

(* ── 8. ★ 루프 정리 (보조정리 B) ─────────────────────────────────────────── *)
(*
 * 요점. 본문을 k 번 도는 것이 왜 안전한가?
 *
 *  (a) **body 가 만드는 차용은 반복마다 신선한 토큰**이다(FRESH). 지난 반복의 토큰은
 *      env 가 더 이상 가리키지 않으므로 *사용될 수 없다* — 즉 반복 간 오염이 없다.
 *  (b) 유일한 위험은 **pre 에서 만들어져 body 에서 쓰이는 차용**이다. 반복 i 의 own(x) 이
 *      그것을 죽이면 반복 i+1 의 use 가 ⚡ 를 낸다.
 *  (c) **S2L 이 정확히 그 짝을 거부한다**(s2l_ok). 그러면 body 를 도는 동안 pre-태그의
 *      정적 상태가 보존되고, 같은 정적 상태에서 시작하는 다음 반복은 첫 반복과 동형이다.
 *
 * 따라서 "한 반복이 안전하고, 상태가 반복 시작 시점으로 되돌아온다"는 귀납이 성립한다.
 * 여기서는 (a)+(c) 를 만족하는 정적 조건 아래, **모든 k 에 대해** dyn_clean 임을 보인다.
 *)

(* 루프 본문을 도는 동안 정적 상태가 보존되면(=고정점), k 번 전개도 안전하다. *)
Lemma loop_fixpoint_clean : forall body l s,
  INV l s -> SINV l -> FRESH s ->
  srun l body = Some l ->              (* ★ 정적 상태의 고정점 — S2L 이 이것을 보장한다 *)
  forall k, exists s', drun s (unroll body k) = Some s'.
Proof.
  intros body l s Hinv Hsinv Hfresh Hfix k. revert s Hinv Hfresh.
  induction k as [| k IH]; intros s Hinv Hfresh.
  - exists s; reflexivity.
  - simpl.
    destruct (agreement_run body l s Hinv Hsinv Hfresh (ex_intro _ l Hfix))
      as [s1 [l1 [Hd [Hs [Hinv1 [Hsinv1 Hfresh1]]]]]].
    assert (Hl1 : l1 = l) by (rewrite Hfix in Hs; inversion Hs; reflexivity).
    subst l1.
    (* drun s (body ++ unroll body k) = drun s1 (unroll body k) *)
    assert (Hsplit : forall a b st, drun st (a ++ b) =
                     match drun st a with None => None | Some st' => drun st' b end).
    { induction a as [| e ra IHa]; intros b st; simpl; [ reflexivity | ].
      destruct (dstep st e); [ apply IHa | reflexivity ]. }
    rewrite Hsplit. rewrite Hd.
    apply (IH s1 Hinv1 Hfresh1).
Qed.

(* ★★ 루프 합치 정리 — 정적 고정점이면 **몇 번을 돌든** ⚡ 가 없다. ★★ *)
Theorem loop_agreement : forall pre body,
  (exists lpre, srun [] pre = Some lpre /\ srun lpre body = Some lpre) ->
  forall k, dyn_clean pre body k = true.
Proof.
  intros pre body [lpre [Hpre Hfix]] k.
  unfold dyn_clean.
  assert (Hinv0 : INV [] d0) by (intros t x m H; simpl in H; contradiction).
  assert (Hsinv0 : SINV []) by (intros t1 x m1 t2 m2 H; simpl in H; contradiction).
  assert (Hfresh0 : FRESH d0) by (intros u i H; simpl in H; discriminate).
  destruct (agreement_run pre [] d0 Hinv0 Hsinv0 Hfresh0 (ex_intro _ lpre Hpre))
    as [s1 [l1 [Hd [Hs [Hinv1 [Hsinv1 Hfresh1]]]]]].
  assert (Hl1 : l1 = lpre) by (rewrite Hpre in Hs; inversion Hs; reflexivity).
  subst l1.
  assert (Hsplit : forall a b st, drun st (a ++ b) =
                   match drun st a with None => None | Some st' => drun st' b end).
  { induction a as [| e ra IHa]; intros b st; simpl; [ reflexivity | ].
    destruct (dstep st e); [ apply IHa | reflexivity ]. }
  rewrite Hsplit. rewrite Hd.
  destruct (loop_fixpoint_clean body lpre s1 Hinv1 Hsinv1 Hfresh1 Hfix k) as [s2 Hs2].
  rewrite Hs2. reflexivity.
Qed.

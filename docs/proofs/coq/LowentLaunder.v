(* LowentLaunder.v — V3: **op 경계**. 차용을 op 로 "세탁"해 참조를 돌려받는 패턴.
 *
 * LowentEXCL.v 의 이벤트 언어에는 **호출이 없다.** 그래서 이 패턴이 모델 밖에 있었다:
 *
 *     let r  = &mut x .        rem 차용 t1
 *     let r2 = f r .           rem ★ op 이 인자 차용을 **세탁해** 참조를 돌려준다
 *     set x  = … .             rem 소유자 쓰기 — t1 이 죽는다
 *     use r2 .                 rem ⚡ r2 는 t1 과 **같은 차용**이다
 *
 * 구현은 이것을 겪었다: 정적 검사가 r2 를 차용으로 **알아보지 못해** 통과시켰다.
 * UNPROVEN.md 가 "열린 갭 D3(op 경계)" 로 기록했고, E-ESCAPE(반환 참조는 인자 유래)를
 * 근거로 **결과를 인자 차용의 별칭으로 등록**해 고쳤다(low_region.c 의 alias[]).
 *
 * 이 파일이 하는 일은 둘이다:
 *   (1) ★ 고치기 전의 규칙이 **건전하지 않았음을 기계 검증된 반례로 못 박는다.**
 *       지금까지 그것은 "우리가 찾았다"는 *이야기*였다. 이제 정리다.
 *   (2) ★ 고친 규칙(별칭 등록)에서 정리 A 가 **그대로 성립함**을 증명한다.
 *
 * 갚는 것: UNPROVEN R1 의 "남은 것: op 경계(D3 별칭)".
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 이벤트 — Launder 가 추가된다 ──────────────────────────────────────── *)

Inductive ev : Type :=
  | Create  (tag : nat) (x : nat) (mut : bool)
  | Use     (tag : nat) (wr : bool)
  | Own     (x : nat)   (wr : bool)
  | Launder (tag' : nat) (tag : nat).   (* op 결과 tag' 는 인자 차용 tag 의 **다른 이름**이다 *)

(* Create 가 발행한 근본 정보 *)
Fixpoint create_info (evs : list ev) (t : nat) : option (nat * bool) :=
  match evs with
  | [] => None
  | Create t' x m :: r => if Nat.eqb t t' then Some (x, m) else create_info r t
  | _ :: r => create_info r t
  end.

(* 별칭 한 홉. 세탁된 이름 → 그 근본 차용. 세탁된 적 없으면 자기 자신. *)
Fixpoint alias_of (evs : list ev) (t : nat) : nat :=
  match evs with
  | [] => t
  | Launder t' u :: r => if Nat.eqb t t' then u else alias_of r t
  | _ :: r => alias_of r t
  end.

(* ★ 핵심: 세탁된 이름의 정보 = **근본 차용의 정보**. 같은 지역, 같은 배타성. *)
Definition tag_info (evs : list ev) (t : nat) : option (nat * bool) :=
  create_info evs (alias_of evs t).

Definition is_mut (evs : list ev) (t : nat) : bool :=
  match tag_info evs t with Some (_, m) => m | None => false end.

(* 적형 — 구현의 신선 태그 발행이 주는 것:
   (a) Create 된 태그는 자기 정보를 찾고, 세탁된 적이 없다(별칭의 대상이지 별칭이 아니다).
   (b) 세탁은 **근본 차용을 가리킨다**(별칭의 별칭은 없다 — 구현의 alias[] 도 그렇다). *)
Definition wf (evs : list ev) : Prop :=
  (forall t x m, In (Create t x m) evs ->
     create_info evs t = Some (x, m) /\ alias_of evs t = t)
  /\ (forall t' u, In (Launder t' u) evs ->
     alias_of evs t' = u /\ alias_of evs u = u).

(* ── 2. 동적 의미 — 세탁은 **아무 일도 하지 않는다** ──────────────────────── *)
(* 이것이 요점이다. r2 는 새 차용이 아니라 **같은 차용의 다른 이름**이다.
   그래서 스택에는 근본 태그 하나만 있고, r2 를 쓰는 것은 그 태그를 쓰는 것이다. *)

Definition state := list (nat * list nat).

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

Definition step (evs : list ev) (s : state) (e : ev) : option state :=
  match e with
  | Create t x _ => Some (set_stk s x (t :: get_stk s x))
  | Launder _ _  => Some s                       (* ★ 새 차용이 아니다 — 아무 일도 없다 *)
  | Use t wr =>
      let r := alias_of evs t in                 (* ★ 세탁된 이름은 근본 태그로 풀린다 *)
      match create_info evs r with
      | None => None
      | Some (x, _) =>
          let st := get_stk s x in
          if existsb (Nat.eqb r) st
          then Some (set_stk s x (if wr then pop_above st r
                                        else drop_muts_above evs st r))
          else None                              (* ⚡ 무효화된 차용 사용 *)
      end
  | Own x wr => Some (set_stk s x (if wr then [] else drop_muts evs (get_stk s x)))
  end.

Fixpoint run (evs : list ev) (s : state) (rest : list ev) : option state :=
  match rest with
  | [] => Some s
  | e :: r => match step evs s e with None => None | Some s' => run evs s' r end
  end.

Definition dyn_clean (evs : list ev) : bool :=
  match run evs [] evs with Some _ => true | None => false end.

(* ── 3. 정적 판정 ─────────────────────────────────────────────────────────── *)

Definition live := list (nat * (nat * bool)).

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
  filter (fun e => let '(_, (y, m)) := e in
                   negb (Nat.eqb x y && (if only_mut then m else true))) l.

Definition has_conflict (l : live) (x : nat) (new_mut : bool) : bool :=
  existsb (fun e => let '(_, (y, m)) := e in Nat.eqb x y && (m || new_mut)) l.

(* ★★ 고친 규칙 (D3 수정, low_region.c 의 alias[]):
   op 이 참조를 돌려주면 그 결과를 **인자 차용의 별칭으로 등록한다.**
   그러면 인자 차용이 죽을 때(소유자 접근) 결과도 함께 죽는다 — kill_at 이 지역으로 죽이므로. *)
Definition sstep (s : live) (e : ev) : option live :=
  match e with
  | Create t x m => if has_conflict s x m then None else Some ((t, (x, m)) :: s)
  | Launder t' t =>
      match alive_of s t with
      | None => Some s                          (* 인자가 차용이 아니었다 → 결과도 참조가 아니다 *)
      | Some (x, m) => Some ((t', (x, m)) :: s) (* ★ 별칭 등록 *)
      end
  | Use t wr =>
      match alive_of s t with
      | None => None
      | Some (_, m) => if wr && negb m then None else Some s
      end
  | Own x wr => Some (kill_at s x (negb wr))
  end.

Fixpoint srun (s : live) (evs : list ev) : option live :=
  match evs with
  | [] => Some s
  | e :: r => match sstep s e with None => None | Some s' => srun s' r end
  end.

Definition static_green (evs : list ev) : bool :=
  match srun [] evs with Some _ => true | None => false end.

(* ── 4. ★ 고치기 **전**의 규칙 — 그리고 그것이 건전하지 않다는 증명 ──────── *)
(* 고치기 전에는 op 결과가 차용으로 등록되지 않았다. 그러면 정적 검사는 그 이름을
   "차용이 아닌 어떤 값" 으로 보고 **아무 검사도 하지 않는다.** 그것이 거짓음성이다. *)

Definition sstep_bad (s : live) (e : ev) : option live :=
  match e with
  | Create t x m => if has_conflict s x m then None else Some ((t, (x, m)) :: s)
  | Launder _ _ => Some s                       (* ✗ 등록하지 않는다 *)
  | Use t wr =>
      match alive_of s t with
      | None => Some s                          (* ✗ 모르는 이름 = 차용 아님 → 검사 없음 *)
      | Some (_, m) => if wr && negb m then None else Some s
      end
  | Own x wr => Some (kill_at s x (negb wr))
  end.

Fixpoint srun_bad (s : live) (evs : list ev) : option live :=
  match evs with
  | [] => Some s
  | e :: r => match sstep_bad s e with None => None | Some s' => srun_bad s' r end
  end.

Definition static_green_bad (evs : list ev) : bool :=
  match srun_bad [] evs with Some _ => true | None => false end.

(* 반례 — 위 주석의 프로그램 그대로. *)
Definition laundered_escape : list ev :=
  [ Create 1 0 true;     (* let r  = &mut x .   태그 1 *)
    Launder 2 1;         (* let r2 = f r .      ★ 세탁 — r2 는 태그 1 의 다른 이름 *)
    Own 0 true;          (* set x = … .         소유자 쓰기 → 태그 1 이 죽는다 *)
    Use 2 true ].        (* use r2 .            ⚡ *)

Example laundered_escape_wf : wf laundered_escape.
Proof.
  split.
  - intros t x m H. simpl in H.
    destruct H as [H | H]; [ inversion H; subst; split; reflexivity | ].
    destruct H as [H | H]; [ discriminate | ].
    destruct H as [H | H]; [ discriminate | ].
    destruct H as [H | H]; [ discriminate | contradiction ].
  - intros t' u H. simpl in H.
    destruct H as [H | H]; [ discriminate | ].
    destruct H as [H | H]; [ inversion H; subst; split; reflexivity | ].
    destruct H as [H | H]; [ discriminate | ].
    destruct H as [H | H]; [ discriminate | contradiction ].
Qed.

(* 이 프로그램은 실제로 차용 위반을 낸다. *)
Example laundered_escape_is_dirty : dyn_clean laundered_escape = false.
Proof. reflexivity. Qed.

(* ★★★ 정리 — **고치기 전의 규칙은 건전하지 않았다.**
   정적으로 통과(green)하는데 실행하면 ⚡ 가 난다. 거짓음성이다.
   지금까지 이것은 "우리가 겪었다"는 이야기였다. 이제 기계가 검사한 **정리**다. *)
Theorem old_rule_is_unsound :
  wf laundered_escape /\
  static_green_bad laundered_escape = true /\      (* 정적 검사는 통과시킨다 *)
  dyn_clean laundered_escape = false.              (* 그런데 실행하면 ⚡ *)
Proof.
  split; [ apply laundered_escape_wf | split; reflexivity ].
Qed.

(* ★ 그리고 **고친 규칙은 그것을 잡는다.** *)
Theorem new_rule_catches_it : static_green laundered_escape = false.
Proof. reflexivity. Qed.

(* ★ 과잉 거부가 아니다 — **정상적인 세탁은 여전히 통과한다.** *)
Definition honest_laundering : list ev :=
  [ Create 1 0 true;     (* let r  = &mut x . *)
    Launder 2 1;         (* let r2 = f r .    *)
    Use 2 true;          (* use r2 .          — 소유자가 끼어들지 않았다 *)
    Use 2 false ].

Example honest_laundering_green : static_green honest_laundering = true.
Proof. reflexivity. Qed.

Example honest_laundering_clean : dyn_clean honest_laundering = true.
Proof. reflexivity. Qed.

(* 세탁된 이름과 원본 이름이 **같은 차용**임도 확인된다: 원본을 쓴 뒤 세탁본을 써도 된다. *)
Definition both_names : list ev :=
  [ Create 1 0 true; Launder 2 1; Use 1 true; Use 2 true; Use 1 false ].

Example both_names_green : static_green both_names = true.
Proof. reflexivity. Qed.

Example both_names_clean : dyn_clean both_names = true.
Proof. reflexivity. Qed.

(* ── 5. 불변식 ────────────────────────────────────────────────────────────── *)

(* INV — 살아 있는 (세탁된 것 포함) 이름은 **그 근본 태그가** 스택에 있다. *)
Definition INV (evs : list ev) (l : live) (s : state) : Prop :=
  forall t x m, In (t, (x, m)) l ->
    tag_info evs t = Some (x, m)
    /\ alias_of evs (alias_of evs t) = alias_of evs t     (* 별칭의 별칭은 없다 *)
    /\ In (alias_of evs t) (get_stk s x).

(* SINV — **서로 다른 근본 차용**이 같은 지역에 살아 있으면 모두 shr.
   세탁된 이름은 원본과 같은 근본 태그를 가지므로 이 조건에 걸리지 않는다.
   (평평한 모델의 SINV 는 태그로 비교했다. 세탁이 들어오면 **근본 태그**로 비교해야 한다 —
   이것이 이 파일이 드러낸 형식화의 핵심이다.) *)
Definition SINV (evs : list ev) (l : live) : Prop :=
  forall t1 x m1 t2 m2,
    In (t1, (x, m1)) l -> In (t2, (x, m2)) l ->
    alias_of evs t1 <> alias_of evs t2 ->
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

Lemma In_kill_at : forall l x om e, In e (kill_at l x om) -> In e l.
Proof. intros l x om e H; unfold kill_at in H; apply filter_In in H as [H _]; exact H. Qed.

Lemma kill_at_pred : forall l x om t y m,
  In (t, (y, m)) (kill_at l x om) ->
  Nat.eqb x y && (if om then m else true) = false.
Proof.
  intros l x om t y m H. unfold kill_at in H. apply filter_In in H as [_ H].
  apply negb_true_iff in H. exact H.
Qed.

(* 근본 태그의 is_mut = 그 이름의 배타성. INV 의 '별칭의 별칭 없음' 이 이것을 준다. *)
Lemma is_mut_root : forall evs t x m,
  tag_info evs t = Some (x, m) ->
  alias_of evs (alias_of evs t) = alias_of evs t ->
  is_mut evs (alias_of evs t) = m.
Proof.
  intros evs t x m Hti Hidem.
  unfold is_mut, tag_info in *. rewrite Hidem. rewrite Hti. reflexivity.
Qed.

Lemma sinv_step : forall evs l e l',
  wf evs -> In e evs ->
  SINV evs l -> sstep l e = Some l' -> SINV evs l'.
Proof.
  intros evs l e l' [Hwc Hwl] Hine Hs Hstep.
  destruct e as [t x m | t wr | x wr | t' t]; simpl in Hstep.
  - (* Create *)
    destruct (has_conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    assert (Hnc : forall t0 y m0, In (t0, (y, m0)) l -> Nat.eqb x y && (m0 || m) = false).
    { intros t0 y m0 Hin. unfold has_conflict in Ec.
      destruct (Nat.eqb x y && (m0 || m)) eqn:E; [ | reflexivity ].
      assert (existsb (fun e => let '(_, (y1, m1)) := e in Nat.eqb x y1 && (m1 || m)) l = true).
      { apply existsb_exists. exists (t0, (y, m0)). split; [ exact Hin | exact E ]. }
      congruence. }
    intros t1 y1 mm1 t2 mm2 H1 H2 Hne. simpl in H1, H2.
    destruct H1 as [H1 | H1]; destruct H2 as [H2 | H2].
    + inversion H1; inversion H2; subst; contradiction.
    + inversion H1; subst t1 y1 mm1.
      pose proof (Hnc t2 x mm2 H2) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm2 Hm]. split; [ exact Hm | exact Hm2 ].
    + assert (Hy : y1 = x) by congruence.
      assert (Ht2 : t2 = t) by congruence.
      assert (Hm2 : mm2 = m) by congruence.
      subst y1 t2 mm2.
      pose proof (Hnc t1 x mm1 H1) as He. rewrite Nat.eqb_refl in He. simpl in He.
      apply orb_false_iff in He as [Hm1 Hm]. split; [ exact Hm1 | exact Hm ].
    + eapply Hs; eauto.
  - (* Use *)
    destruct (alive_of l t) as [[y m] | ]; [ | discriminate ].
    destruct (wr && negb m); [ discriminate | ].
    inversion Hstep; subst l'; exact Hs.
  - (* Own *)
    inversion Hstep; subst l'.
    intros t1 y1 m1 t2 m2 H1 H2 Hne.
    eapply Hs; [ eapply In_kill_at; exact H1 | eapply In_kill_at; exact H2 | exact Hne ].
  - (* ★ Launder — 별칭은 근본 태그를 원본과 **공유하므로** SINV 를 깨지 않는다. *)
    pose proof (Hwl t' t Hine) as [Hal Hau].     (* alias_of t' = t · alias_of t = t *)
    assert (Hsame : alias_of evs t' = alias_of evs t) by (rewrite Hal, Hau; reflexivity).
    destruct (alive_of l t) as [[x m] | ] eqn:Ea; [ | inversion Hstep; subst l'; exact Hs ].
    inversion Hstep; subst l'. clear Hstep.
    apply alive_of_In in Ea.
    intros t1 y1 mm1 t2 mm2 H1 H2 Hne. simpl in H1, H2.
    destruct H1 as [H1 | H1]; destruct H2 as [H2 | H2].
    + inversion H1; inversion H2; subst; contradiction.
    + inversion H1; subst t1 y1 mm1.
      rewrite Hsame in Hne.                       (* 근본으로 갈아탄다 *)
      eapply (Hs t x m t2 mm2 Ea H2 Hne).
    + assert (Ht2 : t2 = t') by congruence.
      assert (Hy : y1 = x) by congruence.
      assert (Hm2 : mm2 = m) by congruence.
      subst t2 y1 mm2.
      rewrite Hsame in Hne.
      apply (Hs t1 x mm1 t m H1 Ea Hne).
    + eapply Hs; eauto.
Qed.

(* ── 7. 정리 A(세탁 판) — 한 걸음 ────────────────────────────────────────── *)

Lemma agreement_step : forall evs l s e l',
  wf evs -> In e evs ->
  INV evs l s -> SINV evs l ->
  sstep l e = Some l' ->
  exists s', step evs s e = Some s' /\ INV evs l' s'.
Proof.
  intros evs l s e l' Hwf Hine Hinv Hsinv Hstep.
  destruct Hwf as [Hwc Hwl].
  destruct e as [t x m | t wr | x wr | t' t]; simpl in Hstep |- *.

  - (* Create — D1 *)
    destruct (has_conflict l x m) eqn:Ec; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    pose proof (Hwc t x m Hine) as [Hci Hid].
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin. simpl in Hin.
    destruct Hin as [Hin | Hin].
    + inversion Hin; subst t0 x0 m0.
      unfold tag_info. rewrite Hid.
      repeat split.
      * exact Hci.
      * try rewrite Hid; reflexivity.
      * try rewrite Hid; rewrite get_set_same; left; reflexivity.
    + pose proof (Hinv t0 x0 m0 Hin) as [Hti [Hid0 Hex]].
      repeat split; [ exact Hti | exact Hid0 | ].
      destruct (Nat.eqb x x0) eqn:Exx.
      * apply Nat.eqb_eq in Exx; subst x0. rewrite get_set_same. right; exact Hex.
      * rewrite get_set_other by exact Exx. exact Hex.

  - (* Use — D2 / D3 *)
    destruct (alive_of l t) as [[y m] | ] eqn:Ea; [ | discriminate ].
    destruct (wr && negb m) eqn:Ew; [ discriminate | ].
    inversion Hstep; subst l'. clear Hstep.
    apply alive_of_In in Ea.
    pose proof (Hinv t y m Ea) as [Hti [Hid Hex]].
    unfold tag_info in Hti.
    rewrite Hti.
    rewrite (In_existsb (alias_of evs t) (get_stk s y) Hex).
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    pose proof (Hinv t0 x0 m0 Hin0) as [Hti0 [Hid0 Hex0]].
    repeat split; [ exact Hti0 | exact Hid0 | ].
    destruct (Nat.eqb y x0) eqn:Eyx.
    + apply Nat.eqb_eq in Eyx; subst x0. rewrite get_set_same.
      destruct (Nat.eq_dec (alias_of evs t0) (alias_of evs t)) as [Hr | Hr].
      * (* ★ 같은 **근본 차용** — 세탁된 이름이든 원본이든. 두 규칙 모두 그 태그를 남긴다. *)
        rewrite Hr. destruct wr.
        -- apply in_pop_above; exact Hex.
        -- apply in_drop_muts_above_self; exact Hex.
      * (* 다른 근본 차용 — SINV 가 둘 다 shr 임을 보장한다 *)
        pose proof (Hsinv t y m t0 m0 Ea Hin0 (fun H => Hr (eq_sym H))) as [Hm Hm0].
        subst m m0.
        destruct wr; [ simpl in Ew; discriminate | ].
        apply in_drop_muts_above; [ | exact Hex0 ].
        apply (is_mut_root evs t0 y false); [ exact Hti0 | exact Hid0 ].
    + rewrite get_set_other by exact Eyx. exact Hex0.

  - (* Own — D4 / D5 *)
    inversion Hstep; subst l'. clear Hstep.
    eexists; split; [ reflexivity | ].
    intros t0 x0 m0 Hin0.
    pose proof (kill_at_pred l x (negb wr) t0 x0 m0 Hin0) as Hk.
    apply In_kill_at in Hin0.
    pose proof (Hinv t0 x0 m0 Hin0) as [Hti0 [Hid0 Hex0]].
    repeat split; [ exact Hti0 | exact Hid0 | ].
    destruct (Nat.eqb x x0) eqn:Exx.
    + apply Nat.eqb_eq in Exx. subst x0.
      rewrite get_set_same.
      simpl in Hk.
      destruct wr; simpl in Hk.
      * discriminate.
      * destruct m0; [ discriminate | ].
        apply in_drop_muts; [ | exact Hex0 ].
        apply (is_mut_root evs t0 x false); [ exact Hti0 | exact Hid0 ].
    + rewrite get_set_other by exact Exx. exact Hex0.

  - (* ★★ Launder — 동적으로는 아무 일도 없다. 정적으로는 별칭이 등록된다. *)
    pose proof (Hwl t' t Hine) as [Hal Hau].
    destruct (alive_of l t) as [[x m] | ] eqn:Ea.
    + inversion Hstep; subst l'. clear Hstep.
      apply alive_of_In in Ea.
      pose proof (Hinv t x m Ea) as [Hti [Hid Hex]].
      eexists; split; [ reflexivity | ].
      intros t0 x0 m0 Hin0. simpl in Hin0.
      destruct Hin0 as [Hin0 | Hin0].
      * inversion Hin0; subst t0 x0 m0.
        (* 새 별칭 t' — 근본은 t 와 같다 *)
        assert (Hsame : alias_of evs t' = alias_of evs t) by (rewrite Hal, Hau; reflexivity).
        repeat split.
        -- unfold tag_info in *. rewrite Hsame. exact Hti.
        -- rewrite Hsame. exact Hid.
        -- rewrite Hsame. exact Hex.
      * pose proof (Hinv t0 x0 m0 Hin0) as [Hti0 [Hid0 Hex0]].
        repeat split; assumption.
    + inversion Hstep; subst l'. clear Hstep.
      eexists; split; [ reflexivity | ]. exact Hinv.
Qed.

(* ── 8. 정리 A(세탁 판) — 전체 실행 ──────────────────────────────────────── *)

Lemma agreement_run : forall evs rest l s,
  wf evs ->
  (forall e, In e rest -> In e evs) ->
  INV evs l s -> SINV evs l ->
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

(* ★★ 정리 A — **세탁 판**. op 경계를 넘는 차용에도 합치가 성립한다. ★★
   즉 **고친 규칙(별칭 등록)은 건전하다.** 위의 old_rule_is_unsound 와 짝을 이룬다:
   옛 규칙은 반증됐고, 새 규칙은 증명됐다. *)
Theorem agreement : forall evs,
  wf evs -> static_green evs = true -> dyn_clean evs = true.
Proof.
  intros evs Hwf Hg.
  unfold static_green in Hg. destruct (srun [] evs) as [l' | ] eqn:Es; [ | discriminate ].
  unfold dyn_clean.
  assert (Hinv : INV evs [] []) by (intros t x m H; simpl in H; contradiction).
  assert (Hsinv : SINV evs []) by (intros t1 x m1 t2 m2 H; simpl in H; contradiction).
  pose proof (agreement_run evs evs [] [] Hwf (fun e H => H) Hinv Hsinv (ex_intro _ l' Es))
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

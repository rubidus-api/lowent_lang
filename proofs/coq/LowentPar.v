(* LowentPar.v — R2: level-1 **결정론 병렬**. 병렬 결과가 순차와 *비트 동일*한가.
 *
 * UNPROVEN R2 의 주장:
 *   "level-1 병렬은 순차와 **비트 동일** 결과다 —
 *      DET-1  disjoint split
 *      DET-2  의존 sync
 *      DET-3  고정 reduction tree"
 *   상태: 미증명. "충분히 똑똑한 스케줄러 + disjoint 증명" **가정**.
 *
 * ★ 먼저 정직해지자: **이 셋 중 둘은 Iris 가 필요 없다.**
 *   결정론은 데이터 경합의 문제가 아니라 **Bernstein 조건**의 문제다. 태스크들이 서로의
 *   읽기/쓰기 집합을 건드리지 않으면, 순서를 바꿔도 결과가 같다 — 순수 Coq 으로 증명된다.
 *   Iris 가 진짜로 필요한 곳은 R7(atomics·race=UB)이지 여기가 아니다.
 *   그래서 여기서 DET-1·DET-2·DET-3 을 갚고, R2 의 "가정" 을 정리로 바꾼다.
 *
 * ★ 그리고 더 정직해지자: **level-1 병렬은 아직 구현되지 않았다**(MVP 제외).
 *   그러므로 이 파일은 구현을 검증하지 않는다 — 구현이 **지켜야 할 조건을 확정한다.**
 *   DET-1 의 "disjoint" 가 정확히 무엇이어야 하는지, DET-3 의 트리를 왜 고정해야 하는지가
 *   여기서 정리로 못 박힌다. 구현이 오면 이 조건을 검사하면 된다.
 *)

Require Import List Bool Arith ZArith Lia.
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 태스크와 Bernstein 독립성 ─────────────────────────────────────────── *)

Definition state := nat -> Z.

(* 태스크 = (쓰기 집합, 읽기 집합, 계산). 계산은 상태를 보고 자기 쓰기 자리의 새 값을 낸다. *)
Record task := mk_task {
  wr : list nat;
  rd : list nat;
  fn : state -> nat -> Z
}.

Definition inb (i : nat) (l : list nat) : bool := existsb (Nat.eqb i) l.

Lemma inb_true : forall i l, inb i l = true -> In i l.
Proof.
  induction l as [| a r IH]; simpl; intros H; [ discriminate | ].
  apply orb_true_iff in H as [H | H].
  - apply Nat.eqb_eq in H; subst a; left; reflexivity.
  - right; apply IH; exact H.
Qed.

Lemma inb_false : forall i l, inb i l = false -> ~ In i l.
Proof.
  induction l as [| a r IH]; simpl; intros H Hin; [ contradiction | ].
  apply orb_false_iff in H as [H1 H2].
  destruct Hin as [Hin | Hin].
  - subst a; rewrite Nat.eqb_refl in H1; discriminate.
  - apply (IH H2 Hin).
Qed.

(* 한 태스크의 적용: 자기 쓰기 자리만 바꾼다. *)
Definition apply (t : task) (s : state) : state :=
  fun i => if inb i (wr t) then fn t s i else s i.

(* ★ 지역성 — 태스크의 계산은 **자기 읽기 집합만** 본다.
   이것은 가정이 아니라 **구현이 지켜야 할 조건**이다(level-1 병렬의 전제). *)
Definition local (t : task) : Prop :=
  forall s s', (forall i, In i (rd t) -> s i = s' i) -> forall i, fn t s i = fn t s' i.

Definition disj (a b : list nat) : Prop := forall i, In i a -> In i b -> False.

(* ★★ Bernstein 조건 — 두 태스크가 **독립**이라는 것의 정확한 뜻:
     쓰기-쓰기 겹침 없음 · 한쪽의 쓰기가 다른 쪽의 읽기와 겹침 없음(양방향).
   RFC-0009 가 "disjoint split" 이라고만 부르던 것의 형식적 내용이 이것이다. *)
Definition indep (t1 t2 : task) : Prop :=
  disj (wr t1) (wr t2) /\ disj (wr t1) (rd t2) /\ disj (rd t1) (wr t2).

Lemma disj_sym : forall a b, disj a b -> disj b a.
Proof. intros a b H i Ha Hb; exact (H i Hb Ha). Qed.

Lemma indep_sym : forall t1 t2, indep t1 t2 -> indep t2 t1.
Proof.
  intros t1 t2 [H1 [H2 H3]]. repeat split.
  - apply disj_sym; exact H1.
  - apply disj_sym; exact H3.
  - apply disj_sym; exact H2.
Qed.

(* ── 2. 순차 실행과 병렬 실행 ─────────────────────────────────────────────── *)

(* 순차: 뒤 태스크가 앞 태스크의 쓰기를 **본다**. *)
Definition seq_run (ts : list task) (s : state) : state :=
  fold_left (fun s' t => apply t s') ts s.

(* 병렬: 모든 태스크가 **원래 상태**를 읽고, 쓰기를 합친다.
   (level-1 의 의미론: fork — 각자 계산 — join.) *)
Definition par_run (ts : list task) (s : state) : state :=
  fun i => match find (fun t => inb i (wr t)) ts with
           | Some t => fn t s i
           | None   => s i
           end.

(* 리스트에 대한 짝별 독립성 *)
Fixpoint pw (ts : list task) : Prop :=
  match ts with
  | [] => True
  | t :: r => Forall (indep t) r /\ pw r
  end.

(* ── 3. ★★ DET-1 — disjoint split 이면 **병렬 = 순차**다 ───────────────────── *)

Lemma seq_run_cons : forall t ts s, seq_run (t :: ts) s = seq_run ts (apply t s).
Proof. reflexivity. Qed.

Theorem det1_par_eq_seq : forall ts s,
  Forall local ts -> pw ts ->
  forall i, par_run ts s i = seq_run ts s i.
Proof.
  induction ts as [| t ts IH]; intros s Hloc Hpw i.
  - reflexivity.
  - simpl in Hpw. destruct Hpw as [Hind Hpw].
    inversion Hloc as [| t0 ts0 Hlt Hlts]; subst.
    rewrite seq_run_cons.
    (* 순차 쪽: IH 를 apply t s 에 쓴다 *)
    rewrite <- (IH (apply t s) Hlts Hpw i).
    (* 이제 양쪽 다 par_run 이다. i 가 t 의 쓰기 자리인가로 갈린다. *)
    unfold par_run at 1. simpl.
    destruct (inb i (wr t)) eqn:Ei.
    + (* i ∈ wr t — ts 의 어떤 태스크도 i 를 쓰지 않는다(쓰기-쓰기 분리) *)
      unfold par_run.
      assert (Hnone : find (fun t' => inb i (wr t')) ts = None).
      { destruct (find (fun t' => inb i (wr t')) ts) as [t' | ] eqn:Ef; [ | reflexivity ].
        apply find_some in Ef as [Hin Hwr].
        pose proof (proj1 (Forall_forall (indep t) ts) Hind t' Hin) as [Hww _].
        exfalso. apply (Hww i (inb_true _ _ Ei) (inb_true _ _ Hwr)). }
      rewrite Hnone. unfold apply. rewrite Ei. reflexivity.
    + (* i ∉ wr t — ts 안의 (유일한) 기록자가 답을 낸다. 그 태스크의 읽기는 t 의 쓰기와
         겹치지 않으므로, 원래 상태를 읽든 apply t s 를 읽든 **같은 값**이다. *)
      unfold par_run.
      destruct (find (fun t' => inb i (wr t')) ts) as [t' | ] eqn:Ef.
      * apply find_some in Ef as [Hin _].
        pose proof (proj1 (Forall_forall (indep t) ts) Hind t' Hin) as [_ [Hwr_rd _]].
        pose proof (proj1 (Forall_forall local ts) Hlts t' Hin) as Hlt'.
        symmetry. apply (Hlt' (apply t s) s).
        intros j Hj. unfold apply.
        destruct (inb j (wr t)) eqn:Ej; [ | reflexivity ].
        exfalso. apply (Hwr_rd j (inb_true _ _ Ej) Hj).
      * unfold apply. rewrite Ei. reflexivity.
Qed.

(* 따름 — 이것이 R2 의 주장 그대로다: **병렬 결과는 순차 결과와 비트 동일하다.**
   "비트 동일" 은 여기서 문자 그대로다. 근사도, 재결합도, 오차 허용도 없다 — **같은 값**이다. *)
Corollary det1_bit_identical : forall ts s,
  Forall local ts -> pw ts ->
  forall i, par_run ts s i = seq_run ts s i.
Proof. exact det1_par_eq_seq. Qed.

(* ── 4. ★ 왜 disjoint 가 **필요**한가 — 겹치면 결정론이 깨진다 ──────────────── *)
(* 이것이 없으면 DET-1 은 장식이다. 겹치는 두 태스크는 순서가 결과를 바꾼다. *)

Definition writes_1 : task := mk_task [0]%nat [] (fun _ _ => 1).
Definition writes_2 : task := mk_task [0]%nat [] (fun _ _ => 2).

Definition zero : state := fun _ => 0.

Example race_order_A : seq_run [writes_1; writes_2] zero 0%nat = 2.
Proof. reflexivity. Qed.

Example race_order_B : seq_run [writes_2; writes_1] zero 0%nat = 1.
Proof. reflexivity. Qed.

(* ★ 정리 — 쓰기 집합이 겹치면 **순서가 결과를 바꾼다.** 그래서 스케줄러가 자유로우면
   결과가 결정되지 않는다. DET-1 의 disjoint 조건은 장식이 아니라 **필수**다. *)
Theorem overlap_is_nondeterministic :
  ~ indep writes_1 writes_2 /\
  seq_run [writes_1; writes_2] zero 0%nat <> seq_run [writes_2; writes_1] zero 0%nat.
Proof.
  split.
  - intros [Hww _]. apply (Hww 0%nat); simpl; left; reflexivity.
  - rewrite race_order_A, race_order_B. discriminate.
Qed.

(* ── 5. ★★ DET-2 — 의존이 있는 쌍만 순서를 지키면 된다 (Bernstein) ────────── *)
(* 인접한 **독립** 태스크는 맞바꿔도 결과가 같다. 그래서 스케줄러의 자유도는
   정확히 "충돌하지 않는 쌍의 순서" 만큼이다 — 그 이상도 이하도 아니다. *)

Lemma swap_ok : forall t1 t2 s,
  local t1 -> local t2 -> indep t1 t2 ->
  forall i, apply t2 (apply t1 s) i = apply t1 (apply t2 s) i.
Proof.
  intros t1 t2 s Hl1 Hl2 [Hww [Hw1r2 Hr1w2]] i.
  unfold apply.
  destruct (inb i (wr t2)) eqn:E2; destruct (inb i (wr t1)) eqn:E1.
  - (* 둘 다 i 를 쓴다 — 독립성에 어긋난다 *)
    exfalso. apply (Hww i (inb_true _ _ E1) (inb_true _ _ E2)).
  - (* t2 만 쓴다 — t2 의 계산은 t1 의 쓰기를 보지 않는다 *)
    apply (Hl2 (apply t1 s) s).
    intros j Hj. unfold apply.
    destruct (inb j (wr t1)) eqn:Ej; [ | reflexivity ].
    exfalso. apply (Hw1r2 j (inb_true _ _ Ej) Hj).
  - (* t1 만 쓴다 *)
    symmetry. apply (Hl1 (apply t2 s) s).
    intros j Hj. unfold apply.
    destruct (inb j (wr t2)) eqn:Ej; [ | reflexivity ].
    exfalso. apply (Hr1w2 j Hj (inb_true _ _ Ej)).
  - (* 아무도 안 쓴다 *)
    reflexivity.
Qed.

(* 상태가 점마다 같으면 이후 실행도 점마다 같다 (지역성이 이것을 준다). *)
Lemma seq_run_congr : forall ts s s',
  Forall local ts -> (forall i, s i = s' i) ->
  forall i, seq_run ts s i = seq_run ts s' i.
Proof.
  induction ts as [| t ts IH]; intros s s' Hloc Heq i; [ apply Heq | ].
  inversion Hloc as [| t0 ts0 Hlt Hlts]; subst.
  rewrite !seq_run_cons.
  apply (IH (apply t s) (apply t s') Hlts).
  intros j. unfold apply.
  destruct (inb j (wr t)); [ apply (Hlt s s'); intros k _; apply Heq | apply Heq ].
Qed.

(* 인접 독립 교환 — 리스트 어디서든. *)
Lemma seq_run_swap : forall l1 t1 t2 l2 s,
  Forall local (l1 ++ t1 :: t2 :: l2) -> indep t1 t2 ->
  forall i, seq_run (l1 ++ t1 :: t2 :: l2) s i = seq_run (l1 ++ t2 :: t1 :: l2) s i.
Proof.
  induction l1 as [| a l1 IH]; intros t1 t2 l2 s Hloc Hind i.
  - cbn [app] in Hloc |- *.
    inversion Hloc as [| x xs Hl1 Hrest]; subst.
    inversion Hrest as [| y ys Hl2 Hl2s]; subst.
    rewrite !seq_run_cons.
    apply (seq_run_congr l2 (apply t2 (apply t1 s)) (apply t1 (apply t2 s)) Hl2s).
    intros j. apply (swap_ok t1 t2 s Hl1 Hl2 Hind).
  - cbn [app] in Hloc |- *.
    inversion Hloc as [| x xs Hla Hrest]; subst.
    rewrite !seq_run_cons.
    apply (IH t1 t2 l2 (apply a s) Hrest Hind).
Qed.

(* ★ 스케줄 자유도의 정확한 크기 — 독립 쌍의 교환으로 닿는 모든 순서는 **같은 결과**다. *)
Inductive sched_equiv : list task -> list task -> Prop :=
  | se_refl : forall l, sched_equiv l l
  | se_swap : forall l1 t1 t2 l2 r,
      indep t1 t2 ->
      sched_equiv (l1 ++ t2 :: t1 :: l2) r ->
      sched_equiv (l1 ++ t1 :: t2 :: l2) r.

Lemma Forall_swap : forall (P : task -> Prop) l1 t1 t2 l2,
  Forall P (l1 ++ t1 :: t2 :: l2) -> Forall P (l1 ++ t2 :: t1 :: l2).
Proof.
  intros P l1 t1 t2 l2 H.
  apply Forall_app in H as [Hl1 H2].
  inversion H2 as [| x xs Hx Hrest]; subst.
  inversion Hrest as [| y ys Hy Hl2]; subst.
  apply Forall_app. split; [ exact Hl1 | ].
  constructor; [ exact Hy | constructor; [ exact Hx | exact Hl2 ] ].
Qed.

(* ★★ DET-2 — **충돌하지 않는 쌍의 순서는 결과에 영향이 없다.**
   그러므로 스케줄러는 의존(=충돌) 간선만 지키면 되고, 나머지는 자유다.
   그것이 "의존 sync" 가 뜻해야 하는 전부다. *)
Theorem det2_schedule_free : forall ts ts',
  sched_equiv ts ts' -> Forall local ts ->
  forall s i, seq_run ts s i = seq_run ts' s i.
Proof.
  intros ts ts' H. induction H as [ l | l1 t1 t2 l2 r Hind Hse IH ]; intros Hloc s i.
  - reflexivity.
  - rewrite (seq_run_swap l1 t1 t2 l2 s Hloc Hind i).
    apply IH; [ apply Forall_swap; exact Hloc ].
Qed.

(* ── 6. ★★ DET-3 — reduction tree 는 **고정되어야** 한다 ────────────────────── *)
(* 왜? 축약 연산이 **결합적이지 않으면** 트리 모양이 결과를 바꾸기 때문이다.
   부동소수 덧셈이 바로 그렇다. 여기서는 IEEE 를 공리로 들여오지 않고,
   **비결합 연산의 대표**(정수 뺄셈)로 그 사실을 기계 검증한다 — 논지는 같다. *)

Inductive tree : Type :=
  | Leaf (z : Z)
  | Node (l r : tree).

Fixpoint red (op : Z -> Z -> Z) (t : tree) : Z :=
  match t with
  | Leaf z => z
  | Node l r => op (red op l) (red op r)
  end.

Fixpoint leaves (t : tree) : list Z :=
  match t with
  | Leaf z => [z]
  | Node l r => leaves l ++ leaves r
  end.

Definition assoc (op : Z -> Z -> Z) : Prop :=
  forall a b c, op (op a b) c = op a (op b c).

Lemma fold_assoc : forall op, assoc op ->
  forall l a b, fold_left op l (op a b) = op a (fold_left op l b).
Proof.
  intros op Ha l. induction l as [| x l IH]; intros a b; simpl; [ reflexivity | ].
  rewrite Ha. apply IH.
Qed.

(* 어떤 트리든 그 잎들 위의 왼쪽 접기와 같다 — **결합적일 때만**. *)
Lemma red_is_fold : forall op, assoc op ->
  forall t, exists z rest, leaves t = z :: rest /\ red op t = fold_left op rest z.
Proof.
  intros op Ha t. induction t as [ z | l IHl r IHr ]; simpl.
  - exists z, []. split; reflexivity.
  - destruct IHl as [zl [rl [Hl Hrl]]]. destruct IHr as [zr [rr [Hr Hrr]]].
    exists zl, (rl ++ zr :: rr).
    rewrite Hl, Hr. simpl. split; [ reflexivity | ].
    rewrite Hrl, Hrr.
    rewrite fold_left_app. simpl.
    symmetry. apply fold_assoc; exact Ha.
Qed.

(* ★ 결합적이면 트리 모양이 **상관없다** — 스케줄러가 트리를 골라도 된다. *)
Theorem assoc_shape_free : forall op, assoc op ->
  forall t1 t2, leaves t1 = leaves t2 -> red op t1 = red op t2.
Proof.
  intros op Ha t1 t2 Hlv.
  destruct (red_is_fold op Ha t1) as [z1 [r1 [H1 E1]]].
  destruct (red_is_fold op Ha t2) as [z2 [r2 [H2 E2]]].
  rewrite Hlv in H1. rewrite H1 in H2. inversion H2; subst z2 r2.
  rewrite E1, E2. reflexivity.
Qed.

(* ★★ 그리고 **결합적이지 않으면 모양이 결과를 바꾼다.** 이것이 DET-3 의 근거다.
   부동소수 덧셈은 결합적이지 않다 — 그러므로 축약 트리를 스케줄러가 고르게 두면
   **결과가 스케줄에 의존한다.** 언어가 트리를 고정해야 하는 이유가 이것이다. *)
Definition sub (a b : Z) : Z := a - b.       (* 비결합 연산의 대표 *)

Definition left_tree  : tree := Node (Node (Leaf 5) (Leaf 3)) (Leaf 1).   (* (5-3)-1 = 1 *)
Definition right_tree : tree := Node (Leaf 5) (Node (Leaf 3) (Leaf 1)).   (* 5-(3-1) = 3 *)

Example same_leaves : leaves left_tree = leaves right_tree.
Proof. reflexivity. Qed.

Theorem nonassoc_shape_matters :
  ~ assoc sub /\
  leaves left_tree = leaves right_tree /\
  red sub left_tree <> red sub right_tree.
Proof.
  split; [ | split ].
  - intros H. specialize (H 5 3 1). unfold sub in H. discriminate.
  - reflexivity.
  - unfold left_tree, right_tree, red, sub. simpl. discriminate.
Qed.

(* ⇒ DET-3 의 내용이 확정된다:
     · 축약 연산이 결합적이면(정수 add·min·max·bit_or …) 트리는 자유다.  ★ assoc_shape_free
     · 결합적이지 않으면(**부동소수 add·mul**) 트리를 **프로그램이 고정해야** 한다.
       스케줄러가 고르면 결과가 스케줄에 의존한다.                        ★ nonassoc_shape_matters
   RFC-0009 가 "고정 reduction tree" 라고만 부르던 것의 이유가 이것이다.
   그리고 이것은 RFC-0053 의 `sum`(Neumaier) vs `sum_fast` 구분과 같은 뿌리다 —
   부동소수의 결합 순서는 **의미의 일부**다. *)

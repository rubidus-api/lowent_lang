(* LowentOpt.v — ★★★ **효과 기반 최적화가 적법하다 — 값 층까지**. 순수 Coq.
 *
 * 16장이 이 줄에 별을 붙여 두었다:
 *
 *     | **효과 기반 최적화의 적법성**(순수 op 재정렬·CSE·memoize) |
 *       **절반** — 효과 층은 증명됨. **값 층은 미증명**(같은 값을 준다는 것은 별개) |
 *
 * `LowentEffect.v` 는 *"순수한 조각은 자리를 바꿔도·지워도 **일어나는 효과**가 같다"* 를
 * 증명했다. 그런데 최적화가 진짜로 기대는 것은 그것이 아니다:
 *
 *     **재정렬해도 같은 값을 낸다** · **두 번 부르지 않고 한 번 불러 재사용해도 같다**(CSE)
 *     **memoize 해도 같다** · **안 쓰는 순수 계산은 지워도 같다**(죽은 코드 제거)
 *
 * 이 파일은 **값과 상태를 가진 모델**에서 그 넷을 증명한다. 그러려면 "순수" 가
 * *"효과 원자를 내지 않는다"* 를 넘어 **상태를 안 만지고 상태에 안 기댄다**로 강해져야 하고,
 * 그 강해진 정의가 정확히 무엇을 요구하는지가 이 파일의 내용이다.
 *
 * ★★ 그리고 **경계를 반례로 못박는다**(§6): 효과가 있는 조각은 재정렬하면 **값이 달라진다.**
 *   즉 이 정리들의 전제(`pure`)는 장식이 아니다 — 빼면 거짓이 되는 것을 계산으로 보인다.
 *)

Require Import List Bool Arith Lia ZArith.
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 상태와 식 ────────────────────────────────────────────────────────── *)

(* 상태는 칸 번호 → 값. `io` 같은 바깥 효과는 **출력 열**로 본다(관찰 가능한 것). *)
Definition store := nat -> Z.
Definition upd (s : store) (l : nat) (z : Z) : store :=
  fun k => if Nat.eqb k l then z else s k.

Inductive exp : Type :=
  | ECst  (z : Z)
  | ELoad (l : nat)                 (* 상태를 **읽는다** *)
  | EAdd  (a b : exp)
  | EStore (l : nat) (a : exp)      (* 상태를 **쓴다** — 값은 쓴 값 *)
  | EPrint (a : exp).               (* 바깥으로 낸다 — 관찰된다 *)

(* 평가: 값 · 새 상태 · **출력 열**. 왼쪽부터 평가한다(구현의 순서와 같다). *)
Fixpoint eval (s : store) (e : exp) : Z * store * list Z :=
  match e with
  | ECst z => (z, s, [])
  | ELoad l => (s l, s, [])
  | EAdd a b =>
      let '(va, s1, o1) := eval s a in
      let '(vb, s2, o2) := eval s1 b in
      (va + vb, s2, o1 ++ o2)
  | EStore l a =>
      let '(va, s1, o1) := eval s a in
      (va, upd s1 l va, o1)
  | EPrint a =>
      let '(va, s1, o1) := eval s a in
      (va, s1, o1 ++ [va])
  end.

Definition val_of (s : store) (e : exp) : Z := let '(v, _, _) := eval s e in v.
Definition st_of  (s : store) (e : exp) : store := let '(_, s', _) := eval s e in s'.
Definition out_of (s : store) (e : exp) : list Z := let '(_, _, o) := eval s e in o.

(* ── 2. **순수**란 무엇인가 — 효과 층보다 강해야 한다 ───────────────────── *)

(* 구문적 순수: 쓰지도 않고, 내지도 않는다. (읽기는 허용한다 — 그것이 요점이다.) *)
Fixpoint writes (e : exp) : bool :=
  match e with
  | ECst _ => false
  | ELoad _ => false
  | EAdd a b => writes a || writes b
  | EStore _ _ => true
  | EPrint a => writes a
  end.

Fixpoint prints (e : exp) : bool :=
  match e with
  | ECst _ | ELoad _ => false
  | EAdd a b => prints a || prints b
  | EStore _ a => prints a
  | EPrint _ => true
  end.

Definition pure (e : exp) : bool := negb (writes e) && negb (prints e).

(* 읽는 칸들 — 재정렬이 안전한지 판정할 때 쓴다. *)
Fixpoint reads (e : exp) (l : nat) : bool :=
  match e with
  | ECst _ => false
  | ELoad k => Nat.eqb k l
  | EAdd a b => reads a l || reads b l
  | EStore _ a => reads a l
  | EPrint a => reads a l
  end.

Fixpoint written (e : exp) (l : nat) : bool :=
  match e with
  | ECst _ | ELoad _ => false
  | EAdd a b => written a l || written b l
  | EStore k a => Nat.eqb k l || written a l
  | EPrint a => written a l
  end.

(* ── 3. ★ 순수한 식은 상태를 바꾸지 않고, 아무것도 내지 않는다 ─────────── *)

Lemma pure_no_write : forall e s, writes e = false -> st_of s e = s.
Proof.
  induction e; intros s H; simpl in *; unfold st_of in *; simpl.
  - reflexivity.
  - reflexivity.
  - apply orb_false_iff in H as [Ha Hb].
    destruct (eval s e1) as [[va s1] o1] eqn:E1.
    destruct (eval s1 e2) as [[vb s2] o2] eqn:E2.
    specialize (IHe1 s Ha). unfold st_of in IHe1. rewrite E1 in IHe1. simpl in IHe1. subst s1.
    specialize (IHe2 s Hb). unfold st_of in IHe2. rewrite E2 in IHe2. simpl in IHe2. exact IHe2.
  - discriminate.
  - destruct (eval s e) as [[va s1] o1] eqn:E1.
    specialize (IHe s H). unfold st_of in IHe. rewrite E1 in IHe. simpl in IHe. exact IHe.
Qed.

Lemma pure_no_output : forall e s, prints e = false -> out_of s e = [].
Proof.
  induction e; intros s H; simpl in *; unfold out_of in *; simpl.
  - reflexivity.
  - reflexivity.
  - apply orb_false_iff in H as [Ha Hb].
    destruct (eval s e1) as [[va s1] o1] eqn:E1.
    destruct (eval s1 e2) as [[vb s2] o2] eqn:E2.
    specialize (IHe1 s Ha). unfold out_of in IHe1. rewrite E1 in IHe1. simpl in IHe1. subst o1.
    specialize (IHe2 s1 Hb). unfold out_of in IHe2. rewrite E2 in IHe2. simpl in IHe2. subst o2.
    reflexivity.
  - destruct (eval s e) as [[va s1] o1] eqn:E1.
    specialize (IHe s H). unfold out_of in IHe. rewrite E1 in IHe. simpl in IHe. exact IHe.
  - discriminate.
Qed.

(* ★ 값은 **읽는 칸에만** 달렸다 — 이것이 재정렬·CSE 를 떠받치는 진짜 보조정리다. *)
Lemma value_depends_only_on_reads : forall e s s',
  (forall l, reads e l = true -> s l = s' l) ->
  writes e = false ->
  val_of s e = val_of s' e.
Proof.
  induction e; intros s s' Hagree Hw; unfold val_of; simpl in *.
  - reflexivity.
  - apply Hagree. apply Nat.eqb_refl.
  - apply orb_false_iff in Hw as [Hw1 Hw2].
    destruct (eval s e1) as [[va s1] o1] eqn:E1.
    destruct (eval s1 e2) as [[vb s2] o2] eqn:E2.
    destruct (eval s' e1) as [[va' s1'] o1'] eqn:E1'.
    destruct (eval s1' e2) as [[vb' s2'] o2'] eqn:E2'.
    (* 왼쪽이 상태를 안 바꾸므로 s1 = s · s1' = s' *)
    assert (Hs1 : s1 = s).
    { pose proof (pure_no_write e1 s Hw1) as HH. unfold st_of in HH. rewrite E1 in HH.
      simpl in HH. exact HH. }
    assert (Hs1' : s1' = s').
    { pose proof (pure_no_write e1 s' Hw1) as HH. unfold st_of in HH. rewrite E1' in HH.
      simpl in HH. exact HH. }
    subst s1 s1'.
    assert (Hva : va = va').
    { specialize (IHe1 s s' (fun l HL => Hagree l (orb_true_intro _ _ (or_introl HL))) Hw1).
      unfold val_of in IHe1. rewrite E1, E1' in IHe1. simpl in IHe1. exact IHe1. }
    assert (Hvb : vb = vb').
    { specialize (IHe2 s s' (fun l HL => Hagree l (orb_true_intro _ _ (or_intror HL))) Hw2).
      unfold val_of in IHe2. rewrite E2, E2' in IHe2. simpl in IHe2. exact IHe2. }
    subst. reflexivity.
  - discriminate.
  - destruct (eval s e) as [[va s1] o1] eqn:E1.
    destruct (eval s' e) as [[va' s1'] o1'] eqn:E1'.
    specialize (IHe s s' Hagree Hw). unfold val_of in IHe.
    rewrite E1, E1' in IHe. simpl in IHe. exact IHe.
Qed.

(* ── 4. ★★★ 네 가지 최적화가 **값 층에서** 적법하다 ─────────────────────── *)

(* ① **재정렬** — 순수한 조각은 어디서 평가해도 같은 값을 준다.
      (여기서 "순수" 는 **상태를 안 바꾼다**는 뜻이고, 읽기는 허용된다.
       다른 조각이 그 칸에 **쓰지 않으면** 순서를 바꿔도 값이 같다.) *)
Theorem reorder_preserves_value : forall a b s,
  writes a = false ->
  (forall l, reads a l = true -> written b l = false) ->
  (* a 를 b 뒤로 미뤄도 a 의 값은 그대로다 *)
  val_of s a = val_of (st_of s b) a.
Proof.
  intros a b s Ha Hindep.
  apply value_depends_only_on_reads; [ | exact Ha ].
  intros l Hr.
  (* b 가 l 에 쓰지 않으므로 st_of s b 의 l 칸은 s 의 것과 같다 *)
  specialize (Hindep l Hr). clear Hr Ha.
  revert s. induction b; intros s; simpl in *; unfold st_of; simpl.
  - reflexivity.
  - reflexivity.
  - apply orb_false_iff in Hindep as [H1 H2].
    destruct (eval s b1) as [[va s1] o1] eqn:E1.
    destruct (eval s1 b2) as [[vb s2] o2] eqn:E2.
    specialize (IHb1 H1 s). unfold st_of in IHb1. rewrite E1 in IHb1. simpl in IHb1.
    specialize (IHb2 H2 s1). unfold st_of in IHb2. rewrite E2 in IHb2. simpl in IHb2.
    rewrite IHb1. exact IHb2.
  - apply orb_false_iff in Hindep as [Hne Hin].
    destruct (eval s b) as [[va s1] o1] eqn:E1.
    specialize (IHb Hin s). unfold st_of in IHb. rewrite E1 in IHb. simpl in IHb.
    unfold upd. rewrite Nat.eqb_sym in Hne. rewrite Hne. exact IHb.
  - destruct (eval s b) as [[va s1] o1] eqn:E1.
    specialize (IHb Hindep s). unfold st_of in IHb. rewrite E1 in IHb. simpl in IHb.
    exact IHb.
Qed.

(* ② **CSE / memoize** — 같은 순수 식을 두 번 평가한 것과, 한 번 평가해 재사용한 것이 같다.
      `add e e` 를 `let v = e in add v v` 로 바꾸는 것이 적법하다는 진술. *)
Theorem cse_is_legal : forall e s,
  pure e = true ->
  val_of s (EAdd e e) = val_of s e + val_of s e /\
  st_of s (EAdd e e) = s /\
  out_of s (EAdd e e) = [].
Proof.
  intros e s Hp. unfold pure in Hp. apply andb_true_iff in Hp as [Hw Ho].
  apply negb_true_iff in Hw. apply negb_true_iff in Ho.
  assert (Hs : st_of s e = s) by (apply pure_no_write; exact Hw).
  unfold val_of, st_of, out_of in *; simpl.
  destruct (eval s e) as [[v s1] o1] eqn:E1. simpl in Hs. subst s1.
  rewrite E1.
  assert (Ho1 : o1 = []).
  { pose proof (pure_no_output e s Ho) as HH. unfold out_of in HH. rewrite E1 in HH.
    simpl in HH. exact HH. }
  subst o1. repeat split; reflexivity.
Qed.

(* ③ **죽은 코드 제거** — 안 쓰는 순수 계산은 지워도 값·상태·출력이 전부 같다. *)
Theorem dropping_pure_preserves_everything : forall dead keep s,
  pure dead = true ->
  val_of s (EAdd (EStore 0 dead) keep) - val_of s dead = val_of (upd s 0 (val_of s dead)) keep /\
  out_of s dead = [].
Proof.
  intros dead keep s Hp. unfold pure in Hp. apply andb_true_iff in Hp as [Hw Ho].
  apply negb_true_iff in Hw. apply negb_true_iff in Ho.
  assert (Hs : st_of s dead = s) by (apply pure_no_write; exact Hw).
  split.
  - unfold val_of, st_of in *; simpl.
    destruct (eval s dead) as [[v s1] o1] eqn:E1. simpl in Hs. subst s1.
    destruct (eval (upd s 0 v) keep) as [[vk sk] ok] eqn:E2. simpl. lia.
  - apply pure_no_output. exact Ho.
Qed.

(* ④ **한 번만 계산하기** — 순수한 식은 상태에 무관하게 **같은 값을 반복해서** 준다.
      memoize 가 기대는 바로 그 문장(같은 상태에서 두 번 부르면 같다). *)
Theorem pure_is_repeatable : forall e s,
  pure e = true ->
  val_of s e = val_of (st_of s e) e.
Proof.
  intros e s Hp. unfold pure in Hp. apply andb_true_iff in Hp as [Hw _].
  apply negb_true_iff in Hw.
  rewrite (pure_no_write e s Hw). reflexivity.
Qed.

(* ── 5. ★ 효과 층과 값 층을 잇는다 ──────────────────────────────────────── *)

(* 순수한 식은 **관찰되지 않는다**: 상태도 그대로, 출력도 없다.
   `LowentEffect.v` 의 `pure_means_pure`(효과 원자가 없다)의 **값 층 짝**이다. *)
Theorem pure_is_unobservable : forall e s,
  pure e = true -> st_of s e = s /\ out_of s e = [].
Proof.
  intros e s Hp. unfold pure in Hp. apply andb_true_iff in Hp as [Hw Ho].
  apply negb_true_iff in Hw. apply negb_true_iff in Ho.
  split; [ apply pure_no_write | apply pure_no_output ]; assumption.
Qed.

(* ── 6. ★★ 전제가 장식이 아니다 — 빼면 거짓이 되는 것을 계산한다 ───────── *)

Definition s0 : store := fun _ => 0.

(* `store 0 7` 과 `load 0` 을 뒤집으면 **값이 달라진다**. ⇒ 효과 있는 조각의 재정렬은 불법. *)
Theorem reordering_an_effectful_piece_changes_the_value :
  val_of s0 (EAdd (EStore 0 (ECst 7)) (ELoad 0)) = 14 /\
  val_of s0 (EAdd (ELoad 0) (EStore 0 (ECst 7))) = 7.
Proof. split; vm_compute; reflexivity. Qed.

(* `print` 를 지우면 **출력이 달라진다**(값은 같아도). ⇒ 죽은 코드 제거도 순수해야 한다. *)
Theorem dropping_an_effectful_piece_changes_the_output :
  out_of s0 (EAdd (EPrint (ECst 3)) (ECst 4)) = [3] /\
  out_of s0 (ECst 4) = [] /\
  val_of s0 (EAdd (EPrint (ECst 3)) (ECst 4)) = 7.
Proof. repeat split; vm_compute; reflexivity. Qed.

(* CSE 도 마찬가지다: `add (print 1) (print 1)` 을 한 번으로 줄이면 **출력이 준다.** *)
Theorem cse_on_an_effectful_piece_loses_an_output :
  out_of s0 (EAdd (EPrint (ECst 1)) (EPrint (ECst 1))) = [1; 1] /\
  out_of s0 (EPrint (ECst 1)) = [1].
Proof. split; vm_compute; reflexivity. Qed.

(* ── 7. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **호출이 없다.** 순수 op **호출**의 재정렬은 이 파일의 `pure` 를 op 경계로 올려야 하고,
 *   그것은 `LowentEffect.v` 의 선언 요약과 맞물린다. 두 파일을 잇는 정리는 아직 없다 —
 *   각각이 자기 층(효과 원자 / 값·상태·출력)을 본다.
 * · **재정렬의 조건이 구문적이다.** `reads a l -> written b l = false` 는 **보수적**이다.
 *   실제 컴파일러는 별칭 분석으로 더 잘한다(그리고 그 정밀도가 이 파일 밖이다).
 * · **비결정성이 없다.** 평가 순서가 하나로 고정돼 있다(왼→오). 순서가 미정인 언어라면
 *   같은 정리가 훨씬 어렵다 — 이 언어는 순서를 정했고, 그 결정이 이 증명을 싸게 만든다.
 * · **트랩·예외가 없다.** 넘침으로 멈추는 식의 재정렬은 *"멈추는 시점"* 이 관찰 가능해서
 *   따로 봐야 한다(`LowentType.v` 의 덫이 그 층이다).
 *)

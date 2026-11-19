(* LowentDeadlock.v — ★★★ **교착 자유**: 순서를 지키면 멈추지 않는다. 순수 Coq.
 *
 * 16장 동시성 표의 두 줄이 비어 있었다:
 *
 *     | 교착 자유(deadlock freedom) | **미증명** — 12장 ⑥ |
 *     | 공정성·굶주림 없음          | **미증명** |
 *
 * 이 파일이 첫 줄을 갚는다(둘째 줄은 `LowentFair.v`).
 *
 * ★ 무엇을 증명하나. 교착은 **순환 대기**다: T1 이 T2 가 쥔 잠금을 기다리고, T2 는 T1 이 쥔
 *   것을 기다린다. 고전적 처방은 하나다 — **모든 스레드가 잠금을 같은 순서로 잡는다.**
 *   그 처방이 **정말로 충분한지**가 이 파일의 정리다:
 *
 *       모든 스레드가 **오름차순으로만** 잠그면, 막힌 스레드가 있는 상태에서
 *       **언제나 진행할 수 있는 스레드가 하나는 있다.**
 *
 *   ⇒ 교착(전원이 동시에 막힘)은 **불가능하다.**
 *
 * ★★ 그리고 **처방이 필요하다**는 것도 증명한다(§5): 순서를 어기면 **실제로 교착이 난다** —
 *   두 스레드가 서로를 기다리는 구체적 상태를 계산으로 보인다. 규칙이 장식이 아님을
 *   보이는 것이 이 저장소의 방식이다(`LowentJoin.v`·`LowentLaunder.v` 와 같은 모양).
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 상태 ─────────────────────────────────────────────────────────────── *)

Definition lock := nat.
Definition tid  := nat.

(* 스레드 하나의 상태: **이미 쥔 것들**(잡은 역순으로 쌓인다)과 **다음에 원하는 것**. *)
Record thread := mk_th {
  held : list lock;          (* head = 가장 최근에 잡은 것 *)
  want : option lock         (* None = 지금은 아무것도 기다리지 않는다(진행 가능) *)
}.

Definition state := list thread.        (* 스레드 목록 *)

(* 어떤 잠금이 **누군가에게 쥐여 있나** *)
Definition held_by_someone (S : state) (l : lock) : bool :=
  existsb (fun t => existsb (Nat.eqb l) (held t)) S.

(* 스레드가 **막혀 있다** = 원하는 것이 있고 그것을 남이 쥐고 있다. *)
Definition blocked (S : state) (t : thread) : bool :=
  match want t with
  | None => false
  | Some l => held_by_someone S l && negb (existsb (Nat.eqb l) (held t))
  end.

(* ── 2. 규율 — **오름차순 잠금** ─────────────────────────────────────────── *)

(* 규율은 한 줄이다: **원하는 것은 이미 쥔 것 전부보다 크다.**
   (그래서 "잠금을 늘 오름차순으로 잡는다" 가 된다.) *)
Definition ordered (t : thread) : bool :=
  match want t with
  | None => true
  | Some l => forallb (fun k => Nat.ltb k l) (held t)
  end.

Definition all_ordered (S : state) : bool := forallb ordered S.

Lemma ordered_want_gt : forall t l k,
  ordered t = true -> want t = Some l -> In k (held t) -> k < l.
Proof.
  intros t l k Ho Hw Hin. unfold ordered in Ho. rewrite Hw in Ho.
  rewrite forallb_forall in Ho. apply Nat.ltb_lt. exact (Ho k Hin).
Qed.

(* ── 3. ★ 기다림은 잠금 번호를 **키운다** ───────────────────────────────── *)

(* 막혔다는 것은 그 잠금을 **남이** 쥐고 있다는 뜻이다. 규율에 의해 그 남이 원하는 것은
   자기가 쥔 것보다 크므로 — **내가 기다리는 것보다 크다.**
   ⇒ 기다림을 따라가면 번호가 엄격히 증가한다. 유한하므로 **순환이 없다.** *)
Definition waits_for (S : state) (t u : thread) : Prop :=
  match want t with
  | Some l => In l (held u) /\ ~ In l (held t)
  | None => False
  end.

Lemma waiting_increases : forall S t u lt lu,
  ordered u = true ->
  want t = Some lt -> want u = Some lu ->
  waits_for S t u ->
  lt < lu.
Proof.
  intros S t u lt lu Hou Ht Hu Hw.
  unfold waits_for in Hw. rewrite Ht in Hw. destruct Hw as [Hin _].
  exact (ordered_want_gt u lu lt Hou Hu Hin).
Qed.

(* ── 4. ★★★ 교착 자유 ───────────────────────────────────────────────────── *)

(* 유한 목록에는 **최대 원소**가 있다 — 이 파일이 쓰는 유일한 산술 사실. *)
Lemma exists_max : forall (xs : list nat),
  xs <> [] -> exists x, In x xs /\ forall y, In y xs -> y <= x.
Proof.
  induction xs as [| a r IH]; intros Hne; [ congruence | ].
  destruct r as [| b r'].
  - exists a. split; [ left; reflexivity | ].
    intros y [Hy | Hy]; [ lia | contradiction ].
  - destruct (IH ltac:(discriminate)) as [x [Hin Hmax]].
    destruct (Nat.le_ge_cases a x) as [Hle | Hge].
    + exists x. split; [ right; exact Hin | ].
      intros y [Hy | Hy]; [ lia | exact (Hmax y Hy) ].
    + exists a. split; [ left; reflexivity | ].
      intros y [Hy | Hy]; [ lia | ].
      pose proof (Hmax y Hy). lia.
Qed.

(* 목록 술어가 전부 참은 아니면 — 거짓인 원소가 **있다**. *)
Lemma forallb_false_exists : forall (A : Type) (f : A -> bool) (xs : list A),
  forallb f xs = false -> exists x, In x xs /\ f x = false.
Proof.
  induction xs as [| a r IH]; simpl; [ discriminate | ].
  destruct (f a) eqn:Ea; simpl; intros H.
  - destruct (IH H) as [x [Hin Hf]]. exists x. split; [ right; exact Hin | exact Hf ].
  - exists a. split; [ left; reflexivity | exact Ea ].
Qed.

(* ★★★ **교착 자유**: 규율을 지키는 상태에는 **진행할 수 있는 스레드가 반드시 있다.**

   증명: 전원이 막혔다고 하자. 그러면 모두 무언가를 원한다. **가장 큰 것을 원하는** 스레드
   t 를 보라. t 가 막혔으므로 그 잠금 lt 를 쥔 남 u 가 있고, u 도 막혔으므로 무언가를
   원하는데 — 규율에 의해 그것은 lt 보다 **크다.** lt 가 최대였다는 데 모순이다. *)
Theorem no_deadlock : forall S,
  all_ordered S = true ->
  S <> [] ->
  exists t, In t S /\ blocked S t = false.
Proof.
  intros S Hord Hne.
  destruct (forallb (fun t => blocked S t) S) eqn:Eall.
  - (* 전원이 막혔다 — 모순을 끌어낸다 *)
    exfalso. rewrite forallb_forall in Eall.
    (* 원하는 잠금들의 목록 *)
    remember (map (fun t => match want t with Some l => l | None => 0 end) S) as ws eqn:Ews.
    assert (Hwne : ws <> []).
    { subst ws. destruct S; [ congruence | discriminate ]. }
    destruct (exists_max ws Hwne) as [w [Hwin Hwmax]].
    subst ws. apply in_map_iff in Hwin as [t [Htw Htin]].
    (* t 는 막혀 있다 ⇒ want t = Some lt 이고 lt = w *)
    pose proof (Eall t Htin) as Hb. unfold blocked in Hb.
    destruct (want t) as [lt | ] eqn:Ht; [ | discriminate ].
    simpl in Htw. subst w.
    apply andb_true_iff in Hb as [Hheld _].
    unfold held_by_someone in Hheld. apply existsb_exists in Hheld as [u [Huin Huh]].
    apply existsb_exists in Huh as [k [Hkin Hkeq]]. apply Nat.eqb_eq in Hkeq. subst k.
    (* u 도 막혔다 ⇒ 무언가를 원한다 *)
    pose proof (Eall u Huin) as Hbu. unfold blocked in Hbu.
    destruct (want u) as [lu | ] eqn:Hu; [ | discriminate ].
    (* 규율: u 가 쥔 lt 는 u 가 원하는 lu 보다 작다 *)
    assert (Hou : ordered u = true).
    { unfold all_ordered in Hord. rewrite forallb_forall in Hord. exact (Hord u Huin). }
    assert (Hlt : lt < lu) by exact (ordered_want_gt u lu lt Hou Hu Hkin).
    (* 그런데 lt 가 최대였다 *)
    assert (Hle : lu <= lt).
    { apply Hwmax. apply in_map_iff. exists u. rewrite Hu. split; [ reflexivity | exact Huin ]. }
    lia.
  - (* 전원이 막힌 것이 아니다 — 막히지 않은 스레드를 꺼낸다 *)
    exact (forallb_false_exists thread (fun t => blocked S t) S Eall).
Qed.

(* ── 5. ★★ 규율이 **필요하다** — 어기면 실제로 교착이 난다 ──────────────── *)

(* T1: lock A(0) 쥐고 B(1) 을 원한다 — 오름차순 ✓
   T2: lock B(1) 쥐고 A(0) 을 원한다 — **내림차순 ✘** *)
Definition t1 : thread := mk_th [0] (Some 1).
Definition t2_bad : thread := mk_th [1] (Some 0).
Definition deadlocked : state := [t1; t2_bad].

Theorem violating_the_order_deadlocks :
  all_ordered deadlocked = false /\                       (* 규율을 어겼고 *)
  forallb (fun t => blocked deadlocked t) deadlocked = true. (* 전원이 막혔다 *)
Proof. split; vm_compute; reflexivity. Qed.

(* ★ 같은 두 스레드가 **순서를 지키면** 교착이 없다 — 규율이 실제로 그 상태를 배제한다. *)
Definition t2_good : thread := mk_th [] (Some 0).
Definition healthy : state := [t1; t2_good].

Theorem ordered_state_has_a_runnable_thread :
  all_ordered healthy = true /\
  existsb (fun t => negb (blocked healthy t)) healthy = true.
Proof. split; vm_compute; reflexivity. Qed.

(* ── 6. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **잠금을 잡는 동작(전이)이 없다.** 이 파일은 **상태의 성질**을 본다: 규율을 지키는
 *   상태에는 진행 가능한 스레드가 있다. *"규율을 지키며 실행하면 상태가 계속 규율을
 *   지킨다"* 는 별도의 보존 정리이고, 여기 없다(§7 후속).
 * · **구현이 이 규율을 강제하지 않는다.** 지금 이 언어는 잠금 순서를 **검사하지 않는다.**
 *   그래서 이 정리는 *"이렇게 쓰면 안전하다"* 이지 *"컴파일러가 막아 준다"* 가 아니다.
 *   ⇒ 12장·16장에 그 차이를 적는다. (게이트로 만들려면 잠금에 **정적 순서**를 붙여야 한다.)
 * · **재진입·조건변수·타임아웃이 없다.** 실무의 교착 상당수가 그쪽에서 온다.
 * · **잠금이 아닌 자원**(채널 만석·스레드 조인)의 순환 대기는 다루지 않는다.
 *)

(* LowentDRF.v — R7 의 심장: **안전 코드에는 데이터 경합이 없다** (DRF ⇒ DRF-SC).
 *
 * RFC-0018 §8-1 이 남긴 후속:
 *   "안전 코드 race-free ⟹ DRF-SC 는 RFC-0009 §6.5a DRF1~4 로 **스케치**.
 *    기계 증명은 iGPS/Cosmo 로 V3."
 *
 * ★ 그런데 이 명제는 **RC11 도 Iris 도 필요 없다.**
 *   "안전 코드에 경합이 없다" 는 메모리 모델에 대한 주장이 아니라 **규율에 대한 주장**이다:
 *     level 1 — disjoint split : 두 태스크가 같은 자리를 건드리지 않는다
 *     level 2 — actor 격리     : 한 자리는 한 액터만 소유하고, 이전은 메시지로만 일어난다
 *   둘 다 순수 Coq 으로 증명된다. 그리고 이것이 증명되면 **DRF-SC 가 발동해서**
 *   level 1·2 코드는 메모리 모델을 아예 볼 일이 없다 — RFC-0018 §6.5 의 주장 그대로다.
 *
 * ★ 정직하게: RC11 의 *내부*(5 ordering·hb 폐포)는 여기서 증명하지 않는다. 그것은 level-3
 *   전용이고 iGPS/Cosmo 가 필요하다(RFC-0018 §8-1 이 옳다). 여기서 증명하는 것은
 *   **level 3 로 내려가지 않아도 되는 이유** — 즉 R7 이 왜 "가둠"(containment)인가 다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

Definition tid := nat.      (* 스레드/액터 *)
Definition loc := nat.      (* 메모리 자리 *)

(* 데이터 경합의 정의(RC11 과 같다): 서로 다른 스레드가 **같은 자리**를 건드리고,
   적어도 하나가 **쓰기**이며, 둘 사이에 **happens-before 가 없다.** *)

(* ── 1. level 1 — disjoint split 이면 경합이 없다 ─────────────────────────── *)

Record task := mk_task { wr : list loc; rd : list loc }.

Definition disj (a b : list loc) : Prop := forall i, In i a -> In i b -> False.

(* Bernstein 조건 — LowentPar.v 의 indep 과 같은 것. *)
Definition indep (t1 t2 : task) : Prop :=
  disj (wr t1) (wr t2) /\ disj (wr t1) (rd t2) /\ disj (rd t1) (wr t2).

(* 한 태스크가 건드리는 자리 = 읽기 ∪ 쓰기. *)
Definition touches (t : task) (l : loc) : Prop := In l (wr t) \/ In l (rd t).

(* 경합 = 같은 자리 · 서로 다른 태스크 · 적어도 하나가 쓰기.
   (level 1 태스크들 사이에는 hb 간선이 없다 — fork 이후 join 전까지 병렬이다.
    그래서 hb 조건은 자동으로 '없음' 이고, 남는 것은 이 셋뿐이다.) *)
Definition races (t1 t2 : task) (l : loc) : Prop :=
  touches t1 l /\ touches t2 l /\ (In l (wr t1) \/ In l (wr t2)).

(* ★★ 정리 — **Bernstein 독립이면 어떤 자리에서도 경합하지 않는다.**
   RFC-0018 §6.5 "level 1 은 메모리 모델이 안 보인다" 의 근거가 이것이다. *)
Theorem l1_no_race : forall t1 t2 l,
  indep t1 t2 -> ~ races t1 t2 l.
Proof.
  intros t1 t2 l [Hww [Hw1r2 Hr1w2]] [Ht1 [Ht2 Hw]].
  destruct Hw as [Hw1 | Hw2].
  - (* t1 이 l 에 쓴다 — t2 는 l 을 읽지도 쓰지도 못한다 *)
    destruct Ht2 as [H2 | H2].
    + apply (Hww l Hw1 H2).
    + apply (Hw1r2 l Hw1 H2).
  - (* t2 가 l 에 쓴다 — 대칭 *)
    destruct Ht1 as [H1 | H1].
    + apply (Hww l H1 Hw2).
    + apply (Hr1w2 l H1 Hw2).
Qed.

(* ★ 그리고 이 조건이 **필요**하다 — 없으면 경합이 실제로 있다. *)
Definition writer : task := mk_task [0]%nat [].
Definition reader : task := mk_task [] [0]%nat.

Theorem l1_condition_is_necessary :
  ~ indep writer reader /\ races writer reader 0%nat.
Proof.
  split.
  - intros [_ [Hw1r2 _]]. apply (Hw1r2 0%nat); simpl; left; reflexivity.
  - repeat split; simpl; [ left; left; reflexivity | right; left; reflexivity
                         | left; left; reflexivity ].
Qed.

(* ── 2. level 2 — actor 격리 + 메시지 hb 이면 경합이 없다 ──────────────────── *)
(* 모델: 한 자리는 **정확히 한 액터**가 소유한다. 접근은 소유자만 한다.
   소유권은 **메시지로만** 옮겨지고, 메시지는 happens-before 간선이다(RFC-0009 MHB).
   ⇒ 서로 다른 액터의 두 접근 사이에는 **반드시 메시지가 있다** — 즉 hb 로 순서지어진다.
     그것이 곧 "경합이 아니다" 의 정의다. *)

Inductive ev : Type :=
  | Access (t : tid) (l : loc) (write : bool)      (* 소유자의 접근 *)
  | Send   (from to : tid) (l : loc).              (* 소유권 이전 = hb 간선 *)

Definition omap := loc -> tid.

Definition upd (o : omap) (l : loc) (t : tid) : omap :=
  fun l' => if Nat.eqb l' l then t else o l'.

Fixpoint owner (o : omap) (tr : list ev) : omap :=
  match tr with
  | [] => o
  | Access _ _ _ :: r => owner o r                  (* 접근은 소유권을 바꾸지 않는다 *)
  | Send _ to l :: r  => owner (upd o l to) r       (* ★ 이전은 메시지로만 *)
  end.

(* 적형 — **소유자만 접근하고, 소유자만 보낸다.** 이것이 level-2 규율의 전부다. *)
Fixpoint wf (o : omap) (tr : list ev) : Prop :=
  match tr with
  | [] => True
  | Access t l _ :: r  => o l = t /\ wf o r
  | Send from to l :: r => o l = from /\ wf (upd o l to) r
  end.

Lemma owner_app : forall a b o, owner o (a ++ b) = owner (owner o a) b.
Proof.
  induction a as [| e a IH]; intros b o; simpl; [ reflexivity | ].
  destruct e; apply IH.
Qed.

Lemma wf_app : forall a b o, wf o (a ++ b) -> wf o a /\ wf (owner o a) b.
Proof.
  induction a as [| e a IH]; intros b o H; simpl in *; [ split; [ exact I | exact H ] | ].
  destruct e as [t l w | from to l].
  - destruct H as [Ho H]. apply IH in H as [Ha Hb].
    split; [ split; [ exact Ho | exact Ha ] | exact Hb ].
  - destruct H as [Ho H]. apply IH in H as [Ha Hb].
    split; [ split; [ exact Ho | exact Ha ] | exact Hb ].
Qed.

(* 자리 l 의 소유권을 옮기는 메시지가 흔적 안에 있는가. *)
Definition has_send (l : loc) (tr : list ev) : bool :=
  existsb (fun e => match e with Send _ _ l' => Nat.eqb l' l | _ => false end) tr.

(* ★ 열쇠 — **메시지가 없으면 소유권은 바뀌지 않는다.** 접근은 소유권을 옮기지 못한다. *)
Lemma owner_stable : forall tr o l,
  has_send l tr = false -> owner o tr l = o l.
Proof.
  induction tr as [| e tr IH]; intros o l H; simpl; [ reflexivity | ].
  destruct e as [t l' w | from to l'].
  - apply IH. simpl in H. exact H.
  - simpl in H. apply orb_false_iff in H as [Hl Htr].
    rewrite (IH (upd o l' to) l Htr).
    unfold upd. rewrite Nat.eqb_sym. rewrite Hl. reflexivity.
Qed.

(* ★★ 정리 — level 2 의 **경합 없음**.
   서로 다른 액터가 같은 자리를 접근하면, **그 사이에 반드시 소유권 이전(메시지)이 있다.**
   메시지는 happens-before 간선이므로(RFC-0009 MHB), 두 접근은 hb 로 순서지어진다 —
   그리고 hb 로 순서지어진 두 접근은 **정의상 경합이 아니다.** *)
Theorem l2_accesses_are_separated_by_a_message :
  forall o tr1 t1 w1 tr2 t2 w2 tr3 l,
    wf o (tr1 ++ Access t1 l w1 :: tr2 ++ Access t2 l w2 :: tr3) ->
    t1 <> t2 ->
    has_send l tr2 = true.
Proof.
  intros o tr1 t1 w1 tr2 t2 w2 tr3 l Hwf Hne.
  apply wf_app in Hwf as [_ H1].
  simpl in H1. destruct H1 as [Ho1 H2].
  set (o1 := owner o tr1) in *.
  apply wf_app in H2 as [_ H3].
  simpl in H3. destruct H3 as [Ho2 _].
  destruct (has_send l tr2) eqn:E; [ reflexivity | ].
  exfalso. apply Hne.
  rewrite (owner_stable tr2 o1 l E) in Ho2.
  rewrite Ho1 in Ho2. exact Ho2.
Qed.

(* 따름 — 규율을 어기면 **바로 경합이다.** 소유권 이전 없이 두 액터가 같은 자리를 만지는
   흔적은 적형이 아니다. 규율이 장식이 아님을 보인다. *)
Definition o0 : omap := fun _ => 1%nat.

Example unsynchronised_sharing_is_not_wf :
  ~ wf o0 [Access 1 0 true; Access 2 0 true]%nat.
Proof.
  simpl. intros [_ [H _]]. unfold o0 in H. discriminate.
Qed.

(* 그리고 **메시지를 끼우면** 적형이다 — 소유권이 넘어갔으므로. *)
Example ownership_transfer_is_wf :
  wf o0 [Access 1 0 true; Send 1 2 0; Access 2 0 true]%nat.
Proof.
  simpl. repeat split; reflexivity.
Qed.

(* ── 3. 그래서 R7 은 "가둠"(containment)이다 ─────────────────────────────── *)
(* level 1 — l1_no_race                       ⇒ 경합 없음 ⇒ DRF-SC ⇒ 메모리 모델이 **안 보인다**
   level 2 — l2_accesses_are_separated…       ⇒ 경합 없음 ⇒ DRF-SC ⇒ ordering 이 **안 보인다**
   level 3 — 여기서만 RC11 ordering 이 노출된다. race = UB 는 프로그래머·감사 책임(G3).

   RFC-0018 §6.5 의 "동시성 복잡도 pay-as-you-go" 가 이제 **정리 위에 선다**:
   메모리 모델을 만나려면 level 3 으로 **내려가야만** 한다. 그 위에서는 만날 수가 없다 —
   경합이 없기 때문이다.

   ★ 남는 것(정직하게): RC11 의 **내부**(5 ordering 의 hb 폐포·SC 축) 는 여기서 증명하지
     않았다. 그것은 level-3 전용이고, 약한 ordering 을 다루려면 iGPS/Cosmo 같은 RC11 분리논리가
     필요하다(RFC-0018 §8-1 이 옳다). 기본값 seq_cst 로 쓰는 level-3 코드는 SC 모델
     (Iris heap_lang)에서 다룰 수 있고, 그것이 LowentLock.v 다. *)

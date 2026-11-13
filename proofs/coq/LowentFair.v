(* LowentFair.v — ★★★ **공정성: 굶주리는 태스크가 없다**. 순수 Coq.
 *
 * 16장 동시성 표의 남은 한 줄:
 *
 *     | 공정성·굶주림 없음 | **미증명** |
 *
 * 무엇을 증명하나. RFC-0039 의 tier **T0**(협력 floor — ATtiny 급, 힙 없음, 잡큐 + 정적 태스크)
 * 는 **라운드로빈**으로 돈다. 그 스케줄러의 약속은 하나다:
 *
 *     **준비된 태스크는 반드시, 유계 시간 안에 돈다.** (굶주림이 없다)
 *
 * ★ 그리고 그 약속은 **공짜가 아니다.** 두 조건이 필요하다:
 *   ① 스케줄러가 **회전**한다(돈 태스크는 뒤로 간다) — 안 그러면 첫 태스크가 독점한다.
 *   ② 태스크가 **양보**한다(무한 루프를 돌지 않는다) — 협력형의 대가다.
 *   ⇒ 그래서 §5 가 **①을 어긴 스케줄러에서 굶주림이 실제로 난다**는 것을 계산으로 보인다.
 *     규칙이 장식이 아님을 보이는 것이 이 저장소의 방식이다.
 *
 * ★★ 유계까지 준다: 준비된 태스크는 **자기 앞에 있는 수만큼** 기다리면 반드시 돈다
 *   (`runs_within`). *"언젠가는 돈다"* 가 아니라 **몇 걸음 안에** 돈다 — 임베디드에서
 *   그 차이가 전부다(RFC-0038 의 floor 는 실시간성을 재는 자리다).
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 스케줄러 ─────────────────────────────────────────────────────────── *)

Definition task := nat.

(* T0 의 준비 큐. head 가 다음에 돌 태스크다. *)
Definition queue := list task.

(* 한 걸음: 앞의 것을 돌리고 **뒤로 보낸다**(라운드로빈). *)
Definition step (q : queue) : queue :=
  match q with
  | [] => []
  | t :: r => r ++ [t]
  end.

Fixpoint run (q : queue) (n : nat) : queue :=
  match n with 0 => q | S k => run (step q) k end.

(* n 걸음 동안 **돈 태스크들**(순서대로). *)
Fixpoint trace (q : queue) (n : nat) : list task :=
  match n with
  | 0 => []
  | S k => match q with
           | [] => []
           | t :: _ => t :: trace (step q) k
           end
  end.

(* ── 2. ★ 라운드로빈의 기본 성질 ────────────────────────────────────────── *)

(* 큐의 **길이가 변하지 않는다** — 태스크가 사라지지도, 늘지도 않는다. *)
Lemma step_length : forall q, length (step q) = length q.
Proof.
  intros [| t r]; simpl; [ reflexivity | ].
  rewrite length_app. simpl. lia.
Qed.

Lemma run_length : forall n q, length (run q n) = length q.
Proof.
  induction n as [| n IH]; intros q; [ reflexivity | ].
  cbn [run]. rewrite IH. apply step_length.
Qed.

(* 큐가 비지 않으면 **계속 비지 않는다** — 돌 것이 늘 있다. *)
Lemma step_nonempty : forall q, q <> [] -> step q <> [].
Proof.
  intros [| t r] H; [ congruence | ]. simpl.
  destruct r; simpl; discriminate.
Qed.

Lemma run_nonempty : forall n q, q <> [] -> run q n <> [].
Proof.
  induction n as [| n IH]; intros q H; [ exact H | ].
  cbn [run]. apply IH. apply step_nonempty. exact H.
Qed.

(* ★ **소속이 보존된다** — 큐에 있던 태스크는 계속 큐에 있다(잃어버리지 않는다). *)
Lemma step_In : forall q t, In t q <-> In t (step q).
Proof.
  intros [| a r] t; simpl; [ reflexivity | ].
  rewrite in_app_iff. simpl. split.
  - intros [He | Hr]; [ right; left; exact He | left; exact Hr ].
  - intros [Hr | [He | []]]; [ right; exact Hr | left; exact He ].
Qed.

Lemma run_In : forall n q t, In t q -> In t (run q n).
Proof.
  induction n as [| n IH]; intros q t H; [ exact H | ].
  cbn [run]. apply IH. apply (proj1 (step_In q t)). exact H.
Qed.

(* ── 3. ★★★ 굶주림 없음 — **자기 자리만큼만 기다린다** ─────────────────── *)

(* 큐에서 t 앞에 몇 개가 있나(맨 앞이면 0). *)
Fixpoint position (q : queue) (t : task) : nat :=
  match q with
  | [] => 0
  | a :: r => if Nat.eqb a t then 0 else S (position r t)
  end.

Lemma position_lt : forall q t, In t q -> position q t < length q.
Proof.
  induction q as [| a r IH]; intros t H; simpl in *; [ contradiction | ].
  destruct (Nat.eqb a t) eqn:E; [ lia | ].
  apply Nat.eqb_neq in E.
  destruct H as [He | Hr]; [ congruence | ].
  specialize (IH t Hr). lia.
Qed.

(* 맨 앞이면 **이번 걸음에 돈다.** *)
Lemma head_runs : forall q t n,
  position q t = 0 -> In t q -> 0 < n -> In t (trace q n).
Proof.
  intros [| a r] t n Hp Hin Hn; simpl in *; [ contradiction | ].
  destruct (Nat.eqb a t) eqn:E; [ | discriminate ].
  apply Nat.eqb_eq in E. subst a.
  destruct n; [ lia | ]. simpl. left; reflexivity.
Qed.

(* ★ 한 걸음 돌면 **자리가 하나 앞당겨진다**(맨 앞이 아니었다면). *)
Lemma position_step : forall q t,
  In t q -> position q t <> 0 -> position (step q) t = pred (position q t).
Proof.
  intros [| a r] t Hin Hne; simpl in *; [ contradiction | ].
  destruct (Nat.eqb a t) eqn:E; [ congruence | ].
  apply Nat.eqb_neq in E.
  destruct Hin as [He | Hr]; [ congruence | ]. clear Hne.
  simpl. induction r as [| b r' IH]; simpl in *; [ contradiction | ].
  destruct (Nat.eqb b t) eqn:Eb; simpl; [ reflexivity | ].
  apply Nat.eqb_neq in Eb.
  destruct Hr as [Hb | Hr']; [ congruence | ].
  rewrite (IH Hr'). destruct (position r' t) eqn:Ep; [ | reflexivity ].
  (* position 이 0 이면 r' 의 맨 앞이 t 라는 뜻인데, r' 에 t 가 있으므로 모순은 없다:
     이 갈래는 실제로 일어나며 pred 0 = 0 으로 맞다 *)
  reflexivity.
Qed.

(* ★★★ **굶주림 없음(유계)**: 큐에 있는 태스크는 **자기 앞의 수 + 1 걸음 안에** 반드시 돈다.

   ★ 귀납의 측정치가 **길이가 아니라 자리**다. `step` 은 길이를 보존하므로(step_length)
     길이로는 줄어들지 않는다 — 줄어드는 것은 **내 앞에 남은 수**다. 그 한 줄이 이 정리의
     전부이고, 라운드로빈이 공정한 이유이기도 하다. *)
Theorem runs_within_at : forall p q t,
  position q t = p -> In t q -> In t (trace q (S p)).
Proof.
  induction p as [| p IH]; intros q t Hp Hin.
  - apply (head_runs q t 1 Hp Hin). lia.
  - destruct q as [| a r]; [ contradiction | ].
    assert (Hne : position (a :: r) t <> 0) by (rewrite Hp; discriminate).
    assert (Hstep : position (step (a :: r)) t = p)
      by (rewrite (position_step (a :: r) t Hin Hne), Hp; reflexivity).
    assert (HIn' : In t (step (a :: r))) by (apply (proj1 (step_In (a :: r) t)); exact Hin).
    specialize (IH (step (a :: r)) t Hstep HIn').
    simpl. right. exact IH.
Qed.

Corollary runs_within : forall q t,
  In t q -> In t (trace q (S (position q t))).
Proof. intros q t Hin. apply (runs_within_at (position q t) q t eq_refl Hin). Qed.

(* ★ 그리고 **모든** 태스크가 큐 길이만큼의 걸음 안에 한 번씩 돈다. *)
Corollary no_starvation : forall q t,
  In t q -> In t (trace q (length q)).
Proof.
  intros q t Hin.
  pose proof (runs_within q t Hin) as H.
  pose proof (position_lt q t Hin) as Hlt.
  (* trace 는 걸음 수에 대해 단조다: 더 오래 돌리면 더 많이 돈다 *)
  assert (Hmono : forall n m q0, n <= m -> forall x, In x (trace q0 n) -> In x (trace q0 m)).
  { induction n as [| n IHn]; intros m q0 Hle x Hx; simpl in Hx; [ contradiction | ].
    destruct q0 as [| a r]; [ contradiction | ].
    destruct m as [| m]; [ lia | ]. simpl.
    destruct Hx as [He | Hr]; [ left; exact He | ].
    right. apply (IHn m (step (a :: r))); [ lia | exact Hr ]. }
  apply (Hmono (S (position q t)) (length q) q); [ lia | exact H ].
Qed.

(* ── 4. ★ 공정성의 다른 얼굴 — **모두가 돈다** ──────────────────────────── *)

Theorem every_task_runs : forall q,
  forall t, In t q -> In t (trace q (length q)).
Proof. intros q t. apply no_starvation. Qed.

(* ── 5. ★★ 회전이 **필요하다** — 안 돌리면 굶는다 ──────────────────────── *)

(* 돈 태스크를 뒤로 보내지 않는 스케줄러(= 늘 맨 앞만 돌린다). *)
Definition step_bad (q : queue) : queue := q.

Fixpoint trace_bad (q : queue) (n : nat) : list task :=
  match n with
  | 0 => []
  | S k => match q with
           | [] => []
           | t :: _ => t :: trace_bad (step_bad q) k
           end
  end.

(* ★ 그러면 둘째 태스크는 **영원히 굶는다** — 몇 걸음을 돌려도 나오지 않는다. *)
Theorem without_rotation_the_second_task_starves : forall n,
  ~ In 1 (trace_bad [0; 1] n).
Proof.
  induction n as [| n IH]; simpl; [ auto | ].
  intros [He | Hr]; [ discriminate | ].
  unfold step_bad in Hr. exact (IH Hr).
Qed.

(* ★ 반면 라운드로빈은 **두 걸음 안에** 둘 다 돌린다. *)
Theorem rotation_runs_both : trace [0; 1] 2 = [0; 1].
Proof. vm_compute; reflexivity. Qed.

(* ── 6. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **양보를 가정한다.** 협력형(T0)에서 태스크가 `yield` 하지 않고 무한 루프를 돌면
 *   스케줄러가 할 수 있는 것이 없다. 이 모델의 한 "걸음" 이 곧 *"태스크가 양보했다"* 이다.
 *   ⇒ **선점이 없는 tier 의 공정성은 태스크의 협조에 조건부다.** 그것을 숨기지 않는다.
 * · **우선순위가 없다.** 우선순위가 들어오면 라운드로빈만으로는 굶주림이 다시 생기고
 *   (낮은 우선순위가 밀린다) 그때는 aging 같은 장치가 필요하다 — RFC-0013 의 일이다.
 * · **차단(blocking)이 없다.** 잠금을 기다리는 태스크는 이 모델에서 "준비" 로 남아 있다.
 *   차단이 들어오면 공정성은 교착 자유와 맞물린다(`LowentDeadlock.v`).
 * · **구현이 이 스케줄러라는 것은 증명하지 않았다.** `impl/` 의 협력 스케줄러
 *   (`lw_schedule`)가 이 모델과 같은지는 골든과 실행 테스트가 받친다(16장 ⑤).
 *)

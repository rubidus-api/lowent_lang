(* LowentEffect.v — ★★★ **효과 체계의 건전성**: 선언이 실제를 덮는다. 순수 Coq.
 *
 * 무엇이 남아 있었나. 16장의 미증명 목록 첫 줄이 이것이었다:
 *
 *     | 효과 체계의 건전성(선언이 실제를 덮는다) | **미증명** — 08장 ⑥ |
 *     | **효과 기반 최적화의 적법성**            | **미증명** ★ 최적화가 이것에 기댄다 |
 *
 * 구현이 하는 검사는 한 줄로 적힌다(`impl/src/low_check.c` 의 `walk_effects`):
 *
 *     op 의 본문이 내는 효과 ⊆ op 이 **선언한** 효과
 *     — 이때 호출 지점에서는 **피호출자의 선언**을 쓴다(본문을 다시 걷지 않는다).
 *
 * ★ 그 "선언을 쓴다" 가 이 파일의 요점이다. 언뜻 순환처럼 보인다(선언이 선언을 믿는다).
 *   그런데 **모든 op 이 검사되면** 순환이 아니다 — 그것이 고전적인 *effect summary* 논증이고,
 *   증명이 **실행에 대한 귀납**이라는 데서 나온다(프로그램에 대한 귀납이 아니다).
 *   ⇒ **재귀가 공짜로 처리된다**(§5). 무한 재귀도 마찬가지다: 실제로 **일어난** 효과는
 *     언제나 **유한한 깊이**에서 일어나므로 유도가 유한하다.
 *
 * 증명하는 것:
 *   ① `effect_sound` — 검사를 통과한 프로그램에서 **실제로 일어나는 모든 효과는 선언 안에 있다.**
 *   ② `pure_means_pure` — `effects none` 인 op 은 **아무 효과도 내지 않는다.**
 *   ③ `pure_calls_commute` — 순수한 조각은 **자리를 바꿔도 효과 열이 같다**(최적화 적법성의
 *      효과 층 조각 — 값 층은 아직 아니다, §7).
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 효과 집합 ────────────────────────────────────────────────────────── *)

(* 원자는 번호로 둔다(`io`·`alloc`·`panic` … — SPEC 부록 K 의 14 원자. 개수는 상관없다). *)
Definition atom := nat.
Definition eff  := list atom.        (* 집합을 목록으로 — 중복은 뜻이 없다 *)

Definition inb (a : atom) (s : eff) : bool := existsb (Nat.eqb a) s.
Definition subset (s1 s2 : eff) : bool := forallb (fun a => inb a s2) s1.

Lemma inb_In : forall a s, inb a s = true <-> In a s.
Proof.
  intros a s. unfold inb. rewrite existsb_exists. split.
  - intros [x [Hin Hx]]. apply Nat.eqb_eq in Hx. subst; exact Hin.
  - intros Hin. exists a. split; [ exact Hin | apply Nat.eqb_refl ].
Qed.

Lemma subset_In : forall s1 s2 a, subset s1 s2 = true -> In a s1 -> In a s2.
Proof.
  intros s1 s2 a Hs Hin. unfold subset in Hs. rewrite forallb_forall in Hs.
  apply inb_In. apply Hs. exact Hin.
Qed.

(* ── 2. 프로그램 ─────────────────────────────────────────────────────────── *)

Inductive expr : Type :=
  | EPrim (e : eff)          (* 원시 연산 — 자기가 효과를 낸다(print · alloc …) *)
  | ECall (f : nat)          (* op 호출 *)
  | ESeq  (a b : expr)       (* 이어서 *)
  | EIf   (a b : expr).      (* 분기 — 어느 갈래든 갈 수 있다 *)

(* op 표: 이름 → (선언한 효과, 본문). 이름공간은 평면이다(구현과 같다 — E-NAME-DUP). *)
Definition prog := list (nat * (eff * expr)).

Fixpoint lookup (P : prog) (f : nat) : option (eff * expr) :=
  match P with
  | [] => None
  | (g, v) :: r => if Nat.eqb f g then Some v else lookup r f
  end.

Definition declared_of (P : prog) (f : nat) : eff :=
  match lookup P f with Some (d, _) => d | None => [] end.

(* ── 3. 정적 검사 — 구현과 같은 모양 ─────────────────────────────────────── *)

(* ★ 호출 자리에서 **선언**을 쓴다. 본문으로 내려가지 않는다 — 그것이 요약(summary)이다. *)
Fixpoint eff_of (P : prog) (t : expr) : eff :=
  match t with
  | EPrim e  => e
  | ECall f  => declared_of P f
  | ESeq a b => eff_of P a ++ eff_of P b
  | EIf  a b => eff_of P a ++ eff_of P b     (* 두 갈래를 **합친다** — 어느 쪽이든 갈 수 있다 *)
  end.

Definition op_ok (P : prog) (v : eff * expr) : bool :=
  let '(d, b) := v in subset (eff_of P b) d.

Definition prog_ok (P : prog) : bool :=
  forallb (fun p => op_ok P (snd p)) P.

(* ── 4. 동적 의미 — "이 효과가 **일어난다**" ─────────────────────────────── *)

(* ★★ 왜 실행 **열**(trace)이 아니라 *"원자 a 가 일어난다"* 인가:
     열로 적으면 **끝나는 실행**만 다룰 수 있다(무한 루프는 열이 없다). 그런데 효과 계약이
     지켜야 하는 것은 *"끝나면 이만큼"* 이 아니라 *"언제 봐도 이 밖은 없다"* 다.
     원자 하나가 일어나는 사건은 **언제나 유한한 깊이**에서 일어나므로, 이렇게 적으면
     **무한 재귀 프로그램도 정리 안에 들어온다.** (§7 이 그 차이를 다시 적는다.) *)
Inductive performs : prog -> expr -> atom -> Prop :=
  | Pf_prim  : forall P e a, In a e -> performs P (EPrim e) a
  | Pf_seq_l : forall P x y a, performs P x a -> performs P (ESeq x y) a
  | Pf_seq_r : forall P x y a, performs P y a -> performs P (ESeq x y) a
  | Pf_if_l  : forall P x y a, performs P x a -> performs P (EIf x y) a
  | Pf_if_r  : forall P x y a, performs P y a -> performs P (EIf x y) a
  | Pf_call  : forall P f d b a,
      lookup P f = Some (d, b) -> performs P b a -> performs P (ECall f) a.

(* ── 5. ★★★ 건전성 ──────────────────────────────────────────────────────── *)

(* 핵심. **실행에 대한 귀납**이다 — 프로그램에 대한 귀납이 아니다.
   그래서 재귀(자기 자신을 부르는 op)도, 상호 재귀도, 무한 재귀도 따로 다룰 것이 없다. *)
Lemma effect_sound_expr : forall P t a,
  prog_ok P = true -> performs P t a -> In a (eff_of P t).
Proof.
  intros P t a Hok H. revert Hok. induction H as
    [ P e a Hin
    | P x y a Hx IH | P x y a Hy IH
    | P x y a Hx IH | P x y a Hy IH
    | P f d b a Hl Hb IH ]; intros Hok; simpl.
  - exact Hin.
  - apply in_or_app; left; exact (IH Hok).
  - apply in_or_app; right; exact (IH Hok).
  - apply in_or_app; left; exact (IH Hok).
  - apply in_or_app; right; exact (IH Hok).
  - (* ★ 호출 — 여기서 **검사된 선언**이 요약으로 쓰인다 *)
    assert (Hok' := Hok).
    unfold declared_of. rewrite Hl.
    assert (Hin : In (f, (d, b)) P).
    { clear -Hl. induction P as [| [g v] r IH]; simpl in Hl; [ discriminate | ].
      destruct (Nat.eqb f g) eqn:E.
      - apply Nat.eqb_eq in E; subst g. inversion Hl; subst v. left; reflexivity.
      - right; apply IH; exact Hl. }
    unfold prog_ok in Hok. rewrite forallb_forall in Hok.
    specialize (Hok _ Hin). simpl in Hok.
    exact (subset_In _ _ _ Hok (IH Hok')).
Qed.

(* ★★★ **선언이 실제를 덮는다.** 검사를 통과한 프로그램에서 op f 를 부르면,
   실제로 일어나는 효과는 **f 가 선언한 것 안에** 있다 — 호출 깊이 무관, 재귀 무관. *)
Theorem effect_sound : forall P f d b a,
  prog_ok P = true ->
  lookup P f = Some (d, b) ->
  performs P (ECall f) a ->
  In a d.
Proof.
  intros P f d b a Hok Hl H.
  pose proof (effect_sound_expr P (ECall f) a Hok H) as Hin.
  simpl in Hin. unfold declared_of in Hin. rewrite Hl in Hin. exact Hin.
Qed.

(* ★★ `effects none` 은 **진짜로** 아무 효과도 없다는 뜻이다.
   최적화(재정렬·CSE·memoize)가 기대는 문장이 정확히 이것이다. *)
Corollary pure_means_pure : forall P f b a,
  prog_ok P = true ->
  lookup P f = Some ([], b) ->
  ~ performs P (ECall f) a.
Proof.
  intros P f b a Hok Hl H.
  pose proof (effect_sound P f [] b a Hok Hl H) as Hin. exact Hin.
Qed.

(* ── 6. ★ 최적화 적법성 — 효과 층의 조각 ────────────────────────────────── *)

(* 순수한 조각은 **관찰되지 않는다**. 그래서 자리를 바꿔도 *"무엇이 일어났나"* 가 같다. *)
Definition pure (P : prog) (t : expr) : Prop := forall a, ~ performs P t a.

Lemma eff_nil_pure : forall P t,
  prog_ok P = true -> eff_of P t = [] -> pure P t.
Proof.
  intros P t Hok He a H.
  pose proof (effect_sound_expr P t a Hok H) as Hin. rewrite He in Hin. exact Hin.
Qed.

(* ★ 재정렬 적법성(효과 층): 순수한 조각은 어디에 두어도 **일어나는 효과 집합이 같다.** *)
Theorem pure_calls_commute : forall P x y a,
  pure P x ->
  (performs P (ESeq x y) a <-> performs P (ESeq y x) a).
Proof.
  intros P x y a Hpure. split; intros H; inversion H; subst.
  - exfalso. apply (Hpure a). assumption.
  - apply Pf_seq_l. assumption.
  - apply Pf_seq_r. assumption.
  - exfalso. apply (Hpure a). assumption.
Qed.

(* ★ 제거 적법성(효과 층): 순수한 조각은 **지워도** 일어나는 효과가 같다(죽은 코드 제거·CSE). *)
Theorem dropping_pure_is_legal : forall P x y a,
  pure P x ->
  (performs P (ESeq x y) a <-> performs P y a).
Proof.
  intros P x y a Hpure. split; intros H.
  - inversion H; subst; [ exfalso; apply (Hpure a); assumption | assumption ].
  - apply Pf_seq_r; exact H.
Qed.

(* ── 7. ★★ 공허하지 않다 — 통과하는 것과 거절되는 것 ────────────────────── *)

Definition io : atom := 0.
Definition alloc : atom := 1.

(* ① **재귀 op** — 자기를 부르면서 io 를 선언한다. 검사가 통과시킨다(선언 요약 덕분). *)
Definition rec_prog : prog :=
  [ (0, ([io], ESeq (EPrim [io]) (EIf (ECall 0) (EPrim [])))) ].

Theorem recursion_is_accepted : prog_ok rec_prog = true.
Proof. vm_compute; reflexivity. Qed.

(* 그리고 그 재귀 프로그램에서 io 는 **실제로 일어난다**(모델이 공허하지 않다). *)
Theorem recursion_performs_io : performs rec_prog (ECall 0) io.
Proof.
  eapply Pf_call; [ reflexivity | ].
  apply Pf_seq_l. apply Pf_prim. left; reflexivity.
Qed.

(* ★ 그런데 **선언 밖의 효과는 일어날 수 없다** — 위 정리의 즉각적 결과. *)
Corollary recursion_never_allocs : ~ performs rec_prog (ECall 0) alloc.
Proof.
  intro H.
  pose proof (effect_sound rec_prog 0 [io] _ alloc recursion_is_accepted eq_refl H) as Hin.
  simpl in Hin. destruct Hin as [Hc | Hc]; [ discriminate | contradiction ].
Qed.

(* ② **거짓말은 거절된다**: `effects none` 인데 본문이 io 를 한다. *)
Definition liar : prog := [ (0, ([], EPrim [io])) ].
Theorem lying_is_rejected : prog_ok liar = false.
Proof. vm_compute; reflexivity. Qed.

(* ③ **호출을 통한 거짓말도 거절된다** — 이것이 구현에서 실제로 샜던 자리다
   (`low_check.c` 의 주석 — **효과가 호출을 통해 전파된 적이 없다**.) *)
Definition liar_via_call : prog :=
  (* 0 은 순수하다고 선언하는데, io 인 op 1 을 부른다 *)
  [ (0, ([], ECall 1)) ; (1, ([io], EPrim [io])) ].
Theorem lying_through_a_call_is_rejected : prog_ok liar_via_call = false.
Proof. vm_compute; reflexivity. Qed.

(* ── 8. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **`task_group` 흡수 규칙이 모델 밖이다.** 구현은 멤버가 둘 이상인 그룹에서 `concurrent`
 *   를 **지운다**(RFC-0071 A5): *"동료가 자기 안에 있으므로 요구가 그 자리에서 만족된다."*
 *   그것은 ⊆ 규율이 아니라 **요구의 해소**이고, 그 논증은 여기서 형식화하지 않았다.
 *   ⇒ 이 파일의 정리는 **효과가 밖으로 새지 않는다**를 말하고, 그 예외는 **말하지 않는다.**
 * · **원자 사이의 폐포**(`concurrent` ⇒ `wait`)도 모델 밖이다 — 원자를 서로 무관한 번호로 뒀다.
 *   폐포는 선언을 **키우는** 방향이므로 건전성을 해치지 않지만, 그 사실도 증명하지 않았다.
 * · **값이 없다.** 그래서 §6 의 "적법성" 은 *"어떤 효과가 일어나는가"* 층이다. 재정렬이
 *   **같은 값**을 준다는 것은 별개의 정리이고, 그것은 타입·의미 모델을 요구한다
 *   (`LowentType.v` 가 그 방향의 첫 걸음이다).
 * · **효과가 언제 일어나는지(순서)를 보지 않는다.** `performs` 는 집합이다. 순서가 중요한
 *   성질(예: io 순서 보존)은 이 모델로 말할 수 없다.
 * · 구현의 **낱말 인식**(효과 낱말 오타·op 이름 겹침)은 게이트가 본다(check-effects·check-vocab).
 *   이 파일은 그 낱말이 이미 원자로 옮겨진 **뒤**의 규율을 본다.
 *)

(* LowentNest.v — ★★★ **중첩 루프**. 순수 Coq. `LowentLoop.v` 의 보조정리 B를 임의 깊이로.
 *
 * 무엇이 남아 있었나. 16장의 미증명 목록에 이 줄이 있었다:
 *
 *     | 중첩 루프 | 종이 **스케치** |
 *
 * `LowentLoop.v` 의 `loop_agreement` 는 **평평한** 프로그램 하나를 다룬다: (pre, body) 와
 * 자연수 k. 실제 프로그램은 루프 안에 루프가 있고, **안쪽 루프의 횟수는 바깥 반복마다 다르다.**
 * 그 둘을 k 하나로는 적을 수 없다.
 *
 * ★ 이 파일이 하는 일 — 프로그램을 **나무**로 만들고, 전개를 **관계**로 만든다:
 *
 *     np      ::= NLeaf evs | NSeq a b | NAlt a b | NLoop b   (프로그램 나무 · 임의 깊이)
 *     expands ::= 귀납 관계                                 (한 나무의 **모든** 전개)
 *
 *   `expands` 가 관계인 것이 요점이다. 함수 `unroll body k` 는 *"모든 반복이 똑같이 돈다"* 를
 *   강요한다. 관계는 그러지 않는다 — **반복 i 의 안쪽 루프는 3번, 반복 i+1 은 0번** 이어도
 *   같은 나무의 전개다. ⇒ 정리가 덮는 프로그램이 k-족보다 **진짜로 넓다.**
 *
 * 증명하는 것(§5):
 *
 *     srun_np [] p = Some l  →  expands p e  →  ∃s', drun d0 e = Some s'
 *
 *   평서문: **정적 검사를 통과한 중첩 프로그램은 어떤 전개에서도 차용 위반이 없다.**
 *   깊이 제한 없음 · 반복 횟수 제한 없음 · 반복마다 달라도 됨 · **분기 포함**.
 *
 * ★★ **분기(2026-07-31 추가)**: 07장 ⑥ 1번이 *"분기는 이 정리에 없다"* 고 적어 두었다.
 *   이제 있다. 그리고 들어오면서 **합류(join)** 라는 물음이 생긴다 — 두 갈래가 서로 다른
 *   정적 상태로 끝나면 그 뒤는 무엇인가? 답은 **교집합**이다(§2-1). 그리고 그것이 옳은
 *   이유가 이 파일에서 한 줄로 나온다: **정적 상태가 작아지는 것은 언제나 안전하다**
 *   (INV·SINV 가 부분집합에서 물려받는다 — `INV_subset`·`SINV_subset`).
 *
 * ★ 그리고 옛 정리는 **따름정리로 되찾는다**(§6, `flat_loop_recovered`) — 새 정리가 옛것을
 *   덮는다는 것을 기계가 확인한다. 덮지 못하면 그것은 다른 정리이지 일반화가 아니다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.
Require Import LowentLoop.

(* ── 1. 프로그램 나무 ────────────────────────────────────────────────────── *)

Inductive np : Type :=
  | NLeaf (evs : list ev)      (* 직선 조각 *)
  | NSeq  (a b : np)           (* 이어 붙이기 *)
  | NAlt  (a b : np)           (* ★ 분기 — 실행은 **한 갈래만** 간다 *)
  | NLoop (b : np).            (* 루프 — 횟수는 **적지 않는다**(전개가 정한다) *)

(* live 의 같음 — 고정점 판정에 필요하다(모델에 없어서 여기서 준다). *)
Definition ent_eqb (p q : nat * (nat * bool)) : bool :=
  let '(t, (x, m)) := p in let '(t', (x', m')) := q in
  Nat.eqb t t' && Nat.eqb x x' && Bool.eqb m m'.

Fixpoint live_eqb (l1 l2 : live) : bool :=
  match l1, l2 with
  | [], [] => true
  | a :: r1, b :: r2 => ent_eqb a b && live_eqb r1 r2
  | _, _ => false
  end.

Lemma ent_eqb_eq : forall p q, ent_eqb p q = true -> p = q.
Proof.
  intros [t [x m]] [t' [x' m']] H. simpl in H.
  apply andb_true_iff in H as [H Hm]. apply andb_true_iff in H as [Ht Hx].
  apply Nat.eqb_eq in Ht. apply Nat.eqb_eq in Hx. apply eqb_prop in Hm.
  subst. reflexivity.
Qed.

Lemma live_eqb_eq : forall l1 l2, live_eqb l1 l2 = true -> l1 = l2.
Proof.
  induction l1 as [| a r1 IH]; intros [| b r2] H; simpl in H; try discriminate; [ reflexivity | ].
  apply andb_true_iff in H as [Ha Hr].
  rewrite (ent_eqb_eq a b Ha), (IH r2 Hr). reflexivity.
Qed.

(* ── 2. 정적 검사 — 나무 위로 ────────────────────────────────────────────── *)

(* ★ 루프의 규칙은 `LowentLoop.v` 와 **같다**: 들어가는 정적 상태가 본문의 **고정점**이어야
   한다. 그것이 이 모델이 루프를 다루는 방식이고, 새로운 것은 **그 규칙이 임의 깊이로
   중첩된다**는 점이다 — 안쪽 루프의 고정점 요구가 바깥 본문의 고정점 요구 안에 들어간다. *)
(* ★★ **합류(join) = 교집합.** 갈래마다 살아 있는 차용이 다르면, 합류 뒤에 살아 있다고
   말할 수 있는 것은 **양쪽 모두에서** 살아 있는 것뿐이다. 어느 갈래로 왔는지 정적으로
   모르기 때문이다. 한쪽에만 있는 것을 살렸다가 다른 갈래로 왔으면 **없는 차용을 쓰게 된다.** *)
Definition ent_in (p : nat * (nat * bool)) (l : live) : bool := existsb (ent_eqb p) l.
Definition join (la lb : live) : live := filter (fun p => ent_in p lb) la.

Lemma In_join_l : forall p la lb, In p (join la lb) -> In p la.
Proof. intros p la lb H. unfold join in H. apply filter_In in H as [H _]. exact H. Qed.

Lemma In_join_r : forall p la lb, In p (join la lb) -> In p lb.
Proof.
  intros p la lb H. unfold join in H. apply filter_In in H as [_ H].
  unfold ent_in in H. apply existsb_exists in H as [q [Hq He]].
  rewrite (ent_eqb_eq p q He). exact Hq.
Qed.

Fixpoint srun_np (l : live) (p : np) : option live :=
  match p with
  | NLeaf evs => srun l evs
  | NSeq a b => match srun_np l a with
                | Some l1 => srun_np l1 b
                | None => None
                end
  | NAlt a b => match srun_np l a, srun_np l b with
                | Some la, Some lb => Some (join la lb)   (* ★ 교집합 *)
                | _, _ => None
                end
  | NLoop b => match srun_np l b with
               | Some l1 => if live_eqb l1 l then Some l else None
               | None => None
               end
  end.

(* ── 3. 전개 — **함수가 아니라 관계** ────────────────────────────────────── *)

(* ★★ 이 관계가 이 파일의 요점이다.
     · 루프는 **0번 이상** 돈다(Ex_loop0 / Ex_loopS).
     · 각 반복의 본문은 **독립적으로** 전개된다 ⇒ 안쪽 루프 횟수가 반복마다 달라도 된다.
     · 깊이에 제한이 없다.
   함수 `unroll body k` 로는 셋 중 어느 것도 적을 수 없다. *)
Inductive expands : np -> list ev -> Prop :=
  | Ex_leaf  : forall evs, expands (NLeaf evs) evs
  | Ex_seq   : forall a b ea eb,
      expands a ea -> expands b eb -> expands (NSeq a b) (ea ++ eb)
  | Ex_alt_l : forall a b ea, expands a ea -> expands (NAlt a b) ea   (* ★ 한 갈래만 간다 *)
  | Ex_alt_r : forall a b eb, expands b eb -> expands (NAlt a b) eb
  | Ex_loop0 : forall b, expands (NLoop b) []
  | Ex_loopS : forall b eb er,
      expands b eb -> expands (NLoop b) er -> expands (NLoop b) (eb ++ er).

(* ── 4. 실행을 이어 붙이기 ───────────────────────────────────────────────── *)

Lemma drun_app : forall a b s,
  drun s (a ++ b) = match drun s a with None => None | Some s' => drun s' b end.
Proof.
  induction a as [| e r IH]; intros b s; simpl; [ reflexivity | ].
  destruct (dstep s e); [ apply IH | reflexivity ].
Qed.

(* ── 4-1. ★★ 합류가 옳은 이유 — **정적 상태가 작아지는 것은 안전하다** ───────── *)

(* 이 두 줄이 분기 전체를 떠받친다. INV·SINV 는 *"정적으로 살아 있으면 동적으로도 있다"* 와
   *"겹친 것은 전부 공유다"* 인데, 둘 다 **작은 목록에서 더 쉽다.** *)
Lemma INV_subset : forall l l' s,
  (forall p, In p l' -> In p l) -> INV l s -> INV l' s.
Proof.
  intros l l' s Hsub H. destruct s as [d ti].
  intros t x m Hin. apply H. apply Hsub. exact Hin.
Qed.

Lemma SINV_subset : forall l l',
  (forall p, In p l' -> In p l) -> SINV l -> SINV l'.
Proof.
  intros l l' Hsub H t1 x m1 t2 m2 H1 H2 Hne.
  apply (H t1 x m1 t2 m2); [ apply Hsub; exact H1 | apply Hsub; exact H2 | exact Hne ].
Qed.

(* ── 5. ★★★ 중첩 합치 정리 ──────────────────────────────────────────────── *)

(* 핵심 보조정리. `expands` 유도에 대한 귀납이고, 루프 갈래에서 **고정점**을 쓴다:
   본문이 정적 상태를 제자리로 되돌리므로, 다음 반복은 첫 반복과 **같은 전제**에서 시작한다.
   ⇒ 반복 횟수에 대한 별도 귀납이 필요 없다 — 전개 관계의 귀납이 그것을 이미 한다. *)
Lemma nest_agreement : forall p e,
  expands p e ->
  forall l s l',
  srun_np l p = Some l' ->
  INV l s -> SINV l -> FRESH s ->
  exists s', drun s e = Some s' /\ INV l' s' /\ SINV l' /\ FRESH s'.
Proof.
  intros p e H. induction H as
    [ evs
    | a b ea eb Ha IHa Hb IHb
    | a b ea Ha IHa
    | a b eb Hb IHb
    | b
    | b eb er Hb IHb Hloop IHloop ];
    intros l s l' Hs Hinv Hsinv Hfresh.

  - (* NLeaf — 직선 조각은 `LowentLoop.v` 의 agreement_run 이 그대로 처리한다 *)
    simpl in Hs.
    destruct (agreement_run evs l s Hinv Hsinv Hfresh (ex_intro _ l' Hs))
      as [s1 [l1 [Hd [Hs1 [Hinv1 [Hsinv1 Hfresh1]]]]]].
    assert (Hll : l1 = l') by (rewrite Hs in Hs1; inversion Hs1; reflexivity).
    rewrite Hll in Hinv1, Hsinv1.
    exists s1. split; [ exact Hd | ]. split; [ exact Hinv1 | ].
    split; [ exact Hsinv1 | exact Hfresh1 ].

  - (* NSeq — 앞을 돌리고 그 결과 상태에서 뒤를 돌린다 *)
    simpl in Hs.
    destruct (srun_np l a) as [l1 | ] eqn:Ea; [ | discriminate ].
    destruct (IHa l s l1 Ea Hinv Hsinv Hfresh) as [s1 [Hd1 [Hinv1 [Hsinv1 Hfresh1]]]].
    destruct (IHb l1 s1 l' Hs Hinv1 Hsinv1 Hfresh1) as [s2 [Hd2 [Hinv2 [Hsinv2 Hfresh2]]]].
    exists s2. rewrite drun_app, Hd1, Hd2.
    split; [ reflexivity | ]. split; [ exact Hinv2 | ].
    split; [ exact Hsinv2 | exact Hfresh2 ].

  - (* ★ 분기 — 왼쪽 갈래. 결과 상태는 **교집합**이므로 INV·SINV 가 부분집합으로 내려온다 *)
    simpl in Hs.
    destruct (srun_np l a) as [la | ] eqn:Ea; [ | discriminate ].
    destruct (srun_np l b) as [lb | ] eqn:Eb; [ | discriminate ].
    inversion Hs; subst l'.
    destruct (IHa l s la Ea Hinv Hsinv Hfresh) as [s1 [Hd1 [Hinv1 [Hsinv1 Hfresh1]]]].
    exists s1. split; [ exact Hd1 | ].
    split; [ eapply INV_subset; [ intros q Hq; exact (In_join_l q la lb Hq) | exact Hinv1 ] | ].
    split; [ eapply SINV_subset; [ intros q Hq; exact (In_join_l q la lb Hq) | exact Hsinv1 ]
           | exact Hfresh1 ].

  - (* ★ 분기 — 오른쪽 갈래. 같은 논증, 교집합의 다른 쪽 *)
    simpl in Hs.
    destruct (srun_np l a) as [la | ] eqn:Ea; [ | discriminate ].
    destruct (srun_np l b) as [lb | ] eqn:Eb; [ | discriminate ].
    inversion Hs; subst l'.
    destruct (IHb l s lb Eb Hinv Hsinv Hfresh) as [s1 [Hd1 [Hinv1 [Hsinv1 Hfresh1]]]].
    exists s1. split; [ exact Hd1 | ].
    split; [ eapply INV_subset; [ intros q Hq; exact (In_join_r q la lb Hq) | exact Hinv1 ] | ].
    split; [ eapply SINV_subset; [ intros q Hq; exact (In_join_r q la lb Hq) | exact Hsinv1 ]
           | exact Hfresh1 ].

  - (* 루프를 0번 돈다 *)
    simpl in Hs.
    destruct (srun_np l b) as [l1 | ] eqn:Eb; [ | discriminate ].
    destruct (live_eqb l1 l) eqn:Ee; [ | discriminate ].
    inversion Hs; subst l'.
    exists s. simpl. split; [ reflexivity | ]. split; [ exact Hinv | ].
    split; [ exact Hsinv | exact Hfresh ].

  - (* ★ 루프를 한 번 더 돈다 — 고정점이 전제를 되돌려 준다 *)
    simpl in Hs.
    destruct (srun_np l b) as [l1 | ] eqn:Eb; [ | discriminate ].
    destruct (live_eqb l1 l) eqn:Ee; [ | discriminate ].
    inversion Hs; subst l'.
    assert (Hl1 : l1 = l) by (apply live_eqb_eq; exact Ee). subst l1.
    (* 한 반복: l → l *)
    destruct (IHb l s l Eb Hinv Hsinv Hfresh) as [s1 [Hd1 [Hinv1 [Hsinv1 Hfresh1]]]].
    (* 나머지 반복들: 같은 정적 상태에서 시작한다 *)
    assert (Hloopstat : srun_np l (NLoop b) = Some l).
    { simpl. rewrite Eb, Ee. reflexivity. }
    destruct (IHloop l s1 l Hloopstat Hinv1 Hsinv1 Hfresh1)
      as [s2 [Hd2 [Hinv2 [Hsinv2 Hfresh2]]]].
    exists s2. rewrite drun_app, Hd1, Hd2.
    split; [ reflexivity | ]. split; [ exact Hinv2 | ].
    split; [ exact Hsinv2 | exact Hfresh2 ].
Qed.

(* ★★★ 중첩 루프 합치 정리 — **어떤 전개에서도** ⚡ 가 없다. *)
Theorem nested_agreement : forall p e l,
  srun_np [] p = Some l ->
  expands p e ->
  exists s', drun d0 e = Some s'.
Proof.
  intros p e l Hs He.
  assert (Hinv0 : INV [] d0) by (intros t x m H; simpl in H; contradiction).
  assert (Hsinv0 : SINV []) by (intros t1 x m1 t2 m2 H; simpl in H; contradiction).
  assert (Hfresh0 : FRESH d0) by (intros u i H; simpl in H; discriminate).
  destruct (nest_agreement p e He [] d0 l Hs Hinv0 Hsinv0 Hfresh0)
    as [s' [Hd _]].
  exists s'. exact Hd.
Qed.

(* ── 6. ★ 옛 정리를 되찾는다 ────────────────────────────────────────────── *)

(* 평평한 (pre, body, k) 는 나무 하나의 전개 하나다. *)
Lemma unroll_expands : forall body k,
  expands (NLoop (NLeaf body)) (unroll body k).
Proof.
  intros body. induction k as [| k IH]; simpl.
  - apply Ex_loop0.
  - apply Ex_loopS; [ apply Ex_leaf | exact IH ].
Qed.

(* ★ `LowentLoop.v` 의 `loop_agreement` 와 **같은 전제**에서 같은 결론이 나온다.
   ⇒ 새 정리는 옛 정리의 **일반화**다(다른 정리가 아니다). *)
Theorem flat_loop_recovered : forall pre body,
  (exists lpre, srun [] pre = Some lpre /\ srun lpre body = Some lpre) ->
  forall k, exists s', drun d0 (pre ++ unroll body k) = Some s'.
Proof.
  intros pre body [lpre [Hpre Hfix]] k.
  apply (nested_agreement (NSeq (NLeaf pre) (NLoop (NLeaf body))) _ lpre).
  - simpl. rewrite Hpre, Hfix.
    destruct (live_eqb lpre lpre) eqn:E; [ reflexivity | ].
    (* live_eqb 는 반사적이다 — 아래에서 따로 증명하지 않고 여기서 바로 배제한다 *)
    exfalso. clear -E. induction lpre as [| [t [x m]] r IH]; simpl in E.
    + discriminate.
    + rewrite !Nat.eqb_refl, eqb_reflx in E. simpl in E. exact (IH E).
  - apply Ex_seq; [ apply Ex_leaf | apply unroll_expands ].
Qed.

(* ── 7. ★ 한 번 벗기기 — 고정점 요구가 보이는 것보다 싸다 ───────────────── *)

(* 고정점을 **들어가는 자리에서** 요구하는 것이 좁아 보인다: `loop { let r = borrow x; … }` 는
   첫 반복이 정적 상태를 바꾼다(τ 가 생긴다). 그런데 그런 루프는 **한 번 벗겨서** 적으면
   이 문법 안에 들어온다 — `b ; loop b`. 컴파일러의 데이터흐름이 하는 일(첫 라운드 뒤 고정점에
   닿는다)이 프로그램 수준에서 표현된다. ⇒ 새 규칙이 필요 없다. *)
Theorem peeled_loop_is_safe : forall b l1,
  srun_np [] b = Some l1 ->
  srun_np l1 b = Some l1 ->
  forall e, expands (NSeq b (NLoop b)) e -> exists s', drun d0 e = Some s'.
Proof.
  intros b l1 H0 Hfix e He.
  apply (nested_agreement (NSeq b (NLoop b)) e l1); [ | exact He ].
  simpl. rewrite H0, Hfix.
  destruct (live_eqb l1 l1) eqn:E; [ reflexivity | ].
  exfalso. clear -E. induction l1 as [| [t [x m]] r IH]; simpl in E.
  - discriminate.
  - rewrite !Nat.eqb_refl, eqb_reflx in E. simpl in E. exact (IH E).
Qed.

(* ── 8. ★★ 공허하지 않다 — 진짜 중첩 프로그램 둘 ────────────────────────── *)

(* 정리가 "아무것도 통과 못 하는 검사" 위에 서 있으면 공짜로 참이다.
   그래서 **통과하는 중첩 프로그램**과 **거절되는 중첩 프로그램**을 각각 계산한다. *)

(* ① 통과: 공유 차용 하나를 pre 에서 만들고, **이중 루프**가 그것을 읽기만 한다. *)
Definition ok_pre  : list ev := [ Create 1 0 false ].
Definition ok_prog : np :=
  NSeq (NLeaf ok_pre)
       (NLoop (NSeq (NLeaf [ Use 1 false ])
                    (NLoop (NLeaf [ Use 1 false ])))).

Theorem nested_example_is_accepted : srun_np [] ok_prog = Some [(1, (0, false))].
Proof. vm_compute; reflexivity. Qed.

(* ★ 그리고 **반복마다 안쪽 횟수가 다른** 전개가 실제로 돈다 — 바깥 2회, 안쪽 1회와 2회.
   (전개 관계의 모양 그대로 적는다: `eb ++ er` 의 괄호가 곧 유도 나무다.) *)
Definition U : ev := Use 1 false.
Definition inner1 : list ev := [U] ++ [].                 (* 안쪽 루프 1회 *)
Definition inner2 : list ev := [U] ++ ([U] ++ []).        (* 안쪽 루프 2회 *)
Definition outer1 : list ev := [U] ++ inner1.             (* 바깥 1회차 *)
Definition outer2 : list ev := [U] ++ inner2.             (* 바깥 2회차 — 안쪽이 다르다 *)
Definition ok_trace : list ev := ok_pre ++ (outer1 ++ (outer2 ++ [])).

Theorem uneven_trace_expands : expands ok_prog ok_trace.
Proof.
  unfold ok_prog, ok_trace, outer1, outer2, inner1, inner2, U.
  apply Ex_seq; [ apply Ex_leaf | ].
  apply Ex_loopS.
  - apply Ex_seq; [ apply Ex_leaf | ].
    apply Ex_loopS; [ apply Ex_leaf | apply Ex_loop0 ].
  - apply Ex_loopS; [ | apply Ex_loop0 ].
    apply Ex_seq; [ apply Ex_leaf | ].
    apply Ex_loopS; [ apply Ex_leaf | ].
    apply Ex_loopS; [ apply Ex_leaf | apply Ex_loop0 ].
Qed.

(* 그 전개는 실제로 **끝까지 돈다**(⚡ 없음) — 정리가 약속한 그대로. *)
Theorem uneven_trace_runs :
  match drun d0 ok_trace with Some _ => true | None => false end = true.
Proof. vm_compute; reflexivity. Qed.

(* ② 거절: 바깥 루프가 소유자로 **쓰고**, 안쪽 루프가 죽은 차용을 쓴다. *)
Definition bad_prog : np :=
  NSeq (NLeaf [ Create 1 0 true ])
       (NLoop (NSeq (NLeaf [ Own 0 true ])
                    (NLoop (NLeaf [ Use 1 false ])))).

Theorem nested_example_is_rejected : srun_np [] bad_prog = None.
Proof. vm_compute; reflexivity. Qed.

(* ★ 그리고 그 거절은 **정당하다** — 그 프로그램의 전개 하나가 실제로 ⚡ 를 낸다. *)
Definition bad_trace : list ev :=
  [ Create 1 0 true ] ++ (([ Own 0 true ] ++ ([U] ++ [])) ++ []).

Theorem bad_trace_expands : expands bad_prog bad_trace.
Proof.
  unfold bad_prog, bad_trace, U.
  apply Ex_seq; [ apply Ex_leaf | ].
  apply Ex_loopS; [ | apply Ex_loop0 ].
  apply Ex_seq; [ apply Ex_leaf | ].
  apply Ex_loopS; [ apply Ex_leaf | apply Ex_loop0 ].
Qed.

Theorem bad_trace_traps : drun d0 bad_trace = None.
Proof. vm_compute; reflexivity. Qed.

(* ── 8-1. ★★ 분기도 공허하지 않다 — 합류가 실제로 무언가를 막는다 ────────── *)

(* ① 통과: 차용을 **pre 에서** 만들고 두 갈래가 읽기만 한다 ⇒ 합류 뒤에도 살아 있다. *)
Definition alt_ok : np :=
  NSeq (NLeaf [ Create 1 0 false ])
       (NSeq (NAlt (NLeaf [ Use 1 false ]) (NLeaf [ Use 1 false ]))
             (NLeaf [ Use 1 false ])).

Theorem branch_example_is_accepted : srun_np [] alt_ok = Some [(1, (0, false))].
Proof. vm_compute; reflexivity. Qed.

(* ② 거절: 차용을 **한 갈래에서만** 만들고 합류 뒤에 쓴다.
   ★ 이것이 교집합이 막는 바로 그 오류다 — 다른 갈래로 왔으면 그 차용은 **없다.** *)
Definition alt_bad : np :=
  NSeq (NAlt (NLeaf [ Create 1 0 false ]) (NLeaf []))
       (NLeaf [ Use 1 false ]).

Theorem one_sided_borrow_is_rejected : srun_np [] alt_bad = None.
Proof. vm_compute; reflexivity. Qed.

(* ★ 그리고 그 거절은 **정당하다**: 오른쪽 갈래로 간 전개가 실제로 ⚡ 를 낸다. *)
Definition alt_bad_trace : list ev := [] ++ [ Use 1 false ].

Theorem alt_bad_trace_expands : expands alt_bad alt_bad_trace.
Proof.
  unfold alt_bad, alt_bad_trace.
  apply Ex_seq; [ apply Ex_alt_r; apply Ex_leaf | apply Ex_leaf ].
Qed.

Theorem alt_bad_trace_traps : drun d0 alt_bad_trace = None.
Proof. vm_compute; reflexivity. Qed.

(* ── 9. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · ~~**분기(if)가 없다.**~~ ★ **넣었다**(§8-1). 합류는 **교집합**이고, 그것이 옳은 이유는
 *   한 줄이다: **정적 상태가 작아지는 것은 언제나 안전하다**(`INV_subset`·`SINV_subset`).
 *   ☞ 다만 이것은 **차용 사실**의 합류다. **수치·관계 사실**의 합류(05장의 lru 사례)는
 *     다른 격자이고 `LowentJoin.v` 가 본다.
 * · **고정점을 들어가는 자리에서 요구한다.** 컴파일러는 조인으로 그 자리에 **닿는다**;
 *   그 닿는 과정(`live` 위의 격자와 단조 반복)은 이 모델에 없다. §7 이 한 번 벗기기로
 *   그 간극의 **일부**를 프로그램 수준에서 메운다 — 전부가 아니다.
 * · **`break`/`continue`/조기 반환이 없다.** 전개 관계가 "본문을 통째로" 돈다고 본다.
 * · 이벤트 모델의 한계는 그대로다(06장 ⑥) — 이 파일은 그 위에 **제어 구조**만 얹는다.
 *)

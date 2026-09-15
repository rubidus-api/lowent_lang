(* LowentRC11SC.v — R7-(3) 의 **일반 방향**: 손으로 고른 반례 두 개에서 ∀실행으로.
 *
 * 왜 이 파일이 있나 — **문서가 스스로 과장했다.**
 *   `docs/manual/theory/16-what-is-not-proven.md` 초판이 "RC11 일반 약한 ordering — iGPS/Cosmo
 *   필요" 라고 적었다. 그런데 다시 보니 **세 문제가 섞여 있었다**:
 *     (a) 모델 내부 메타정리 — 모든 접근이 seq_cst ⟹ SC · 단조성    ⇒ **순수 Coq**
 *     (b) 프로그램 논리 — lock-free 알고리즘을 relaxed 로 검증        ⇒ 진짜 iGPS/Cosmo
 *     (c) 컴파일 매핑 — RC11 ordering → 기계어                        ⇒ 별도 문헌
 *   RFC-0018 §8-1 이 "iGPS/Cosmo 로" 미뤄 둔 것은 **DRF-SC** 였고, 그것은 LowentDRF.v 가
 *   순수 Coq 으로 이미 닫았다. **같은 문장을 다른 대상에 재사용한 것**이 그 오류였다.
 *   ⇒ 이 파일이 (a) 를 갚는다. 그리고 (a) 는 분리논리가 필요 없다 — **관계 그래프의 주장**이다.
 *
 * 무엇을 증명하나:
 *   ① **부분관계는 비순환성을 물려받는다.** 큰 그래프가 비순환이면 그 안의 작은 그래프도
 *      비순환이다. 이 한 줄이 아래 둘의 엔진이다.
 *   ② ★★ **모든 접근이 seq_cst 이면 `po ∪ rf ∪ mo ∪ fr` 가 비순환이다** (∀ 실행).
 *      그것이 공리적 모델에서 "이 실행은 SC 다" 의 표준 형태다. LowentRC11.v 는 이것을
 *      **저장 버퍼링 하나**에 대해 계산으로 보였다 — 여기서는 **모든 실행**에 대해 증명한다.
 *   ③ ★★ **단조성**: ordering 을 세게 하면 거동이 늘지 않는다. 정확히는 — 세게 한 실행이
 *      일관적이면 **약하게 한 같은 실행도 일관적이다**(그 역은 아니다).
 *      ⇒ 설계 결정 "기본값을 강하게, 약한 것은 명시 + 감사" 가 이 정리 위에 선다.
 *        지금까지 그 결정의 근거는 SB·MP **두 사례**였다.
 *
 * ★ 정직한 범위(이 파일이 **하지 않는** 것):
 *   ②는 "비순환" 까지다. *"비순환 ⟹ 그 순서를 잇는 **전순서가 존재한다**"* 는 마지막 한 걸음
 *   (순서 확장 원리 · 위상 정렬)은 **여기 없다**. 그것은 언어와 무관한 표준 사실이지만,
 *   **없는 것을 있는 척하지 않는다** — §4 에 무엇이 남았는지 정확히 적어 둔다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.
Require Import LowentRC11.

(* ── 1. 부분관계 — 이 파일의 엔진 ────────────────────────────── *)

(* R ⊆ S : R 의 모든 간선이 S 에도 있다. *)
Definition sub (R S : rel) : Prop := forall i j, has R i j = true -> has S i j = true.

Lemma has_app : forall R S i j,
  has (R ++ S) i j = has R i j || has S i j.
Proof. intros. unfold has. apply existsb_app. Qed.

(* ★ dedup 은 **집합으로서** 아무것도 바꾸지 않는다. (계산량만 바꾼다 — 그것이 목적이었다.)
   ★ 귀납 가설이 **모든 i j 에 대해** 있어야 한다: `then` 갈래에서 (fst p, snd p) 가 r 에
     있음을 써야 하는데, 그것은 지금 보는 i j 와 다른 짝이다. 그래서 intros 를 뒤로 미룬다. *)
(* ★ `has` 를 **펼치지 않는다.** 펼치면 귀납 가설(has 로 적혀 있다)과 모양이 어긋나
   `rewrite` 가 붙을 자리를 못 찾는다. 한 줄 보조정리로 껍질만 벗긴다. *)
Lemma has_cons : forall p R i j,
  has (p :: R) i j = (Nat.eqb (fst p) i && Nat.eqb (snd p) j) || has R i j.
Proof. reflexivity. Qed.

Lemma has_dedup : forall R i j, has (dedup R) i j = has R i j.
Proof.
  unfold dedup. intros R. induction R as [| p r IH]; intros i j; simpl; [ reflexivity | ].
  destruct (has (fold_right (fun q acc => if has acc (fst q) (snd q) then acc else q :: acc) [] r)
                (fst p) (snd p)) eqn:E.
  - (* p 가 이미 있다 — 넣지 않아도 같은 집합이다 *)
    (* ★ `simpl` 이 이미 `has (p :: r)` 의 껍질을 벗겨 놓았다 — has_cons 를 쓸 자리가 없다. *)
    rewrite (IH i j).
    destruct (Nat.eqb (fst p) i) eqn:Ei; destruct (Nat.eqb (snd p) j) eqn:Ej; simpl;
      try reflexivity.
    assert (Hi : i = fst p) by (symmetry; now apply Nat.eqb_eq).
    assert (Hj : j = snd p) by (symmetry; now apply Nat.eqb_eq).
    subst i j. rewrite <- (IH (fst p) (snd p)). exact E.
  - (* ★ 이쪽은 `has (p :: F)` 가 **접힌 채** 남는다(위 갈래와 모양이 다르다 — `if` 가
       reduce 를 막았다). 그래서 여기서만 껍질 벗기기가 필요하다. *)
    rewrite has_cons, (IH i j). reflexivity.
Qed.

Lemma sub_refl : forall R, sub R R.
Proof. intros R i j H; exact H. Qed.

(* R ⊆ S 이면 한 걸음 합성도 보존된다. *)
Lemma step_sub : forall R S, sub R S -> sub (step R) (step S).
Proof.
  intros R S Hsub i j H. unfold step in *.
  rewrite has_dedup in H. rewrite has_dedup.
  rewrite has_app in H. rewrite has_app.
  apply orb_true_iff in H. apply orb_true_iff.
  destruct H as [H | H]; [ left; apply Hsub; exact H | right ].
  (* 합성 간선: (i,k) ∈ R 이고 (k,j) ∈ R  ⟹  S 에서도 *)
  unfold has in H |- *. apply existsb_exists in H as [q [Hq Heq]].
  apply andb_true_iff in Heq as [Hi Hj].
  apply Nat.eqb_eq in Hi; apply Nat.eqb_eq in Hj.
  apply in_flat_map in Hq as [p [Hp Hq2]].
  apply in_flat_map in Hq2 as [q2 [Hq2 Hq3]].
  destruct (Nat.eqb (snd p) (fst q2)) eqn:Emid; [ | simpl in Hq3; contradiction ].
  simpl in Hq3. destruct Hq3 as [Hq3 | Hq3]; [ | contradiction ].
  subst q. simpl in Hi, Hj. subst i j.
  apply Nat.eqb_eq in Emid.
  (* p 와 q2 가 S 에 있다 *)
  assert (HpS : has S (fst p) (snd p) = true).
  { apply Hsub. unfold has. apply existsb_exists. exists p. split; [ exact Hp | ].
    now rewrite !Nat.eqb_refl. }
  assert (HqS : has S (fst q2) (snd q2) = true).
  { apply Hsub. unfold has. apply existsb_exists. exists q2. split; [ exact Hq2 | ].
    now rewrite !Nat.eqb_refl. }
  unfold has in HpS, HqS. apply existsb_exists in HpS as [p' [Hp' Ep']].
  apply existsb_exists in HqS as [q' [Hq' Eq']].
  apply andb_true_iff in Ep' as [Ep1 Ep2]. apply andb_true_iff in Eq' as [Eq1 Eq2].
  apply Nat.eqb_eq in Ep1; apply Nat.eqb_eq in Ep2.
  apply Nat.eqb_eq in Eq1; apply Nat.eqb_eq in Eq2.
  (* ★ 중간 마디가 맞물린다: snd p' = snd p = fst q2 = fst q'.
     (rewrite 사슬로 밀지 않고 **등식 하나로** 세운다 — 사슬은 어느 발이 먼저 맞는지에
      의존해서 부러진다. 실제로 부러졌다.) *)
  assert (Hm : Nat.eqb (snd p') (fst q') = true).
  { apply Nat.eqb_eq. rewrite Ep2, Eq1. exact Emid. }
  apply existsb_exists. exists (fst p', snd q'). split.
  - apply in_flat_map. exists p'. split; [ exact Hp' | ].
    apply in_flat_map. exists q'. split; [ exact Hq' | ].
    rewrite Hm. simpl. now left.
  - simpl. rewrite Ep1, Eq2. now rewrite !Nat.eqb_refl.
Qed.

Lemma closure_sub : forall n R S, sub R S -> sub (closure n R) (closure n S).
Proof.
  induction n as [| n IH]; intros R S Hsub; simpl; [ exact Hsub | ].
  apply IH. apply step_sub. exact Hsub.
Qed.

(* ★ 한 걸음은 **잃지 않는다** — R ⊆ step R. *)
Lemma step_incl : forall R i j, has R i j = true -> has (step R) i j = true.
Proof.
  intros R i j H. unfold step. rewrite has_dedup, has_app. now rewrite H.
Qed.

Lemma closure_incl : forall n R i j, has R i j = true -> has (closure n R) i j = true.
Proof.
  induction n as [| n IH]; intros R i j H; simpl; [ exact H | ].
  apply IH. apply step_incl. exact H.
Qed.

(* ★★ **비순환성은 부분관계로 물려받는다.** 이 파일의 엔진. *)
Lemma acyclic_sub : forall n R S,
  sub R S ->
  (forall i, has (closure n S) i i = false) ->
  (forall i, has (closure n R) i i = false).
Proof.
  intros n R S Hsub HS i.
  destruct (has (closure n R) i i) eqn:E; [ | reflexivity ].
  pose proof (closure_sub n R S Hsub i i E) as HinS.
  rewrite HS in HinS. discriminate.
Qed.

(* ── 2. ★★ 정리 — 모든 접근이 seq_cst 이면 그 실행은 SC 다 ───────────── *)

Definition all_sc (E : exec) : bool := forallb (fun e => is_sc (e_mode e)) E.

(* SC 의 표준 형태: **program order 와 통신 관계(rf ∪ mo ∪ fr)를 합쳐도 순환이 없다.**
   이것이 참이면 그 실행은 하나의 전역 순서로 늘어놓을 수 있다(§4 참조). *)
Definition sc_com_edges (E : exec) (rf : rfmap) (mo : momap) : rel :=
  flat_map (fun a => flat_map (fun b =>
     if po a b || eco rf mo a b then [(e_id a, e_id b)] else []) E) E.

Definition sc_com_acyclic (E : exec) (rf : rfmap) (mo : momap) : bool :=
  forallb (fun e => negb (has (closure (length E) (sc_com_edges E rf mo)) (e_id e) (e_id e))) E.

(* po ⊆ hb — 프로그램 순서는 언제나 happens-before 다. *)
Lemma po_in_hb : forall E rf a b,
  In a E -> In b E -> po a b = true -> hb E rf a b = true.
Proof.
  intros E rf a b Ha Hb Hpo. unfold hb, hb_rel.
  apply closure_incl. unfold has, edges. apply existsb_exists.
  exists (e_id a, e_id b). split.
  - apply in_flat_map. exists a. split; [ exact Ha | ].
    apply in_flat_map. exists b. split; [ exact Hb | ].
    unfold edge. rewrite Hpo. simpl. now left.
  - simpl. now rewrite !Nat.eqb_refl.
Qed.

(* 모든 접근이 seq_cst 이면 `po ∪ eco` 의 모든 간선은 psc 간선이다. *)
Lemma sc_com_sub_psc : forall E rf mo,
  all_sc E = true -> sub (sc_com_edges E rf mo) (psc_edges E rf mo).
Proof.
  intros E rf mo Hall i j H.
  unfold has, sc_com_edges in H. apply existsb_exists in H as [q [Hq Heq]].
  apply andb_true_iff in Heq as [Hi Hj].
  apply Nat.eqb_eq in Hi; apply Nat.eqb_eq in Hj.
  apply in_flat_map in Hq as [a [Ha Hq2]].
  apply in_flat_map in Hq2 as [b [Hb Hq3]].
  destruct (po a b || eco rf mo a b) eqn:Ecom; [ | simpl in Hq3; contradiction ].
  simpl in Hq3. destruct Hq3 as [Hq3 | Hq3]; [ | contradiction ].
  subst q. simpl in Hi, Hj. subst i j.
  (* psc_edge a b = is_sc a && is_sc b && (hb || eco) *)
  unfold has, psc_edges. apply existsb_exists.
  exists (e_id a, e_id b). split.
  - apply in_flat_map. exists a. split; [ exact Ha | ].
    apply in_flat_map. exists b. split; [ exact Hb | ].
    assert (Hsa : is_sc (e_mode a) = true).
    { unfold all_sc in Hall. rewrite forallb_forall in Hall. now apply Hall. }
    assert (Hsb : is_sc (e_mode b) = true).
    { unfold all_sc in Hall. rewrite forallb_forall in Hall. now apply Hall. }
    unfold psc_edge. rewrite Hsa, Hsb. simpl.
    apply orb_true_iff in Ecom. destruct Ecom as [Hpo | Heco].
    + rewrite (po_in_hb E rf a b Ha Hb Hpo). simpl. now left.
    + rewrite Heco, orb_true_r. simpl. now left.
  - simpl. now rewrite !Nat.eqb_refl.
Qed.

(* ★★★ **정리 (a)** — 모든 접근이 seq_cst 이고 그 실행이 RC11 일관적이면,
   `po ∪ rf ∪ mo ∪ fr` 에 **순환이 없다.** ∀ 실행 · ∀ rf · ∀ mo.

   ⇒ 11장의 주장("기본값이면 순서대로 사고해도 된다")이 이제 **저장 버퍼링 한 사례**가
     아니라 **모든 실행**에 대해 선다. *)
Theorem all_sc_is_sc : forall E rf mo,
  all_sc E = true ->
  consistent E rf mo = true ->
  sc_com_acyclic E rf mo = true.
Proof.
  intros E rf mo Hall Hcons.
  unfold consistent in Hcons.
  apply andb_true_iff in Hcons as [Hcons Hsc].
  unfold sc_acyclic in Hsc. rewrite forallb_forall in Hsc.
  unfold sc_com_acyclic. rewrite forallb_forall. intros e He.
  (* psc 가 비순환이므로, 그 부분관계인 sc_com 도 비순환이다 *)
  assert (HpscAcyc : forall i, In i (map e_id E) ->
                     has (psc_rel E rf mo) i i = false).
  { intros i Hi. apply in_map_iff in Hi as [x [Hx HxE]]. subst i.
    specialize (Hsc x HxE). now apply negb_true_iff in Hsc. }
  destruct (has (closure (length E) (sc_com_edges E rf mo)) (e_id e) (e_id e)) eqn:Ecyc;
    [ | reflexivity ].
  exfalso.
  pose proof (closure_sub (length E) _ _ (sc_com_sub_psc E rf mo Hall) _ _ Ecyc) as Hin.
  unfold psc_rel in HpscAcyc.
  rewrite (HpscAcyc (e_id e) (in_map e_id E e He)) in Hin. discriminate.
Qed.

(* ── 3. ★★ 단조성 — 세게 하면 거동이 늘지 않는다 ──────────────────── *)

(* mode 의 세기: relaxed ⊏ acquire/release ⊏ acq_rel ⊏ seq_cst.
   ★ "센 것" 의 뜻은 하나다 — **sw(동기화) 간선을 더 많이 만든다.** 그것을 그대로 적는다. *)
Definition stronger (m m' : mode) : bool :=
  (implb (reads_acq m') (reads_acq m)) && (implb (writes_rel m') (writes_rel m)).

Lemma stronger_refl : forall m, stronger m m = true.
Proof. destruct m; reflexivity. Qed.

(* SC 는 **모든 mode 보다 세다** — 그것이 기본값으로 옳은 이유의 형식적 내용이다. *)
Theorem sc_is_the_strongest : forall m, stronger SC m = true.
Proof. destruct m; reflexivity. Qed.

Theorem rlx_is_the_weakest : forall m, stronger m Rlx = true.
Proof. destruct m; reflexivity. Qed.

(* ── ★★★ 그런데 이 `stronger` 는 **세기 순서가 아니다** (2026-07-31) ───────────────
 *
 * 위 정의는 자기가 무엇을 재는지 정직하게 적어 두었다 — *"센 것의 뜻은 하나다: sw 간선을
 * 더 많이 만든다."* 문제는 RC11 의 **공리가 둘을 본다**는 것이다:
 *
 *     sw 간선  ← reads_acq · writes_rel  (이 정의가 재는 것)
 *     psc 공리 ← **is_sc**               (이 정의가 **안 재는** 것)
 *
 * 그래서 이런 일이 생긴다:
 *
 *     stronger AcqRel SC = true          ("AcqRel 이 SC 보다 세다")
 *
 * AcqRel 은 acquire·release 를 **둘 다** 갖고 있으므로 sw 자리에서는 SC 와 똑같다. 그러나
 * **psc 공리에 들어가지 않는다.** ⇒ AcqRel 은 SC 보다 **약하다** — sw 로는 안 보이는 축에서.
 * `LowentRC11Mono.v` 의 `old_order_is_not_a_strength_order` 가 그 반례를 계산한다
 * (SB 는 AcqRel 에서 **일관**이고 SC 에서 **불일관**이다).
 *
 * ★ 이것을 어떻게 찾았나: 유계 전수(`mono_ok`, 3 이벤트)를 **일반 정리로 올리려다** 걸렸다.
 *   전수 검사는 CoWR·CoRR 골격만 봤고 그 둘은 **psc 축을 건드리지 않는다** — 그래서
 *   조용히 통과했다. **증명이 검사가 못 보는 자리를 본다**(15장 ②의 그 짝).
 *
 * ⇒ 진짜 세기 순서는 **is_sc 도 함께** 요구한다. SC 만이 꼭대기가 된다. *)
Definition stronger_sc (m m' : mode) : bool :=
  stronger m m' && implb (is_sc m') (is_sc m).

Theorem sc_is_the_top : forall m, stronger_sc SC m = true.
Proof. destruct m; reflexivity. Qed.

(* ★ 그리고 **꼭대기는 SC 뿐이다** — 옛 순서가 놓친 바로 그 문장. *)
Theorem only_sc_is_above_sc : forall m, stronger_sc m SC = true -> m = SC.
Proof. destruct m; simpl; intro H; try discriminate; reflexivity. Qed.

Lemma stronger_sc_refl : forall m, stronger_sc m m = true.
Proof. destruct m; reflexivity. Qed.

Lemma stronger_sc_stronger : forall m m', stronger_sc m m' = true -> stronger m m' = true.
Proof. intros m m' H; apply andb_true_iff in H as [H _]; exact H. Qed.

Lemma stronger_sc_is_sc : forall m m',
  stronger_sc m m' = true -> is_sc m' = true -> is_sc m = true.
Proof.
  intros m m' H Hsc. apply andb_true_iff in H as [_ H]. rewrite Hsc in H. exact H.
Qed.

(* 같은 이벤트 열에서 mode 만 바꾼 것(같은 id·tid·idx·kind·loc·val). *)
Definition mode_weaken (E E' : exec) : Prop :=
  length E = length E' /\
  Forall2 (fun a b =>
     e_id a = e_id b /\ e_tid a = e_tid b /\ e_idx a = e_idx b /\
     e_kind a = e_kind b /\ e_loc a = e_loc b /\ e_val a = e_val b /\
     stronger (e_mode a) (e_mode b) = true) E E'.
  (* E 가 센 쪽, E' 가 약한 쪽 *)

(* ★ 약하게 하면 sw 간선이 **줄어든다** (없던 것이 생기지 않는다).
   ★★ **fence 가 들어오면서 이 정리의 범위를 좁혔다**(2026-07-30): `sw` 는 이제 네 갈래다
     (기본 + fence 세 모양). fence 갈래는 **다른 이벤트**(po/rf 로 이어진 쓰기·읽기)를 보므로,
     "이벤트 열이 mode 만 다르다" 는 가정이 **추가로** 필요하다. 그 가정을 슬쩍 끼워 넣는 대신
     정리를 **기본 갈래로 좁히고** 이름을 바꿨다 — `sw_base_weaken`.
     ⇒ fence 를 포함한 단조성은 `LowentRC11Sweep.v` 의 **유계 전수**(`mono_ok`)가 본다.
     ★ 정리의 이름이 그 범위를 말해야 한다. `sw_weaken` 이라고 두면 fence 까지 덮는 것처럼 읽힌다. *)
Lemma sw_base_weaken : forall rf a b a' b',
  e_id a = e_id a' -> e_id b = e_id b' ->
  e_tid a = e_tid a' -> e_tid b = e_tid b' ->
  e_kind a = e_kind a' -> e_kind b = e_kind b' ->
  stronger (e_mode a) (e_mode a') = true ->
  stronger (e_mode b) (e_mode b') = true ->
  sw_base rf a' b' = true -> sw_base rf a b = true.
Proof.
  intros rf a b a' b' Hia Hib Hta Htb Hka Hkb Hsa Hsb H.
  unfold sw_base in *. rewrite Hka, Hkb.
  destruct (e_kind a') eqn:Ea; destruct (e_kind b') eqn:Eb; try discriminate.
  apply andb_true_iff in H as [H Hrf].
  apply andb_true_iff in H as [Hw Hr].
  unfold stronger in Hsa, Hsb.
  apply andb_true_iff in Hsa as [_ Hwa]. apply andb_true_iff in Hsb as [Hrb _].
  rewrite Hw in Hwa. rewrite Hr in Hrb. simpl in Hwa, Hrb.
  rewrite Hwa, Hrb. simpl.
  (* rf_cross: 스레드가 다르고, 읽기의 rf 가 그 쓰기를 가리킨다 *)
  unfold rf_cross in *. rewrite Hta, Htb.
  (* ★ rf 조건은 **읽기의 id 로 찾고 쓰기의 id 와 견준다** — 둘 다 옮겨야 한다.
     `rewrite Hib` 만 하면 찾는 쪽만 맞고 견주는 쪽(e_id a vs e_id a')이 남는다. *)
  rewrite Hib, Hia. exact Hrf.
Qed.

(* ── 4. ★ 남은 한 걸음 — 정확히 무엇이 없나 ────────────────────────── *)

(* §2 의 `all_sc_is_sc` 는 **비순환**까지 증명한다. 공리적 메모리 모델에서 *"이 실행은 SC 다"* 를
   그렇게 정의하는 것이 표준이다(SC ⟺ acyclic(po ∪ com)). 그러나 사람이 실제로 기대는 문장은
   한 걸음 더 간다:

       "모든 접근을 **한 줄로 늘어놓을 수 있다.**"

   그 한 걸음이 **순서 확장 원리**(order extension · 유한이면 위상 정렬)다:

       유한 집합 위의 비순환 관계는 그것을 포함하는 **전순서로 확장된다.**

   이것은 **언어와 무관한 표준 사실**이고 이 파일에 **없다.** 없는 이유도 적어 둔다 —
   이 모델의 전이 폐포가 `closure n`(step 을 n 번, 한 걸음이 길이를 두 배로)이어서,
   "이 폐포가 도달가능성 전체와 같다"(=고정점에 닿았다)를 먼저 증명해야 하고 그것이
   *"임의의 걸음은 단순 경로로 줄어든다"* 라는 별개의 리스트 보조정리를 요구한다.
   ⇒ **손이 닿는다. 다만 이 패스에서 하지 않았다.** 그 차이를 흐리지 않는다.

   ★ 그래서 이 파일의 정직한 요약은 이것이다:
       · 기존:  기본값이면 안전하다 — **저장 버퍼링 한 사례**에 대해 계산으로 확인 (LowentRC11.v)
       · 지금:  기본값이면 `po ∪ rf ∪ mo ∪ fr` 에 **순환이 없다** — **∀ 실행 · ∀ rf · ∀ mo** (이 파일)
       · 남음:  비순환 ⟹ **전순서 존재** (표준 사실 · 미형식화)
       · 밖:    특정 lock-free 알고리즘을 relaxed 로 검증 (iGPS/Cosmo — 이 파일의 일이 아니다)

   그리고 §3 의 단조성은 **간선 수준**까지다(`sw_weaken`): 약하게 하면 동기화 간선이 줄어든다.
   그것을 "그러므로 consistent 가 보존된다" 로 잇는 것도 남은 일이다 — 다만 방향은 확인됐다:
   모든 공리(hb 비반사 · coherence · coh_acyclic · sc_acyclic)에서 **hb 간선이 늘면 더 어려워진다.**
   즉 약한 쪽이 통과하기 **쉽다.** 거동이 늘어나는 쪽이 약한 쪽이라는 설계 전제와 같은 방향이다. *)

(* LowentOrderExt.v — **순서 확장 원리**: 비순환 ⟹ 전순서가 존재한다.
 *
 * 왜 이 파일이 있나 — `LowentRC11SC.v` §4 가 남긴 **딱 한 걸음**이다.
 *   그 파일은 *"모든 접근이 seq_cst 이면 `po ∪ rf ∪ mo ∪ fr` 에 순환이 없다"* 를 ∀실행으로
 *   증명했다. 그런데 사람이 실제로 기대는 문장은 한 걸음 더 간다:
 *
 *       "모든 접근을 **한 줄로 늘어놓을 수 있다.**"
 *
 *   그 한 걸음이 순서 확장(order extension · 유한이면 위상 정렬)이고, **언어와 무관한
 *   표준 사실**이다. 표준이라고 미뤄 두는 것과 **실제로 있는** 것은 다르다 — 미뤄 두면
 *   그 자리는 영원히 "표준 사실이니까" 로 남는다. 그래서 여기서 짓는다.
 *
 * 어떻게 — **증명이 아니라 계산으로.** 전순서의 *존재*를 추상적으로 논증하는 대신,
 *   위상 정렬을 **함수로 짓고** 그 함수가 낸 목록이 진짜 선형 확장임을 **검증한다**:
 *
 *     topo R nodes = Some L  ⟹  L 은 nodes 의 재배열이고, R 의 모든 간선이 L 에서 앞→뒤다
 *
 *   ★ 이 방향(Some ⟹ 옳다)이 **쓸모 있는 반쪽**이다: 실제로 순서를 얻고 그것이 옳음을 안다.
 *     반대 방향(비순환 ⟹ Some)은 **비둘기집 논증**이 필요하고, §4 에 정확히 남겨 둔다.
 *     ⇒ 즉 이 파일은 그 한 걸음의 **절반**을 짓고, 나머지 절반의 모양을 정확히 적는다.
 *       "표준 사실" 이라는 낱말로 덮는 것보다 낫다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.
Require Import LowentRC11.
Require Import LowentRC11SC.

(* ── 1. 위상 정렬 — 원천(들어오는 간선이 없는 마디)을 반복해 뽑는다 ─────── *)

(* 마디 i 로 **들어오는** 간선이 남은 마디들 중에 있나? *)
Definition has_pred (R : rel) (rest : list nat) (i : nat) : bool :=
  existsb (fun j => has R j i) rest.

(* 원천 하나를 찾는다(없으면 None — 그러면 남은 그래프에 순환이 있다). *)
Fixpoint find_source (R : rel) (rest : list nat) (scan : list nat) : option nat :=
  match scan with
  | [] => None
  | i :: r => if has_pred R rest i then find_source R rest r else Some i
  end.

Fixpoint remove_one (i : nat) (l : list nat) : list nat :=
  match l with
  | [] => []
  | j :: r => if Nat.eqb i j then r else j :: remove_one i r
  end.

(* 위상 정렬. `fuel` 은 남은 마디 수 — 그래서 **항상 끝난다**(구조 재귀). *)
Fixpoint topo (fuel : nat) (R : rel) (rest : list nat) : option (list nat) :=
  match fuel with
  | 0 => match rest with [] => Some [] | _ => None end
  | S f =>
      match rest with
      | [] => Some []
      | _ => match find_source R rest rest with
             | None => None                     (* 원천이 없다 = 남은 부분에 순환이 있다 *)
             | Some i => match topo f R (remove_one i rest) with
                         | None => None
                         | Some L => Some (i :: L)
                         end
             end
      end
  end.

Definition topo_of (R : rel) (nodes : list nat) : option (list nat) :=
  topo (length nodes) R nodes.

(* ── 2. 선형 확장의 정의 — "R 의 모든 간선이 L 에서 앞→뒤" ─────────────── *)

Fixpoint pos (i : nat) (l : list nat) : option nat :=
  match l with
  | [] => None
  | j :: r => if Nat.eqb i j then Some 0
              else match pos i r with Some k => Some (S k) | None => None end
  end.

(* L 에서 i 가 j 보다 **앞**이다. *)
Definition before (L : list nat) (i j : nat) : bool :=
  match pos i L, pos j L with
  | Some a, Some b => Nat.ltb a b
  | _, _ => false
  end.

(* L 이 R 의 선형 확장인가 — R 의 모든 간선이 앞→뒤인가. *)
Definition linearises (L : list nat) (R : rel) : bool :=
  forallb (fun p => before L (fst p) (snd p)) R.

(* ── 3. ★★ 정리 — topo 가 낸 것은 **정말 선형 확장이다** ───────────────── *)

Lemma pos_remove_none : forall i l, ~ In i l -> pos i l = None.
Proof.
  induction l as [| j r IH]; simpl; intros H; [ reflexivity | ].
  destruct (Nat.eqb i j) eqn:E.
  - apply Nat.eqb_eq in E. exfalso. apply H. now left.
  - rewrite IH; [ reflexivity | ]. intro Hi. apply H. now right.
Qed.

Lemma in_remove_one : forall i j l, In j (remove_one i l) -> In j l.
Proof.
  induction l as [| a r IH]; simpl; intros H; [ contradiction | ].
  destruct (Nat.eqb i a) eqn:E.
  - now right.
  - simpl in H. destruct H as [H | H]; [ now left | right; now apply IH ].
Qed.

Lemma remove_one_not_in : forall i l, ~ In i l -> remove_one i l = l.
Proof.
  induction l as [| a r IH]; simpl; intros H; [ reflexivity | ].
  destruct (Nat.eqb i a) eqn:E.
  - apply Nat.eqb_eq in E. exfalso. apply H. now left.
  - rewrite IH; [ reflexivity | ]. intro Hi. apply H. now right.
Qed.

(* find_source 가 낸 마디는 scan 목록 안에 있다. *)
Lemma find_source_in : forall R rest scan s,
  find_source R rest scan = Some s -> In s scan.
Proof.
  intros R rest scan s. induction scan as [| b sc IH]; simpl; intros H; [ discriminate | ].
  destruct (has_pred R rest b) eqn:Eb.
  - right. now apply IH.
  - inversion H; subst. now left.
Qed.

(* topo 가 낸 목록의 원소는 전부 rest 에서 왔다. *)
Lemma topo_incl : forall f R rest L,
  topo f R rest = Some L -> forall i, In i L -> In i rest.
Proof.
  induction f as [| f IH]; intros R rest L H i Hi; simpl in H.
  - destruct rest; [ | discriminate ]. inversion H; subst. contradiction.
  - destruct rest as [| a r] eqn:Er.
    + inversion H; subst. contradiction.
    + destruct (find_source R (a :: r) (a :: r)) as [s |] eqn:Es; [ | discriminate ].
      destruct (topo f R (remove_one s (a :: r))) as [L' |] eqn:Et; [ | discriminate ].
      inversion H; subst L. simpl in Hi.
      destruct Hi as [Hi | Hi].
      * subst i. now apply (find_source_in R (a :: r) (a :: r)).
      * apply in_remove_one with (i := s). now apply (IH R _ L' Et).
Qed.

(* ★ find_source 가 s 를 냈다면, **남은 마디 중 s 로 들어오는 간선이 없다.** *)
Lemma find_source_no_pred : forall R rest scan s,
  find_source R rest scan = Some s -> has_pred R rest s = false.
Proof.
  intros R rest scan s. induction scan as [| b sc IH]; simpl; intros H; [ discriminate | ].
  destruct (has_pred R rest b) eqn:Eb; [ now apply IH | ].
  inversion H; subst. exact Eb.
Qed.

(* ★ 원천에는 남은 마디로부터 오는 간선이 **하나도** 없다 — 그 사실을 쓸 모양으로 만든다.
   (`existsb = false` 를 매번 손으로 뒤집으면 증명이 지저분해진다. 한 번만 뒤집는다.) *)
Lemma no_pred_no_edge : forall R rest s i,
  has_pred R rest s = false -> In i rest -> has R i s = false.
Proof.
  intros R rest s i Hnp Hin. destruct (has R i s) eqn:E; [ | reflexivity ].
  exfalso. unfold has_pred in Hnp.
  assert (Ht : existsb (fun j => has R j s) rest = true).
  { apply existsb_exists. exists i. split; [ exact Hin | exact E ]. }
  rewrite Hnp in Ht. discriminate.
Qed.

(* 목록 안에 있으면 자리가 있다. *)
Lemma pos_in : forall i L, In i L -> exists k, pos i L = Some k.
Proof.
  induction L as [| a r IH]; simpl; intros H; [ contradiction | ].
  destruct (Nat.eqb i a) eqn:E; [ now exists 0 | ].
  destruct H as [H | H].
  - subst a. rewrite Nat.eqb_refl in E. discriminate.
  - destruct (IH H) as [k Hk]. rewrite Hk. now exists (S k).
Qed.

(* ★★ 핵심 보조정리 — topo 가 낸 목록에서 **간선은 앞에서 뒤로만** 간다.
   왜 성립하나: 매 걸음 뽑는 마디 s 는 **남은 것들로부터 들어오는 간선이 없다.**
   그러므로 s 뒤에 올 어떤 마디도 s 로 가는 간선을 갖지 않는다 — 뒤→앞 간선이 없다. *)
Lemma topo_edges_forward : forall f R rest L,
  topo f R rest = Some L ->
  forall i j, has R i j = true -> In i L -> In j L -> before L i j = true.
Proof.
  induction f as [| f IH]; intros R rest L H i j Hij Hi Hj; simpl in H.
  - destruct rest; [ | discriminate ]. inversion H; subst. contradiction.
  - destruct rest as [| a r] eqn:Er.
    + inversion H; subst. contradiction.
    + destruct (find_source R (a :: r) (a :: r)) as [s |] eqn:Es; [ | discriminate ].
      destruct (topo f R (remove_one s (a :: r))) as [L' |] eqn:Et; [ | discriminate ].
      inversion H; subst L.
      pose proof (find_source_no_pred R (a :: r) (a :: r) s Es) as Hnp.
      (* s 로 **들어오는** 간선은 남은 마디 어디에서도 오지 않는다. 그것이 전부다. *)
      simpl in Hi, Hj.
      destruct Hj as [Hj | Hj].
      * (* j = s : 불가능하다 — i 가 (a::r) 안이므로 i→s 간선이 없다 *)
        subst j. exfalso.
        assert (Hin : In i (a :: r)).
        { destruct Hi as [Hi | Hi].
          - subst i. now apply (find_source_in R (a :: r) (a :: r)).
          - apply in_remove_one with (i := s). now apply (topo_incl f R _ L' Et). }
        rewrite (no_pred_no_edge R (a :: r) s i Hnp Hin) in Hij. discriminate.
      * (* j 는 L' 안에 있다 *)
        destruct (pos_in j L' Hj) as [kj Hkj].
        destruct Hi as [Hi | Hi].
        -- (* i = s — 맨 앞이므로 0 < S kj *)
           subst i. unfold before. simpl. rewrite Nat.eqb_refl.
           destruct (Nat.eqb j s) eqn:Ejs.
           ++ (* j = s 이면서 j ∈ L' — pos 가 0 이 되어 0 < 0 이 아니다.
                 그런데 그 경우는 s→s 간선이고, 위와 같은 이유로 불가능하다. *)
              exfalso. apply Nat.eqb_eq in Ejs. subst j.
              assert (Hin : In s (a :: r)) by (now apply (find_source_in R (a :: r) (a :: r))).
              rewrite (no_pred_no_edge R (a :: r) s s Hnp Hin) in Hij. discriminate.
           ++ rewrite Hkj. simpl. reflexivity.
        -- (* 둘 다 L' 안 — 귀납 가설을 쓰고, s 를 앞에 붙여도 순서가 유지됨을 본다 *)
           destruct (pos_in i L' Hi) as [ki Hki].
           pose proof (IH R (remove_one s (a :: r)) L' Et i j Hij Hi Hj) as Hb.
           unfold before in Hb. rewrite Hki, Hkj in Hb.
           unfold before. simpl.
           destruct (Nat.eqb i s) eqn:Eis.
           ++ (* i = s 이면서 i ∈ L' : 자리 0 이 되어 여전히 앞이다 *)
              destruct (Nat.eqb j s) eqn:Ejs.
              ** (* i = j = s 인데 i→j 간선이 있다 = 자기 고리. Hb 가 자리 < 자리 를 말하므로 모순.
                    ★ `pos` 는 함수다 — 같은 마디의 자리는 하나다. 그 사실을 명시해야 lia 가 본다. *)
                 exfalso. apply Nat.eqb_eq in Eis; apply Nat.eqb_eq in Ejs.
                 subst i j. rewrite Hki in Hkj. inversion Hkj; subst kj.
                 apply Nat.ltb_lt in Hb. lia.
              ** rewrite Hkj. simpl. reflexivity.
           ++ destruct (Nat.eqb j s) eqn:Ejs.
              ** (* j = s 이면서 j ∈ L' : 자리 0 인데 i 는 뒤 — Hb 와 어긋난다 *)
                 exfalso. apply Nat.eqb_eq in Ejs. subst j.
                 assert (Hin : In i (a :: r)).
                 { apply in_remove_one with (i := s). now apply (topo_incl f R _ L' Et). }
                 rewrite (no_pred_no_edge R (a :: r) s i Hnp Hin) in Hij. discriminate.
              ** rewrite Hki, Hkj. simpl. exact Hb.
Qed.

(* ★★★ 정리 — **topo 가 성공하면 그것은 선형 확장이다.** (∀ R · ∀ nodes)
   `linearises L R` 는 "R 의 모든 간선이 L 에서 앞→뒤" 를 **판정 함수**로 적은 것이다. *)
Theorem topo_linearises : forall R nodes L,
  topo_of R nodes = Some L ->
  (forall i j, has R i j = true -> In i L -> In j L -> before L i j = true).
Proof.
  intros R nodes L H. unfold topo_of in H.
  now apply (topo_edges_forward (length nodes) R nodes L H).
Qed.

(* ── 4. ★★ RC11 과 잇는다 — "한 줄로 늘어놓을 수 있다" ─────────────────── *)

(* 실행의 마디 = 이벤트 id. *)
Definition sc_order (E : exec) (rf : rfmap) (mo : momap) : option (list nat) :=
  topo_of (sc_com_edges E rf mo) (map e_id E).

(* ★★★ **이것이 11장이 실제로 주장하는 문장이다.**
   모든 접근이 seq_cst 이고 실행이 RC11 일관적이고 위상 정렬이 성공하면 —
   그 순서 L 에서 `po ∪ rf ∪ mo ∪ fr` 의 **모든 간선이 앞에서 뒤로** 간다.
   즉 **모든 접근을 한 줄로 늘어놓았고, 그 줄이 프로그램 순서와 통신 순서를 전부 지킨다.**

   ★ 남은 조건이 하나 있다: "위상 정렬이 성공하면". 그것을 없애는 것이 §5 다. *)
Theorem sc_witness_orders_everything : forall E rf mo L,
  all_sc E = true ->
  consistent E rf mo = true ->
  sc_order E rf mo = Some L ->
  forall i j, has (sc_com_edges E rf mo) i j = true ->
              In i L -> In j L -> before L i j = true.
Proof.
  intros E rf mo L _ _ Htopo i j Hij Hi Hj.
  now apply (topo_linearises (sc_com_edges E rf mo) (map e_id E) L Htopo).
Qed.

(* ★ 실제로 성공하는지 계산으로 확인한다 — 모델이 공허하지 않다는 증거.
   SB 를 **seq_cst 로 두고 좋은 결과**(T1 이 T2 의 쓰기를 본다)를 준 실행이
   LowentRC11.v 에 있다(SB_sc_ok). 그것에 순서가 실제로 붙는다. *)
Definition sb_ok_order := sc_order SB_sc_ok rf_ok mo_ok.

Theorem the_good_sb_execution_has_an_order : exists L, sb_ok_order = Some L.
Proof. unfold sb_ok_order. vm_compute. now exists [0; 1; 2; 4; 3; 5]. Qed.

(* ── 5. ★ 여기까지가 절반이었다 — 그리고 §6~§8 에서 나머지를 갚았다 ─────────
 *
 * 이 절은 원래 *"비순환 ⟹ topo 성공"* 이 **없다**고 적고, 그 모양(비둘기집 + 걸음 반감)을
 * 남겨 두었다. 그런데 다시 보니 **비둘기집이 필요 없었다** — §6 의 조상 수 논증이 더 짧다.
 * ⇒ §6 `source_exists` · §7 `topo_succeeds` · §8 `sc_order_total` 이 그것을 갚는다.
 *   이 절은 **무엇이 바뀌었는지 남기기 위해** 남겨 둔다(지우면 그 교훈도 사라진다):
 *
 *     "표준 사실이니까 미뤄 둔다" 와 "그 표준 사실의 증명이 실제로 무엇을 요구하나" 는
 *     다른 물음이다. 두 번째를 물었을 때 **요구가 줄었다.**
 *
 * ★ 그리고 남은 조건은 **판정 가능한 것 하나**다: 폐포가 **안정**한가(`stable_on`).
 *   증명할 수 없던 것(전이성)을 **검사할 수 있는 것**으로 바꾼 것이 이 설계의 요점이다.
 *)

(* ── 6. ★★ 완전성 — **비순환 + 폐포 안정 ⟹ topo 는 반드시 성공한다** ─────────
 *
 * §5 가 남긴 것을 갚는다. 그런데 §5 가 예상한 길(비둘기집 + 걸음 반감)로 가지 않는다 —
 * **더 짧은 길이 있다:**
 *
 *     조상(자기에게 도달하는 마디)의 **수가 가장 적은** 마디에는 선행자가 없다.
 *
 * 왜? 선행자 p 가 있으면 p 의 조상 전부가 나의 조상이고(전이성) **거기에 p 자신이 더해진다.**
 * 그런데 p 는 p 자신의 조상이 아니다(비순환). ⇒ 조상 수가 **엄격히** 늘어난다.
 * 즉 최소인 마디가 선행자를 가지면 더 작은 것이 있다는 말이므로 모순이다. ∎
 *
 * ★ 전이성을 어디서 얻나 — 그것이 §5 를 막고 있던 것이었다(`closure n` 이 고정점에 닿았는지).
 *   여기서는 **묻지 않고 검사한다**: `stable_on nodes T`(= `step T` 가 T 와 같다)는 **판정 가능**하고,
 *   그것이 참이면 전이성이 **한 줄로** 따라온다(step 은 T∘T 를 담으므로).
 *   ⇒ 비둘기집도, 걸음 반감도 필요 없다. **증명 못 하는 것을 검사 가능한 것으로 바꾼다.**
 *)

(* T 가 (nodes 위에서) 한 걸음 더 가도 자라지 않는다 = 고정점이다. *)
Definition stable_on (nodes : list nat) (T : rel) : bool :=
  forallb (fun i => forallb (fun j => Bool.eqb (has (step T) i j) (has T i j)) nodes) nodes.

(* ★ 안정하면 **전이적**이다 — step 이 합성을 담으므로. *)
Lemma stable_transitive : forall nodes T i j k,
  stable_on nodes T = true ->
  In i nodes -> In j nodes -> In k nodes ->
  has T i j = true -> has T j k = true -> has T i k = true.
Proof.
  intros nodes T i j k Hst Hi Hj Hk Hij Hjk.
  (* step T 는 T ∘ T 를 담는다 *)
  assert (Hstep : has (step T) i k = true).
  { unfold step. rewrite has_dedup, has_app. apply orb_true_iff. right.
    unfold has in Hij, Hjk |- *.
    apply existsb_exists in Hij as [p [Hp Ep]]. apply existsb_exists in Hjk as [q [Hq Eq]].
    apply andb_true_iff in Ep as [Ep1 Ep2]. apply andb_true_iff in Eq as [Eq1 Eq2].
    apply Nat.eqb_eq in Ep1; apply Nat.eqb_eq in Ep2.
    apply Nat.eqb_eq in Eq1; apply Nat.eqb_eq in Eq2.
    apply existsb_exists. exists (fst p, snd q). split.
    - apply in_flat_map. exists p. split; [ exact Hp | ].
      apply in_flat_map. exists q. split; [ exact Hq | ].
      assert (Hm : Nat.eqb (snd p) (fst q) = true).
      { apply Nat.eqb_eq. rewrite Ep2, Eq1. reflexivity. }
      rewrite Hm. simpl. now left.
    - simpl. rewrite Ep1, Eq2. now rewrite !Nat.eqb_refl. }
  (* 안정성이 step T 를 T 로 되돌린다 *)
  unfold stable_on in Hst. rewrite forallb_forall in Hst.
  specialize (Hst i Hi). rewrite forallb_forall in Hst. specialize (Hst k Hk).
  apply Bool.eqb_prop in Hst. rewrite Hstep in Hst. now rewrite <- Hst.
Qed.

(* 조상 목록과 그 수. *)
Definition anc (T : rel) (nodes : list nat) (i : nat) : list nat :=
  filter (fun j => has T j i) nodes.
Definition anc_count (T : rel) (nodes : list nat) (i : nat) : nat :=
  length (anc T nodes i).

Lemma remove_length_lt_nat : forall (x : nat) l,
  In x l -> length (remove Nat.eq_dec x l) < length l.
Proof.
  induction l as [| a r IH]; simpl; intros H; [ contradiction | ].
  destruct (Nat.eq_dec x a) as [E | E].
  - subst a. assert (length (remove Nat.eq_dec x r) <= length r) as Hle.
    { clear. induction r as [| b s IH]; simpl; [ lia | ].
      destruct (Nat.eq_dec x b); simpl; lia. }
    lia.
  - simpl. destruct H as [H | H]; [ subst a; contradiction | ].
    specialize (IH H). lia.
Qed.

(* ★★ 핵심 — 간선을 거스르면 조상 수가 **엄격히** 늘어난다. *)
Lemma anc_count_lt : forall nodes T p i,
  NoDup nodes -> stable_on nodes T = true ->
  (forall q, In q nodes -> has T q q = false) ->        (* 비순환 *)
  In p nodes -> In i nodes -> has T p i = true ->
  anc_count T nodes p < anc_count T nodes i.
Proof.
  intros nodes T p i Hnd Hst Hacyc Hp Hi Hpi.
  unfold anc_count, anc.
  (* ① p 의 조상은 i 의 조상이다(전이성) *)
  assert (Hsub : incl (filter (fun j => has T j p) nodes)
                      (remove Nat.eq_dec p (filter (fun j => has T j i) nodes))).
  { intros q Hq. apply filter_In in Hq as [Hqn Hqp].
    apply in_in_remove.
    - (* q ≠ p — 아니면 has T p p = true 가 되어 비순환에 어긋난다 *)
      intro Heq. subst q. rewrite (Hacyc p Hp) in Hqp. discriminate.
    - apply filter_In. split; [ exact Hqn | ].
      now apply (stable_transitive nodes T q p i Hst Hqn Hp Hi Hqp Hpi). }
  (* ② p 자신은 i 의 조상이다 *)
  assert (Hin : In p (filter (fun j => has T j i) nodes)).
  { apply filter_In. split; [ exact Hp | exact Hpi ]. }
  (* ③ 길이 비교 *)
  pose proof (NoDup_incl_length (NoDup_filter (fun j => has T j p) Hnd) Hsub) as Hle.
  pose proof (remove_length_lt_nat p (filter (fun j => has T j i) nodes) Hin) as Hlt.
  lia.
Qed.

(* 조상 수가 최소인 마디를 고른다. *)
Fixpoint argmin (f : nat -> nat) (l : list nat) : option nat :=
  match l with
  | [] => None
  | x :: r => match argmin f r with
              | None => Some x
              | Some y => if Nat.leb (f x) (f y) then Some x else Some y
              end
  end.

Lemma argmin_in : forall f l x, argmin f l = Some x -> In x l.
Proof.
  induction l as [| a r IH]; simpl; intros x H; [ discriminate | ].
  destruct (argmin f r) as [y |] eqn:Er.
  - destruct (Nat.leb (f a) (f y)); inversion H; subst;
      [ now left | right; now apply IH ].
  - inversion H; subst. now left.
Qed.

(* argmin 이 None 을 내는 것은 목록이 빈 것뿐이다. *)
Lemma argmin_none : forall f l, argmin f l = None -> l = [].
Proof.
  intros f l H. destruct l as [| a r]; [ reflexivity | ].
  simpl in H. destruct (argmin f r) as [y |]; [ destruct (Nat.leb (f a) (f y)) | ]; discriminate.
Qed.

Lemma argmin_min : forall f l x, argmin f l = Some x -> forall y, In y l -> f x <= f y.
Proof.
  induction l as [| a r IH]; simpl; intros x H y Hy; [ contradiction | ].
  destruct (argmin f r) as [z |] eqn:Er.
  - destruct (Nat.leb (f a) (f z)) eqn:Ele; inversion H; subst.
    + apply Nat.leb_le in Ele. destruct Hy as [Hy | Hy]; [ subst y; lia | ].
      (* ★ `destruct … eqn:Er` 이 IH 안의 `argmin f r` 을 이미 `Some z` 로 바꿔 놓았다 —
         그래서 Er 을 다시 넘기면 형이 안 맞는다. `eq_refl` 이 맞는 증거다. *)
      pose proof (IH z eq_refl y Hy). lia.
    + apply Nat.leb_gt in Ele. destruct Hy as [Hy | Hy]; [ subst y; lia | ].
      now apply (IH x eq_refl y Hy).
  - inversion H; subst. destruct Hy as [Hy | Hy]; [ subst y; lia | ].
    (* argmin 이 None 이면 r = [] 이므로 In y r 은 불가능하다 *)
    apply argmin_none in Er. subst r. contradiction.
Qed.

Lemma argmin_some : forall f l, l <> [] -> exists x, argmin f l = Some x.
Proof.
  intros f l H. destruct l as [| a r]; [ contradiction | ].
  simpl. destruct (argmin f r) as [y |].
  - destruct (Nat.leb (f a) (f y)); [ now exists a | now exists y ].
  - now exists a.
Qed.

(* ★ 안정성은 **부분집합으로 물려받는다** — 더 적은 짝을 보므로.
   (처음엔 이것을 `topo_succeeds` 의 가정으로 받았다. 가정으로 두면 쓰는 쪽이 그것을 증명해야
    하고, 그러면 정리가 실제보다 약해 보인다. 보조정리로 내리는 것이 옳다.) *)
Lemma stable_on_incl : forall sub nodes T,
  incl sub nodes -> stable_on nodes T = true -> stable_on sub T = true.
Proof.
  intros sub nodes T Hi Hst. unfold stable_on in *.
  rewrite forallb_forall. intros i Hin. rewrite forallb_forall. intros j Hjn.
  rewrite forallb_forall in Hst. specialize (Hst i (Hi i Hin)).
  rewrite forallb_forall in Hst. now apply (Hst j (Hi j Hjn)).
Qed.

(* ★★★ **원천은 반드시 있다.** (비순환 + 안정 ⟹ 선행자 없는 마디가 존재) *)
Theorem source_exists : forall nodes T,
  NoDup nodes -> stable_on nodes T = true ->
  (forall q, In q nodes -> has T q q = false) ->
  nodes <> [] ->
  exists i, In i nodes /\ (forall j, In j nodes -> has T j i = false).
Proof.
  intros nodes T Hnd Hst Hacyc Hne.
  destruct (argmin_some (anc_count T nodes) nodes Hne) as [i Hi].
  exists i. split; [ now apply (argmin_in _ _ _ Hi) | ].
  intros j Hj. destruct (has T j i) eqn:E; [ | reflexivity ].
  exfalso.
  pose proof (anc_count_lt nodes T j i Hnd Hst Hacyc Hj (argmin_in _ _ _ Hi) E) as Hlt.
  pose proof (argmin_min (anc_count T nodes) nodes i Hi j Hj) as Hle.
  lia.
Qed.

(* ── 7. ★★★ topo 는 실패하지 않는다 ─────────────────────────────────── *)

(* 원천이 존재하면 find_source 는 그것을(또는 다른 원천을) 찾는다. *)
Lemma find_source_some : forall R rest,
  (exists i, In i rest /\ (forall j, In j rest -> has R j i = false)) ->
  exists s, find_source R rest rest = Some s.
Proof.
  intros R rest [i [Hi Hno]].
  (* scan 을 따라가며: 아직 못 찾았으면 남은 scan 에 i 가 있다 *)
  assert (Hgen : forall scan, In i scan ->
                 exists s, find_source R rest scan = Some s).
  { induction scan as [| b sc IH]; simpl; intros Hin; [ contradiction | ].
    destruct (has_pred R rest b) eqn:Eb.
    - destruct Hin as [Hin | Hin].
      + (* b = i 인데 선행자가 있다? — Hno 가 그것을 금지한다 *)
        subst b. exfalso. unfold has_pred in Eb.
        apply existsb_exists in Eb as [j [Hj Hji]].
        rewrite (Hno j Hj) in Hji. discriminate.
      + now apply IH.
    - now exists b. }
  now apply Hgen.
Qed.

Lemma remove_one_length : forall i l, In i l -> length (remove_one i l) = pred (length l).
Proof.
  induction l as [| a r IH]; simpl; intros H; [ contradiction | ].
  destruct (Nat.eqb i a) eqn:E; [ reflexivity | ].
  simpl. destruct H as [H | H].
  - subst a. rewrite Nat.eqb_refl in E. discriminate.
  - rewrite (IH H). destruct r; [ contradiction | reflexivity ].
Qed.

Lemma remove_one_nodup : forall i l, NoDup l -> NoDup (remove_one i l).
Proof.
  induction l as [| a r IH]; simpl; intros H; [ constructor | ].
  inversion H as [| x xs Hnin Hnd]; subst.
  destruct (Nat.eqb i a); [ exact Hnd | ].
  constructor; [ | now apply IH ].
  intro Hin. apply Hnin. now apply (in_remove_one i).
Qed.

(* ★★★ **정리 — 비순환 + 안정이면 topo 가 반드시 성공한다.**
   §5 가 "남았다" 고 적은 것이 이것이다. 비둘기집 없이 갚았다 — 조상 수 논증으로. *)
Theorem topo_succeeds : forall n R rest,
  length rest <= n ->
  NoDup rest ->
  stable_on rest R = true ->
  (forall q, In q rest -> has R q q = false) ->
  exists L, topo n R rest = Some L.
Proof.
  induction n as [| n IH]; intros R rest Hlen Hnd Hst Hacyc; simpl.
  - destruct rest as [| a r]; [ now exists [] | simpl in Hlen; lia ].
  - destruct rest as [| a r] eqn:Er; [ now exists [] | ].
    (* 원천이 존재한다 *)
    assert (Hne : (a :: r) <> []) by (intro Hc; discriminate Hc).
    destruct (source_exists (a :: r) R Hnd Hst Hacyc Hne) as [i [Hi Hno]].
    destruct (find_source_some R (a :: r) (ex_intro _ i (conj Hi Hno))) as [s Hs].
    rewrite Hs.
    (* 남은 것에 대해 귀납 *)
    assert (Hins : In s (a :: r)) by (now apply (find_source_in R (a :: r) (a :: r))).
    destruct (IH R (remove_one s (a :: r))) as [L HL].
    + rewrite remove_one_length by exact Hins. simpl in Hlen. simpl. lia.
    + now apply remove_one_nodup.
    + (* 안정성은 부분집합으로 내려간다 *)
      apply (stable_on_incl _ (a :: r)); [ | exact Hst ].
      intros q Hq. now apply (in_remove_one s).
    + intros q Hq. apply Hacyc. now apply (in_remove_one s).
    + rewrite HL. now exists (s :: L).
Qed.

(* ── 8. ★★★ RC11 과 잇는 완성형 — 순서가 **항상** 나온다 ─────────────────── *)

(* ★ topo 는 **폐포**에 대해 돌려야 한다. 원 간선 R 은 전이적이 아니므로 §6 의 조상 수
   논증이 적용되지 않는다. 폐포에 대해 순서를 얻으면 R ⊆ 폐포 이므로 R 도 자동으로 지켜진다. *)
Definition sc_T (E : exec) (rf : rfmap) (mo : momap) : rel :=
  closure (length E) (sc_com_edges E rf mo).

Definition sc_order_c (E : exec) (rf : rfmap) (mo : momap) : option (list nat) :=
  topo_of (sc_T E rf mo) (map e_id E).

(* ★★★ **정리 — 모든 접근이 seq_cst 이면 전역 순서가 존재한다.**
   조건 둘: 이벤트 id 가 서로 다르고(`wf` 가 주는 것), 폐포가 **안정**하다(판정 가능).
   결론: 순서가 **나오고**, 그 순서에서 `po ∪ rf ∪ mo ∪ fr` 의 모든 간선이 앞→뒤다.

   ⇒ 11장이 실제로 주장하는 문장이 이제 **전부** 기계 위에 있다:
     "기본값이면 모든 접근을 한 줄로 늘어놓을 수 있고, 그 줄이 프로그램 순서와 통신 순서를
      전부 지킨다." *)
Theorem sc_order_total : forall E rf mo,
  NoDup (map e_id E) ->
  all_sc E = true ->
  consistent E rf mo = true ->
  stable_on (map e_id E) (sc_T E rf mo) = true ->
  exists L, sc_order_c E rf mo = Some L /\
            (forall i j, has (sc_com_edges E rf mo) i j = true ->
                         In i L -> In j L -> before L i j = true).
Proof.
  intros E rf mo Hnd Hall Hcons Hst.
  (* ① 비순환 — all_sc_is_sc 가 준다 *)
  assert (Hacyc : forall q, In q (map e_id E) -> has (sc_T E rf mo) q q = false).
  { intros q Hq. pose proof (all_sc_is_sc E rf mo Hall Hcons) as Hsc.
    unfold sc_com_acyclic in Hsc. rewrite forallb_forall in Hsc.
    apply in_map_iff in Hq as [e [He HeE]]. subst q.
    specialize (Hsc e HeE). apply negb_true_iff in Hsc. exact Hsc. }
  (* ② 원천 논증으로 topo 가 성공한다 *)
  destruct (topo_succeeds (length (map e_id E)) (sc_T E rf mo) (map e_id E)
                          (Nat.le_refl _) Hnd Hst Hacyc) as [L HL].
  exists L. split; [ exact HL | ].
  intros i j Hij Hi Hj.
  (* ③ 원 간선은 폐포 안에 있으므로, 폐포에 대한 선형 확장이 그것도 지킨다 *)
  apply (topo_linearises (sc_T E rf mo) (map e_id E) L HL); [ | exact Hi | exact Hj ].
  unfold sc_T. now apply closure_incl.
Qed.

(* ★ 조건이 실제로 만족되는지 **계산으로** 확인한다 — 좋은 SB 실행에서. *)
Theorem the_good_sb_satisfies_the_side_conditions :
  stable_on (map e_id SB_sc_ok) (sc_T SB_sc_ok rf_ok mo_ok) = true.
Proof. vm_compute. reflexivity. Qed.

Theorem the_good_sb_has_a_total_order :
  exists L, sc_order_c SB_sc_ok rf_ok mo_ok = Some L.
Proof. unfold sc_order_c. vm_compute. now exists [0; 1; 2; 4; 3; 5]. Qed.

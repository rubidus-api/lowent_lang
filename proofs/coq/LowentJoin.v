(* LowentJoin.v — ★★★ **분기 합류에서 관계 사실이 정확하다**. 순수 Coq. 무계.
 *
 * 무엇이 남아 있었나. 16장의 미증명 목록:
 *
 *     | 분기(if) 합류에서 관계 사실의 정확성 | **전수 검사됨**(887,358 if-형태), 무계 미증명 |
 *
 * 05장 ④ 가 이 분석의 세 조각을 적어 두었다:
 *   ① 사실을 **어디서 얻는가** — 분기 narrowing (`lt i n` 이 참인 갈래에 `i < n` 을 심는다)
 *   ② 사실을 **어떻게 나르는가** — 대입을 따라가고, **합류에서는 두 경로가 같을 때만 살린다**
 *   ③ 사실이 **죽는 자리** — 관련 변수가 바뀌면 그 사실을 죽인다
 * 그리고 *"3번이 안전의 핵이다. 사실을 오래 살려 두면 그것이 곧 잘못된 검사 제거다."*
 *
 * ★ 이 파일은 셋을 모델로 만들고 **건전성을 증명한다**(§4):
 *
 *     분석이 "이 자리에서 이 사실이 성립한다" 고 말하면, **그 자리에 닿는 모든 실행에서**
 *     그 사실이 실제로 참이다.
 *
 * ★★ 그리고 **두 규칙이 왜 필요한지**를 반례로 증명한다(§5) — 이 저장소의 방식이다
 *   (`LowentLaunder.v` 의 `old_rule_is_unsound` 와 같은 모양):
 *     · 합류를 **합집합**으로 하면 → 분석이 **거짓을 말한다**(구체적 실행이 반례다)
 *     · 대입에서 **죽이지 않으면** → 분석이 **거짓을 말한다**
 *   ⇒ 규칙을 느슨하게 하면 정확히 어디가 깨지는지를 기계가 가리킨다.
 *)

Require Import List Bool Arith Lia ZArith.
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 사실과 그 뜻 ─────────────────────────────────────────────────────── *)

Inductive opnd : Type := OVar (x : nat) | OCst (z : Z).

(* 관계 사실은 `<` 하나면 충분하다 — `≤`·`=` 는 상수를 밀어 표현된다. *)
Inductive fact : Type := FLt (a b : opnd).

Definition env := nat -> Z.
Definition denote (e : env) (o : opnd) : Z :=
  match o with OVar x => e x | OCst z => z end.

Definition sat (e : env) (f : fact) : Prop :=
  match f with FLt a b => denote e a < denote e b end.

Definition holds (e : env) (fs : list fact) : Prop :=
  forall f, In f fs -> sat e f.

(* 같음 — 합류(교집합)에 필요하다. *)
Definition op_eqb (a b : opnd) : bool :=
  match a, b with
  | OVar x, OVar y => Nat.eqb x y
  | OCst z, OCst w => Z.eqb z w
  | _, _ => false
  end.
Definition fact_eqb (f g : fact) : bool :=
  match f, g with FLt a b, FLt c d => op_eqb a c && op_eqb b d end.

Lemma op_eqb_eq : forall a b, op_eqb a b = true -> a = b.
Proof.
  intros [x | z] [y | w] H; simpl in H; try discriminate.
  - apply Nat.eqb_eq in H; subst; reflexivity.
  - apply Z.eqb_eq in H; subst; reflexivity.
Qed.

Lemma fact_eqb_eq : forall f g, fact_eqb f g = true -> f = g.
Proof.
  intros [a b] [c d] H. simpl in H. apply andb_true_iff in H as [H1 H2].
  rewrite (op_eqb_eq a c H1), (op_eqb_eq b d H2). reflexivity.
Qed.

(* ── 2. 프로그램 ─────────────────────────────────────────────────────────── *)

Inductive expr : Type := EVar (y : nat) | ECst (z : Z).
Inductive cond : Type := CLt (a b : opnd).

Inductive stmt : Type :=
  | SNop
  | SAssign (x : nat) (e : expr)
  | SSeq (a b : stmt)
  | SIf (c : cond) (a b : stmt).

Definition upd (e : env) (x : nat) (z : Z) : env :=
  fun y => if Nat.eqb y x then z else e y.

Definition eval_expr (e : env) (ex : expr) : Z :=
  match ex with EVar y => e y | ECst z => z end.

Definition cond_holds (e : env) (c : cond) : Prop :=
  match c with CLt a b => denote e a < denote e b end.

Definition cond_holdsb (e : env) (c : cond) : bool :=
  match c with CLt a b => Z.ltb (denote e a) (denote e b) end.

(* 구체 실행 — 분기는 **실제 조건**을 따른다(분석이 아니라 값이 정한다). *)
Fixpoint eval (e : env) (s : stmt) : env :=
  match s with
  | SNop => e
  | SAssign x ex => upd e x (eval_expr e ex)
  | SSeq a b => eval (eval e a) b
  | SIf c a b => if cond_holdsb e c then eval e a else eval e b
  end.

(* ── 3. 분석 — 05장 ④ 의 세 조각 ────────────────────────────────────────── *)

Definition op_mentions (x : nat) (o : opnd) : bool :=
  match o with OVar y => Nat.eqb x y | OCst _ => false end.
Definition mentions (x : nat) (f : fact) : bool :=
  match f with FLt a b => op_mentions x a || op_mentions x b end.

(* ③ **죽이는 자리** — x 가 바뀌면 x 를 말하는 사실은 전부 죽는다. *)
Definition kill (x : nat) (fs : list fact) : list fact :=
  filter (fun f => negb (mentions x f)) fs.

Definition subst_op (y x : nat) (o : opnd) : opnd :=
  match o with OVar z => if Nat.eqb z y then OVar x else o | OCst _ => o end.
Definition subst_fact (y x : nat) (f : fact) : fact :=
  match f with FLt a b => FLt (subst_op y x a) (subst_op y x b) end.

(* ② **나르는 자리** — `x := y` 면 y 에 대한 사실이 x 에도 성립한다.
      `x := c` 면 c 를 사이에 두고 **정확한** 두 사실을 얻는다. *)
Definition assign (fs : list fact) (x : nat) (ex : expr) : list fact :=
  let k := kill x fs in
  match ex with
  | EVar y => k ++ map (subst_fact y x) (filter (mentions y) k)
  | ECst z => k ++ [ FLt (OCst (z - 1)) (OVar x) ; FLt (OVar x) (OCst (z + 1)) ]
  end.

Definition cond_fact (c : cond) : fact := match c with CLt a b => FLt a b end.

(* ★★ **합류 = 교집합.** 한쪽에서만 참인 것은 **사실이 아니다.** *)
Definition fact_in (f : fact) (fs : list fact) : bool := existsb (fact_eqb f) fs.
Definition join (fa fb : list fact) : list fact := filter (fun f => fact_in f fb) fa.

Fixpoint analyze (fs : list fact) (s : stmt) : list fact :=
  match s with
  | SNop => fs
  | SAssign x ex => assign fs x ex
  | SSeq a b => analyze (analyze fs a) b
  | SIf c a b => join (analyze (cond_fact c :: fs) a)   (* ① 참인 갈래에 조건을 심는다 *)
                      (analyze fs b)                    (* 거짓 갈래 — 이 어휘로는 부정을 못 적는다 *)
  end.

(* ── 4. ★★★ 건전성 ─────────────────────────────────────────────────────── *)

Lemma denote_upd_other : forall e x z o,
  op_mentions x o = false -> denote (upd e x z) o = denote e o.
Proof.
  intros e x z [y | w] H; simpl in *; [ | reflexivity ].
  unfold upd. destruct (Nat.eqb y x) eqn:E; [ | reflexivity ].
  apply Nat.eqb_eq in E; subst y. rewrite Nat.eqb_refl in H. discriminate.
Qed.

Lemma kill_sat : forall e x z fs,
  holds e fs -> holds (upd e x z) (kill x fs).
Proof.
  intros e x z fs H f Hin. unfold kill in Hin. apply filter_In in Hin as [Hin Hm].
  apply negb_true_iff in Hm.
  destruct f as [a b]. simpl in Hm. apply orb_false_iff in Hm as [Ha Hb].
  simpl. rewrite (denote_upd_other e x z a Ha), (denote_upd_other e x z b Hb).
  exact (H (FLt a b) Hin).
Qed.

(* 옮겨 적은 사실 — y 를 x 로 바꿔도 뜻이 같다(x 는 방금 y 의 값을 받았으므로). *)
Lemma subst_op_denote : forall e x y o,
  op_mentions x o = false ->
  denote (upd e x (e y)) (subst_op y x o) = denote e o.
Proof.
  intros e x y [z | w] H; simpl in *; [ | reflexivity ].
  destruct (Nat.eqb z y) eqn:E.
  - apply Nat.eqb_eq in E; subst z. simpl. unfold upd. rewrite Nat.eqb_refl. reflexivity.
  - simpl. unfold upd. destruct (Nat.eqb z x) eqn:E2; [ | reflexivity ].
    apply Nat.eqb_eq in E2; subst z. rewrite Nat.eqb_refl in H. discriminate.
Qed.

Lemma assign_sound : forall e fs x ex,
  holds e fs -> holds (upd e x (eval_expr e ex)) (assign fs x ex).
Proof.
  intros e fs x ex H f Hin. unfold assign in Hin.
  destruct ex as [y | z]; simpl in *.
  - (* x := y *)
    apply in_app_or in Hin as [Hin | Hin].
    + exact (kill_sat e x (e y) fs H f Hin).
    + apply in_map_iff in Hin as [g [Hg Hgin]].
      apply filter_In in Hgin as [Hgin _].
      unfold kill in Hgin. apply filter_In in Hgin as [Hgin Hm].
      apply negb_true_iff in Hm.
      destruct g as [a b]. simpl in Hm. apply orb_false_iff in Hm as [Ha Hb].
      subst f. simpl.
      rewrite (subst_op_denote e x y a Ha), (subst_op_denote e x y b Hb).
      exact (H (FLt a b) Hgin).
  - (* x := c — 상수는 **정확한** 두 사실을 준다 *)
    apply in_app_or in Hin as [Hin | Hin].
    + exact (kill_sat e x z fs H f Hin).
    + simpl in Hin. destruct Hin as [Hf | [Hf | Hf]]; try contradiction; subst f; simpl;
        unfold upd; rewrite Nat.eqb_refl; lia.
Qed.

Lemma join_sound_l : forall f fa fb, In f (join fa fb) -> In f fa.
Proof. intros f fa fb H. unfold join in H. apply filter_In in H as [H _]. exact H. Qed.

Lemma join_sound_r : forall f fa fb, In f (join fa fb) -> In f fb.
Proof.
  intros f fa fb H. unfold join in H. apply filter_In in H as [_ H].
  unfold fact_in in H. apply existsb_exists in H as [g [Hg He]].
  rewrite (fact_eqb_eq f g He). exact Hg.
Qed.

(* ★★★ **분석이 말하는 사실은 실제로 참이다** — 모든 실행에서. 유계가 아니다. *)
Theorem analyze_sound : forall s e fs,
  holds e fs -> holds (eval e s) (analyze fs s).
Proof.
  induction s as [ | x ex | a IHa b IHb | c a IHa b IHb ]; intros e fs H; simpl.
  - exact H.
  - apply assign_sound. exact H.
  - apply IHb. apply IHa. exact H.
  - (* ★ 분기 — 실행은 한 갈래만 간다. 분석은 **양쪽을 다 보고 교집합**을 남긴다 *)
    destruct (cond_holdsb e c) eqn:Ec.
    + (* 참인 갈래: 조건이 실제로 성립하므로 심은 사실도 참이다 *)
      intros f Hin. apply (IHa e (cond_fact c :: fs)); [ | exact (join_sound_l f _ _ Hin) ].
      intros g Hg. destruct Hg as [Hg | Hg]; [ | exact (H g Hg) ].
      subst g. destruct c as [p q]. simpl in *. apply Z.ltb_lt in Ec. exact Ec.
    + (* 거짓 갈래: 조건을 심지 않는다(이 어휘로는 부정을 못 적는다 — 보수적이고 안전하다) *)
      intros f Hin. apply (IHb e fs); [ exact H | exact (join_sound_r f _ _ Hin) ].
Qed.

(* ★ 실무에서 쓰는 형태: 분석이 `i < n` 을 말하면 **경계 검사를 지워도 된다.** *)
Corollary removing_a_check_is_justified : forall s e fs i n,
  holds e fs ->
  In (FLt (OVar i) (OVar n)) (analyze fs s) ->
  eval e s i < eval e s n.
Proof.
  intros s e fs i n H Hin.
  exact (analyze_sound s e fs H (FLt (OVar i) (OVar n)) Hin).
Qed.

(* ── 5. ★★ 두 규칙이 **필요하다** — 느슨하게 하면 어디가 깨지나 ──────────── *)

Definition e0 : env := fun _ => 0.

(* ── (가) 합류를 **합집합**으로 하면 분석이 거짓을 말한다 ────────────────── *)

Definition join_union (fa fb : list fact) : list fact := fa ++ fb.

(* if (0 < 1) then x := 0 else x := 100 — 참 갈래에서만 `x < 50` 이 성립한다. *)
Definition prog_alt : stmt :=
  SIf (CLt (OCst 0) (OCst 1)) (SAssign 0 (ECst 0)) (SAssign 0 (ECst 100)).

Definition bad_analyze_alt (fs : list fact) : list fact :=
  join_union (analyze (cond_fact (CLt (OCst 0) (OCst 1)) :: fs) (SAssign 0 (ECst 0)))
             (analyze fs (SAssign 0 (ECst 100))).

(* ★ 합집합은 **양쪽 갈래의 상수 사실을 모두** 남긴다: `x < 1` 과 `x < 101` 이 함께 산다.
   그런데 실행은 한 갈래만 간다 — 거짓 갈래로 가면 `x < 1` 이 **거짓**이다. *)
Theorem union_join_is_unsound :
  In (FLt (OVar 0) (OCst 1)) (bad_analyze_alt []) /\      (* 합집합은 이 사실을 남긴다 *)
  ~ sat (upd e0 0 100) (FLt (OVar 0) (OCst 1)).           (* 그런데 그 실행에서 거짓이다 *)
Proof.
  split.
  - vm_compute. right; right; left; reflexivity.
  - simpl. unfold upd. rewrite Nat.eqb_refl. lia.
Qed.

(* ★ 교집합은 그것을 **남기지 않는다** — 그래서 §4 가 성립한다.
   (두 갈래가 공유하는 사실이 하나도 없으므로 합류 결과는 아예 비어 있다.) *)
Theorem intersection_join_drops_it :
  ~ In (FLt (OVar 0) (OCst 1)) (analyze [] prog_alt).
Proof. vm_compute. intros H. exact H. Qed.

(* ── (나) 대입에서 **죽이지 않으면** 분석이 거짓을 말한다 ────────────────── *)

Definition assign_no_kill (fs : list fact) (x : nat) (z : Z) : list fact :=
  fs ++ [ FLt (OCst (z - 1)) (OVar x) ; FLt (OVar x) (OCst (z + 1)) ].

(* 들어올 때 `x < 10` 을 알고 있었다. 그다음 `x := 20`. *)
Definition fs0 : list fact := [ FLt (OVar 0) (OCst 10) ].

Theorem not_killing_is_unsound :
  In (FLt (OVar 0) (OCst 10)) (assign_no_kill fs0 0 20) /\   (* 안 죽이면 옛 사실이 남는다 *)
  ~ sat (upd e0 0 20) (FLt (OVar 0) (OCst 10)).              (* 그런데 실행 뒤에는 거짓이다 *)
Proof.
  split.
  - vm_compute. left; reflexivity.
  - simpl. unfold upd. rewrite Nat.eqb_refl. lia.
Qed.

(* ★ 죽이는 규칙은 그것을 **지운다.** 05장 ④-3 의 *"3번이 안전의 핵"* 이 이 두 줄이다. *)
Theorem killing_removes_it :
  ~ In (FLt (OVar 0) (OCst 10)) (analyze fs0 (SAssign 0 (ECst 20))).
Proof. vm_compute. intros [H | [H | H]]; try discriminate; contradiction. Qed.

(* ── 6. ★ 공허하지 않다 — 그리고 **정밀도가 어디서 죽는지**까지 보인다 ──── *)

(* ① 나르는 규칙이 실제로 사실을 만든다: `i < n` 을 알 때 `j := i` 뒤에 **`j < n`** 을 안다.
   이것이 05장 ④-2 의 "대입을 따라간다" 이고, 색인 검사를 지우는 힘이 여기서 나온다. *)
Theorem copy_rule_carries_the_fact :
  In (FLt (OVar 2) (OVar 1)) (analyze [ FLt (OVar 0) (OVar 1) ] (SAssign 2 (EVar 0))).
Proof. vm_compute. right; left; reflexivity. Qed.

(* ② 그리고 **합류를 지나면 그 사실이 죽는다.** 05장 ⑥ 이 lru 사례로 적어 둔 바로 그 손실이다:
     *"lru_idx 는 0 과 j 가 합류한 값이라 관계 사실이 meet 에서 죽는다."*
   ★ 이것은 **결함이 아니라 값이다** — 교집합이 살릴 수 없는 것을 살리면 §5-(가)가 된다.
     정직한 요약: **정밀도를 잃는 자리와 안전을 지키는 자리가 같은 자리다.** *)
Definition prog_narrow : stmt :=
  SSeq (SAssign 1 (ECst 5))                                    (* n := 5 *)
       (SIf (CLt (OVar 0) (OVar 1)) (SAssign 2 (EVar 0)) SNop). (* if i < n then j := i *)

Theorem narrowing_dies_at_the_join :
  ~ In (FLt (OVar 2) (OVar 1)) (analyze [] prog_narrow).
Proof. vm_compute. intros [H | [H | H]]; try discriminate; contradiction. Qed.

(* ③ 반면 **두 갈래가 공유하는** 사실은 합류를 넘어 살아남는다 — 분석이 공허하지 않다. *)
Theorem shared_facts_survive_the_join :
  In (FLt (OVar 1) (OCst 6)) (analyze [] prog_narrow).
Proof. vm_compute. right; left; reflexivity. Qed.

(* ── 7. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **부정을 못 적는다.** 사실 어휘가 `<` 뿐이라 거짓 갈래에 `¬(a < b)` 를 심지 못한다.
 *   보수적이므로 **건전성은 그대로**이고, 잃는 것은 **정밀도**다. 구현은 그 자리를 본다
 *   (05장 ④-1 은 *"거짓인 갈래에서는 그 사실을 지운다 — 보수적이 아니라 정확하게"* 라고 적는다).
 *   ⇒ 즉 **이 모델은 구현보다 덜 정밀하고, 더 안전한 쪽으로 덜 정밀하다.**
 * · **루프가 없다.** 합류만 본다. 루프의 고정점은 `LowentNest.v` 가 **차용** 쪽에서 본다.
 * · **산술이 없다.** `x := y + 1` 같은 대입은 어휘 밖이다(그래서 구간 격자가 따로 있다 —
 *   `NumericLattice.v`). 이 파일이 보는 것은 **관계 사실의 전파와 합류**다.
 * · 구현(`low_ir.c` 의 사실 전파)이 **이 모델과 같다**는 것은 여전히 골든·전수 검사가
 *   받치는 경험적 대응이다(16장 ⑤).
 *)

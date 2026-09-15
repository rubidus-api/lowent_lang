(* LowentType.v — ★★★ **타입 건전성**(수치 식 코어): 진행 · 보존 · **표현 불변식**. 순수 Coq.
 *
 * 무엇이 남아 있었나. 16장의 미증명 목록:
 *
 *     | 타입 체계 전체의 건전성 | **미증명**(수치 코어만 증명됨) |
 *
 * 그 괄호 안의 "수치 코어" 는 `NumericLattice.v` — **추상 도메인**(구간)이 옳다는 정리였다.
 * 그것은 *분석*에 대한 정리이지 **타입**에 대한 정리가 아니다. 이 파일은 다른 것을 증명한다:
 *
 *     타입이 붙은 식은 **막히지 않고**(progress), 실행해도 **타입이 변하지 않으며**(preservation),
 *     ★ 값은 **언제나 그 타입의 폭 안에 있다**(representation invariant).
 *
 * ★ 세 번째가 이 언어에서 특별하다. C 백엔드와 VM 이 그 사실에 기댄다 — `u8` 이라고 적힌
 *   자리에 8비트를 넘는 값이 들어오면 `--emit-c` 가 낸 코드가 조용히 다른 일을 한다.
 *   그래서 그것을 **타입 규칙 안에** 넣고(T_Int 가 범위를 요구한다), 실행 내내 보존됨을 증명한다.
 *
 * ★★ 그리고 **덫(trap)을 정직하게 모델링한다.** 이 언어의 검사된 산술은 넘치면 값을 내지 않고
 *   **멈춘다**(계약이 그것을 컴파일 시각에 막는 것이 정상 경로이고, 막지 못하면 런타임 덫이다).
 *   그래서 progress 의 결론이 셋이다: **값이거나 · 덫이거나 · 한 걸음 간다.**
 *   덫을 "막힌 것"(stuck)으로 두면 정리가 거짓이 되고, 덫을 무시하면 정리가 거짓말이 된다.
 *
 * 범위(§8 에 다시 적는다): 수치·불리언 · 묶는 자(let) · 분기 · 검사된 산술 · 감싸는 산술 ·
 * 좁히기 · 비교. **참조·슬라이스·호출·루프는 없다** — 그것들은 각각 다른 파일이 본다.
 *)

Require Import List Bool Arith Lia ZArith.
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 타입과 폭 ────────────────────────────────────────────────────────── *)

Inductive ty : Type :=
  | TInt (bits : nat) (signed : bool)
  | TBool.

(* 폭 상수는 **자연수 거듭제곱**으로 둔다 — `2^(b-1)` 이 b=0 에서도 뜻을 갖게 하려고.
   (Z 의 음수 지수는 0 이 되어 범위가 비어 버린다. 그 함정을 피한다.) *)
Definition full (b : nat) : Z := Z.of_nat (Nat.pow 2 b).
Definition half (b : nat) : Z := Z.of_nat (Nat.pow 2 (b - 1)).

Lemma full_pos : forall b, 0 < full b.
Proof.
  intros b. unfold full. apply (Nat2Z.inj_lt 0). apply Nat.neq_0_lt_0.
  apply Nat.pow_nonzero. discriminate.
Qed.

Lemma half_pos : forall b, 0 < half b.
Proof.
  intros b. unfold half. apply (Nat2Z.inj_lt 0). apply Nat.neq_0_lt_0.
  apply Nat.pow_nonzero. discriminate.
Qed.

(* 값이 타입 안에 있는가 — **이것이 표현 불변식이다.** *)
Definition in_range (b : nat) (s : bool) (z : Z) : Prop :=
  if s then - half b <= z < half b else 0 <= z < full b.

Definition in_range_b (b : nat) (s : bool) (z : Z) : bool :=
  if s then (- half b <=? z) && (z <? half b) else (0 <=? z) && (z <? full b).

Lemma in_range_b_iff : forall b s z, in_range_b b s z = true <-> in_range b s z.
Proof.
  intros b s z. unfold in_range_b, in_range. destruct s.
  - rewrite andb_true_iff, Z.leb_le, Z.ltb_lt. split; [ intros [H1 H2] | intros [H1 H2] ];
      split; assumption.
  - rewrite andb_true_iff, Z.leb_le, Z.ltb_lt. split; [ intros [H1 H2] | intros [H1 H2] ];
      split; assumption.
Qed.

(* 감싸기(wrap) — 언제나 범위 안이다. *)
Definition wrap (b : nat) (s : bool) (z : Z) : Z :=
  if s then ((z + half b) mod full b) - half b else z mod full b.

(* ── 2. 항 ───────────────────────────────────────────────────────────────── *)

Inductive binop : Type := BAdd | BSub | BMul | BWrapAdd.

Inductive tm : Type :=
  | TmInt  (z : Z) (b : nat) (s : bool)
  | TmBool (v : bool)
  | TmVar  (x : nat)
  | TmLet  (x : nat) (t1 t2 : tm)
  | TmIf   (c t e : tm)
  | TmBin  (op : binop) (t1 t2 : tm)
  | TmLt   (t1 t2 : tm)
  | TmNarrow (b : nat) (s : bool) (t : tm)
  | TmTrap.                                  (* ★ 넘침·좁히기 실패 — 값이 아니라 **멈춤** *)

Inductive value : tm -> Prop :=
  | V_Int  : forall z b s, value (TmInt z b s)
  | V_Bool : forall v, value (TmBool v).

(* ── 3. 타입 규칙 ────────────────────────────────────────────────────────── *)

Definition ctx := nat -> option ty.
Definition empty_ctx : ctx := fun _ => None.
Definition update (G : ctx) (x : nat) (T : ty) : ctx :=
  fun y => if Nat.eqb y x then Some T else G y.

Lemma update_eq : forall G x T, update G x T x = Some T.
Proof. intros. unfold update. rewrite Nat.eqb_refl. reflexivity. Qed.

Lemma update_neq : forall G x y T, x <> y -> update G x T y = G y.
Proof.
  intros G x y T H. unfold update.
  destruct (Nat.eqb y x) eqn:E; [ apply Nat.eqb_eq in E; congruence | reflexivity ].
Qed.

Lemma update_shadow : forall G x T1 T2 y,
  update (update G x T1) x T2 y = update G x T2 y.
Proof.
  intros. unfold update. destruct (Nat.eqb y x); reflexivity.
Qed.

Lemma update_permute : forall G x1 x2 T1 T2 y,
  x1 <> x2 ->
  update (update G x1 T1) x2 T2 y = update (update G x2 T2) x1 T1 y.
Proof.
  intros G x1 x2 T1 T2 y H. unfold update.
  destruct (Nat.eqb y x2) eqn:E2; destruct (Nat.eqb y x1) eqn:E1; try reflexivity.
  apply Nat.eqb_eq in E1. apply Nat.eqb_eq in E2. congruence.
Qed.

Inductive has_ty : ctx -> tm -> ty -> Prop :=
  | T_Int : forall G z b s,
      in_range b s z ->                    (* ★★ 표현 불변식이 **타입 규칙 안에** 있다 *)
      has_ty G (TmInt z b s) (TInt b s)
  | T_Bool : forall G v, has_ty G (TmBool v) TBool
  | T_Var : forall G x T, G x = Some T -> has_ty G (TmVar x) T
  | T_Let : forall G x t1 t2 T1 T2,
      has_ty G t1 T1 -> has_ty (update G x T1) t2 T2 -> has_ty G (TmLet x t1 t2) T2
  | T_If : forall G c t e T,
      has_ty G c TBool -> has_ty G t T -> has_ty G e T -> has_ty G (TmIf c t e) T
  | T_Bin : forall G op t1 t2 b s,
      has_ty G t1 (TInt b s) -> has_ty G t2 (TInt b s) ->
      has_ty G (TmBin op t1 t2) (TInt b s)   (* ★ 두 인자의 타입이 **같아야** 한다 — 암묵 변환 없음 *)
  | T_Lt : forall G t1 t2 b s,
      has_ty G t1 (TInt b s) -> has_ty G t2 (TInt b s) -> has_ty G (TmLt t1 t2) TBool
  | T_Narrow : forall G t b s b' s',
      has_ty G t (TInt b s) -> has_ty G (TmNarrow b' s' t) (TInt b' s')
  | T_Trap : forall G T, has_ty G TmTrap T.  (* 덫은 어떤 타입 자리에도 설 수 있다 *)

(* ── 4. 치환과 한 걸음 ───────────────────────────────────────────────────── *)

(* 치환하는 것은 **닫힌 값**뿐이므로 포획이 없다. 다시 묶는 자리에서는 멈춘다(가리기). *)
Fixpoint subst (x : nat) (v : tm) (t : tm) : tm :=
  match t with
  | TmVar y => if Nat.eqb y x then v else t
  | TmLet y t1 t2 =>
      TmLet y (subst x v t1) (if Nat.eqb y x then t2 else subst x v t2)
  | TmIf c a b => TmIf (subst x v c) (subst x v a) (subst x v b)
  | TmBin op a b => TmBin op (subst x v a) (subst x v b)
  | TmLt a b => TmLt (subst x v a) (subst x v b)
  | TmNarrow b s a => TmNarrow b s (subst x v a)
  | TmInt _ _ _ | TmBool _ | TmTrap => t
  end.

Definition apply_op (op : binop) (z1 z2 : Z) : Z :=
  match op with
  | BAdd | BWrapAdd => z1 + z2
  | BSub => z1 - z2
  | BMul => z1 * z2
  end.

(* 검사된 산술: 넘치면 **덫**. 감싸는 산술: 언제나 값. *)
Definition bin_result (op : binop) (b : nat) (s : bool) (z1 z2 : Z) : tm :=
  match op with
  | BWrapAdd => TmInt (wrap b s (z1 + z2)) b s
  | _ => let z := apply_op op z1 z2 in
         if in_range_b b s z then TmInt z b s else TmTrap
  end.

Inductive step : tm -> tm -> Prop :=
  (* 묶는 자 *)
  | ST_Let1 : forall x t1 t1' t2, step t1 t1' -> step (TmLet x t1 t2) (TmLet x t1' t2)
  | ST_LetTrap : forall x t2, step (TmLet x TmTrap t2) TmTrap
  | ST_LetV : forall x v t2, value v -> step (TmLet x v t2) (subst x v t2)
  (* 분기 *)
  | ST_If1 : forall c c' a b, step c c' -> step (TmIf c a b) (TmIf c' a b)
  | ST_IfTrap : forall a b, step (TmIf TmTrap a b) TmTrap
  | ST_IfT : forall a b, step (TmIf (TmBool true) a b) a
  | ST_IfF : forall a b, step (TmIf (TmBool false) a b) b
  (* 이항 *)
  | ST_Bin1 : forall op t1 t1' t2, step t1 t1' -> step (TmBin op t1 t2) (TmBin op t1' t2)
  | ST_Bin2 : forall op v t2 t2', value v -> step t2 t2' -> step (TmBin op v t2) (TmBin op v t2')
  | ST_BinTrap1 : forall op t2, step (TmBin op TmTrap t2) TmTrap
  | ST_BinTrap2 : forall op v, value v -> step (TmBin op v TmTrap) TmTrap
  | ST_BinV : forall op z1 b1 s1 z2 b2 s2,
      step (TmBin op (TmInt z1 b1 s1) (TmInt z2 b2 s2)) (bin_result op b1 s1 z1 z2)
  (* 비교 *)
  | ST_Lt1 : forall t1 t1' t2, step t1 t1' -> step (TmLt t1 t2) (TmLt t1' t2)
  | ST_Lt2 : forall v t2 t2', value v -> step t2 t2' -> step (TmLt v t2) (TmLt v t2')
  | ST_LtTrap1 : forall t2, step (TmLt TmTrap t2) TmTrap
  | ST_LtTrap2 : forall v, value v -> step (TmLt v TmTrap) TmTrap
  | ST_LtV : forall z1 b1 s1 z2 b2 s2,
      step (TmLt (TmInt z1 b1 s1) (TmInt z2 b2 s2)) (TmBool (z1 <? z2))
  (* 좁히기 — 안 들어가면 **덫**(이 언어의 `narrow` 가 계약을 요구하는 이유) *)
  | ST_Nrw1 : forall b s t t', step t t' -> step (TmNarrow b s t) (TmNarrow b s t')
  | ST_NrwTrap : forall b s, step (TmNarrow b s TmTrap) TmTrap
  | ST_NrwV : forall b s z b0 s0,
      step (TmNarrow b s (TmInt z b0 s0))
           (if in_range_b b s z then TmInt z b s else TmTrap).

(* ── 5. ★ 표현 불변식 — 값의 정규형 ──────────────────────────────────────── *)

(* **타입이 u8 이면 값은 0..255 안에 있다.** 백엔드가 기대는 바로 그 문장. *)
Lemma canonical_int : forall G z b s b' s',
  has_ty G (TmInt z b s) (TInt b' s') -> b = b' /\ s = s' /\ in_range b' s' z.
Proof.
  intros G z b s b' s' H. inversion H; subst. repeat split; assumption.
Qed.

Lemma wrap_in_range : forall b s z, in_range b s (wrap b s z).
Proof.
  intros b s z. unfold wrap, in_range. pose proof (full_pos b) as Hf.
  destruct s.
  - pose proof (Z.mod_pos_bound (z + half b) (full b) Hf) as [H1 H2].
    (* 부호 있는 폭: full b = 2 * half b (b ≥ 1) · b = 0 이면 full = half = 1 *)
    assert (Hfh : full b <= 2 * half b).
    { unfold full, half. destruct b as [| b0].
      - simpl. lia.
      - rewrite Nat.sub_succ, Nat.sub_0_r.
        assert (Hp : (Nat.pow 2 (S b0) = 2 * Nat.pow 2 b0)%nat) by (simpl; lia).
        rewrite Hp, Nat2Z.inj_mul. change (Z.of_nat 2) with 2. lia. }
    split; lia.
  - apply Z.mod_pos_bound. exact Hf.
Qed.

Lemma bin_result_types : forall G op b s z1 z2,
  has_ty G (bin_result op b s z1 z2) (TInt b s).
Proof.
  intros G op b s z1 z2. unfold bin_result. destruct op.
  - destruct (in_range_b b s (apply_op BAdd z1 z2)) eqn:E; [ | apply T_Trap ].
    apply T_Int. apply in_range_b_iff. exact E.
  - destruct (in_range_b b s (apply_op BSub z1 z2)) eqn:E; [ | apply T_Trap ].
    apply T_Int. apply in_range_b_iff. exact E.
  - destruct (in_range_b b s (apply_op BMul z1 z2)) eqn:E; [ | apply T_Trap ].
    apply T_Int. apply in_range_b_iff. exact E.
  - apply T_Int. apply wrap_in_range.
Qed.

(* ── 6. ★★ 치환 보조정리 ────────────────────────────────────────────────── *)

(* 문맥이 **같은 값을 주면** 타입도 같다. `let` 의 가리기를 다루는 데 이것이 필요하다. *)
Lemma ctx_ext : forall G G' t T,
  (forall y, G y = G' y) -> has_ty G t T -> has_ty G' t T.
Proof.
  intros G G' t T Heq H. revert G' Heq.
  induction H; intros G' Heq.
  - apply T_Int; assumption.
  - apply T_Bool.
  - apply T_Var. rewrite <- Heq. assumption.
  - eapply T_Let; [ apply IHhas_ty1; exact Heq | ].
    apply IHhas_ty2. intros y. unfold update. destruct (Nat.eqb y x); [ reflexivity | apply Heq ].
  - apply T_If; [ apply IHhas_ty1 | apply IHhas_ty2 | apply IHhas_ty3 ]; exact Heq.
  - eapply T_Bin; [ apply IHhas_ty1 | apply IHhas_ty2 ]; exact Heq.
  - eapply T_Lt; [ apply IHhas_ty1 | apply IHhas_ty2 ]; exact Heq.
  - eapply T_Narrow. apply IHhas_ty. exact Heq.
  - apply T_Trap.
Qed.

(* 닫힌 값은 어느 문맥에서도 같은 타입이다. *)
Lemma closed_value_any_ctx : forall v T G',
  has_ty empty_ctx v T -> value v -> has_ty G' v T.
Proof.
  intros v T G' H Hv. inversion Hv; subst; inversion H; subst.
  - apply T_Int; assumption.
  - apply T_Bool.
Qed.

(* ★★ 치환 — 표준 논증이되 **가리기**를 두 갈래로 정확히 나눈다. *)
Lemma substitution : forall t G x U T v,
  has_ty (update G x U) t T ->
  has_ty empty_ctx v U -> value v ->
  has_ty G (subst x v t) T.
Proof.
  induction t as [ z b s | bv | y | y t1 IH1 t2 IH2 | c IHc a IHa e IHe
                 | op t1 IH1 t2 IH2 | t1 IH1 t2 IH2 | b s t0 IH | ];
    intros G x U T v Ht Hv Hval; simpl; inversion Ht; subst.

  - apply T_Int; assumption.
  - apply T_Bool.

  - (* 변수 *)
    destruct (Nat.eqb y x) eqn:E.
    + apply Nat.eqb_eq in E; subst y.
      rewrite update_eq in H1. inversion H1; subst.
      apply closed_value_any_ctx; assumption.
    + apply T_Var. rewrite <- H1. symmetry. apply update_neq.
      intro Hc. subst y. rewrite Nat.eqb_refl in E. discriminate.

  - (* let *)
    destruct (Nat.eqb y x) eqn:E.
    + (* 같은 이름을 다시 묶는다 — 안쪽에는 치환이 닿지 않는다 *)
      apply Nat.eqb_eq in E; subst y.
      eapply T_Let; [ eapply IH1; eauto | ].
      eapply ctx_ext; [ | exact H5 ]. intros y0. apply update_shadow.
    + (* 다른 이름 — 안쪽으로 들어간다(문맥을 맞바꾼다) *)
      eapply T_Let; [ eapply IH1; eauto | ].
      eapply IH2; [ | exact Hv | exact Hval ].
      eapply ctx_ext; [ | exact H5 ]. intros y0. apply update_permute.
      intro Hc. subst y. rewrite Nat.eqb_refl in E. discriminate.

  - apply T_If; [ eapply IHc | eapply IHa | eapply IHe ]; eauto.
  - eapply T_Bin; [ eapply IH1 | eapply IH2 ]; eauto.
  - eapply T_Lt; [ eapply IH1 | eapply IH2 ]; eauto.
  - eapply T_Narrow. eapply IH; eauto.
  - apply T_Trap.
Qed.

(* ── 7. ★★★ 보존 — 실행해도 타입이 변하지 않는다 ────────────────────────── *)

Theorem preservation : forall t t' T,
  has_ty empty_ctx t T -> step t t' -> has_ty empty_ctx t' T.
Proof.
  intros t t' T Ht Hs. revert T Ht.
  induction Hs; intros T Ht; inversion Ht; subst.
  - (* let 안쪽 *)
    eapply T_Let; [ apply IHHs; eassumption | eassumption ].
  - apply T_Trap.
  - (* let 값 — 치환 *)
    eapply substitution; eassumption.
  - apply T_If; [ apply IHHs | | ]; eassumption.
  - apply T_Trap.
  - assumption.
  - assumption.
  - eapply T_Bin; [ apply IHHs; eassumption | eassumption ].
  - eapply T_Bin; [ eassumption | apply IHHs; eassumption ].
  - apply T_Trap.
  - apply T_Trap.
  - (* ★ 이항 값 — 검사되면 값, 넘치면 덫. 어느 쪽이든 타입은 그대로다 *)
    match goal with
    | [ H : has_ty _ (TmInt z1 _ _) _ |- _ ] => inversion H; subst
    end.
    apply bin_result_types.
  - eapply T_Lt; [ apply IHHs; eassumption | eassumption ].
  - eapply T_Lt; [ eassumption | apply IHHs; eassumption ].
  - apply T_Trap.
  - apply T_Trap.
  - apply T_Bool.
  - eapply T_Narrow. apply IHHs. eassumption.
  - apply T_Trap.
  - (* ★ 좁히기 — 들어가면 값(범위 증거와 함께), 아니면 덫 *)
    destruct (in_range_b b s z) eqn:E.
    + apply T_Int. apply in_range_b_iff. exact E.
    + apply T_Trap.
Qed.

(* ── 8. ★★★ 진행 — 막히지 않는다 ────────────────────────────────────────── *)

Lemma canonical_bool : forall G v, value v -> has_ty G v TBool -> exists b, v = TmBool b.
Proof.
  intros G v Hv H. inversion Hv; subst; inversion H; subst. exists v0; reflexivity.
Qed.

Lemma canonical_int_v : forall G v b s,
  value v -> has_ty G v (TInt b s) -> exists z, v = TmInt z b s /\ in_range b s z.
Proof.
  intros G v b s Hv H. inversion Hv; subst; inversion H; subst.
  exists z. split; [ reflexivity | assumption ].
Qed.

(* ★ 결론이 **셋**이다: 값이거나 · 덫이거나 · 한 걸음 간다.
   덫을 빼면 정리가 거짓이고(넘침이 실제로 있다), 덫을 값에 넣으면 정리가 거짓말이다. *)
Theorem progress : forall t T,
  has_ty empty_ctx t T ->
  value t \/ t = TmTrap \/ exists t', step t t'.
Proof.
  intros t T Ht. remember empty_ctx as G eqn:HG. revert HG.
  induction Ht; intros HG; subst.
  - left. apply V_Int.
  - left. apply V_Bool.
  - (* 변수 — 닫힌 항에는 없다 *)
    unfold empty_ctx in H. discriminate.
  - (* let *)
    right; right.
    destruct (IHHt1 eq_refl) as [Hv | [Htr | [t1' Hst]]].
    + exists (subst x t1 t2). apply ST_LetV; exact Hv.
    + subst t1. exists TmTrap. apply ST_LetTrap.
    + exists (TmLet x t1' t2). apply ST_Let1; exact Hst.
  - (* if *)
    right; right.
    destruct (IHHt1 eq_refl) as [Hv | [Htr | [c' Hst]]].
    + destruct (canonical_bool empty_ctx c Hv Ht1) as [bv Hb]. subst c.
      destruct bv; [ exists t; apply ST_IfT | exists e; apply ST_IfF ].
    + subst c. exists TmTrap. apply ST_IfTrap.
    + exists (TmIf c' t e). apply ST_If1; exact Hst.
  - (* 이항 *)
    right; right.
    destruct (IHHt1 eq_refl) as [Hv1 | [Htr1 | [t1' Hst]]].
    + destruct (canonical_int_v empty_ctx t1 b s Hv1 Ht1) as [z1 [Hz1 _]]. subst t1.
      destruct (IHHt2 eq_refl) as [Hv2 | [Htr2 | [t2' Hst]]].
      * destruct (canonical_int_v empty_ctx t2 b s Hv2 Ht2) as [z2 [Hz2 _]]. subst t2.
        exists (bin_result op b s z1 z2). apply ST_BinV.
      * subst t2. exists TmTrap. apply ST_BinTrap2. apply V_Int.
      * exists (TmBin op (TmInt z1 b s) t2'). apply ST_Bin2; [ apply V_Int | exact Hst ].
    + subst t1. exists TmTrap. apply ST_BinTrap1.
    + exists (TmBin op t1' t2). apply ST_Bin1; exact Hst.
  - (* 비교 *)
    right; right.
    destruct (IHHt1 eq_refl) as [Hv1 | [Htr1 | [t1' Hst]]].
    + destruct (canonical_int_v empty_ctx t1 b s Hv1 Ht1) as [z1 [Hz1 _]]. subst t1.
      destruct (IHHt2 eq_refl) as [Hv2 | [Htr2 | [t2' Hst]]].
      * destruct (canonical_int_v empty_ctx t2 b s Hv2 Ht2) as [z2 [Hz2 _]]. subst t2.
        exists (TmBool (z1 <? z2)). apply ST_LtV.
      * subst t2. exists TmTrap. apply ST_LtTrap2. apply V_Int.
      * exists (TmLt (TmInt z1 b s) t2'). apply ST_Lt2; [ apply V_Int | exact Hst ].
    + subst t1. exists TmTrap. apply ST_LtTrap1.
    + exists (TmLt t1' t2). apply ST_Lt1; exact Hst.
  - (* 좁히기 *)
    right; right.
    destruct (IHHt eq_refl) as [Hv | [Htr | [t' Hst]]].
    + destruct (canonical_int_v empty_ctx t b s Hv Ht) as [z [Hz _]]. subst t.
      exists (if in_range_b b' s' z then TmInt z b' s' else TmTrap). apply ST_NrwV.
    + subst t. exists TmTrap. apply ST_NrwTrap.
    + exists (TmNarrow b' s' t'). apply ST_Nrw1; exact Hst.
  - right; left. reflexivity.
Qed.

(* ── 9. ★★★ 타입 안전성 — 여러 걸음으로 ─────────────────────────────────── *)

Inductive multi : tm -> tm -> Prop :=
  | multi_refl : forall t, multi t t
  | multi_step : forall t1 t2 t3, step t1 t2 -> multi t2 t3 -> multi t1 t3.

Theorem type_safety : forall t t' T,
  has_ty empty_ctx t T -> multi t t' ->
  value t' \/ t' = TmTrap \/ exists t'', step t' t''.
Proof.
  intros t t' T Ht Hm. revert T Ht.
  induction Hm; intros T Ht.
  - eapply progress; eassumption.
  - apply (IHHm T). eapply preservation; eassumption.
Qed.

(* ★★★ **표현 불변식 — 끝까지.** 실행이 값에 닿으면 그 값은 **그 타입의 폭 안에** 있다.
   `--emit-c` 가 낸 C 코드와 VM 이 기대는 문장이 이것이다. *)
Theorem values_fit : forall t v b s,
  has_ty empty_ctx t (TInt b s) -> multi t v -> value v ->
  exists z, v = TmInt z b s /\ in_range b s z.
Proof.
  intros t v b s Ht Hm Hv. revert b s Ht.
  induction Hm; intros b s Ht.
  - eapply canonical_int_v; eassumption.
  - apply IHHm; [ exact Hv | ]. eapply preservation; eassumption.
Qed.

(* ── 10. ★★ 공허하지 않다 — 넘침은 덫, 감싸기는 값 ──────────────────────── *)

(* u8 에서 255 + 1 은 **덫**이다(검사된 산술). 계약이 이것을 컴파일 시각에 막는 것이 정상 경로다. *)
Example u8_overflow_traps :
  step (TmBin BAdd (TmInt 255 8 false) (TmInt 1 8 false)) TmTrap.
Proof.
  replace TmTrap with (bin_result BAdd 8 false 255 1) by (vm_compute; reflexivity).
  apply ST_BinV.
Qed.

(* 같은 자리에서 **감싸는** 산술은 0 을 낸다 — 그리고 그 0 은 타입 안에 있다. *)
Example u8_wrap_is_zero :
  step (TmBin BWrapAdd (TmInt 255 8 false) (TmInt 1 8 false)) (TmInt 0 8 false).
Proof.
  replace (TmInt 0 8 false) with (bin_result BWrapAdd 8 false 255 1)
    by (vm_compute; reflexivity).
  apply ST_BinV.
Qed.

(* 좁히기: 300 은 u8 에 **안 들어간다** ⇒ 덫. 200 은 들어간다 ⇒ 값. *)
Example narrow_out_of_range_traps :
  step (TmNarrow 8 false (TmInt 300 16 false)) TmTrap.
Proof.
  replace TmTrap with (if in_range_b 8 false 300 then TmInt 300 8 false else TmTrap)
    by (vm_compute; reflexivity).
  apply ST_NrwV.
Qed.

Example narrow_in_range_is_a_value :
  step (TmNarrow 8 false (TmInt 200 16 false)) (TmInt 200 8 false).
Proof.
  replace (TmInt 200 8 false)
    with (if in_range_b 8 false 200 then TmInt 200 8 false else TmTrap)
    by (vm_compute; reflexivity).
  apply ST_NrwV.
Qed.

(* 타입이 다른 둘을 더하는 식은 **타입이 붙지 않는다** — 암묵 변환이 없다는 것의 형식적 내용. *)
Example no_implicit_conversion :
  ~ (exists T, has_ty empty_ctx (TmBin BAdd (TmInt 1 8 false) (TmInt 1 16 false)) T).
Proof.
  intros [T H]. inversion H; subst.
  match goal with
  | [ H8 : has_ty _ (TmInt 1 8 false) _ |- _ ] => inversion H8; subst
  end.
  match goal with
  | [ H16 : has_ty _ (TmInt 1 16 false) _ |- _ ] => inversion H16
  end.
Qed.

(* ── 11. ★ 증명하지 않은 것 ──────────────────────────────────────────────
 *
 * 이 파일은 **수치 식 코어**의 건전성이다. 아래는 **여기 없다**:
 * · **참조·차용**(06장이 본다) · **슬라이스와 색인**(계약·증명서가 본다) · **op 호출**
 *   (08장의 효과 층은 `LowentEffect.v`, 값 층은 아직 없다) · **루프**(07장·`LowentNest.v`).
 * · **제네릭·comptime**(RFC-0021) · **enum 페이로드**(RFC-0080)의 타입 규칙.
 * · 구현의 타입 검사기(`low_typecheck.c`)가 **이 규칙과 같다**는 것 — 그것은 여전히
 *   골든과 diff-sweep 이 받치는 경험적 대응이다(16장 ⑤의 그 간극).
 * · 덫이 **어디서** 나는지(진단 코드·위치)는 모델 밖이다. 여기서는 "값이 아니라 멈춤" 만 본다.
 * ⇒ 그래서 16장의 그 줄은 **"미증명"에서 "수치 식 코어는 증명됨"으로** 바뀐다. 전부가 아니다.
 *)

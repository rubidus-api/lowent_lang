(* LowentTypeStore.v — ★★★ **참조·슬라이스·호출까지의 타입 건전성**. 순수 Coq.
 *
 * `LowentType.v` 는 **수치 식 코어**를 덮었다(값·묶는 자·분기·산술·좁히기). 16장은 그 뒤에
 * 남은 것을 이렇게 적었다:
 *
 *     | 타입 체계 전체의 건전성 | **부분** — 수치 식 코어는 증명됨.
 *       **참조·슬라이스·호출·제네릭·enum 은 여전히 미증명** |
 *
 * 이 파일이 그중 **셋**을 갚는다: **참조**(shr/mut) · **슬라이스와 색인** · **op 호출**.
 * 그러려면 상태(store)가 들어와야 하고, 그러면 정리의 모양이 바뀐다:
 *
 *     진행(progress)    — 값이거나 · 덫이거나 · **한 걸음 간다**(막히지 않는다)
 *     보존(preservation)— 한 걸음 가도 **타입이 그대로**이고 **store 도 여전히 적격**이다
 *     ★ 메모리 안전     — 색인은 범위 밖을 읽지 않는다(밖이면 값이 아니라 **덫**이다) ·
 *                        참조는 언제나 **살아 있는 칸**을 가리킨다(dangling 이 없다)
 *
 * ★ 설계 결정 둘을 모델에 그대로 넣었다:
 *   · **할당이 없다.** 이 조각의 store 는 크기가 고정이다 — 그래서 store 확장 기계장치가
 *     통째로 사라지고, 증명이 *"참조가 가리키는 칸은 언제나 있다"* 에 집중한다.
 *     (동적 할당은 `alloc` 효과와 소유 타입의 일이고 이 파일 밖이다 — §9.)
 *   · **쓰기는 mut 참조로만.** 공유 참조로 쓰는 항은 **타입이 붙지 않는다**(§8 의 정리).
 *     06장의 배타성 규칙이 *차용*을 다룬다면, 여기서는 **타입**이 같은 것을 한 겹 더 막는다.
 *)

Require Import List Bool Arith Lia ZArith.
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 타입 ─────────────────────────────────────────────────────────────── *)

Inductive ty : Type :=
  | TInt (bits : nat) (signed : bool)
  | TBool
  | TUnit
  | TRef (mut : bool) (T : ty)      (* shr 참조 = mut false · mut 참조 = mut true *)
  | TSlice (T : ty).                (* 원소 타입 T 의 슬라이스 *)

(* 폭 — `LowentType.v` 와 같은 규약(자연수 거듭제곱으로 두어 b=0 도 뜻을 갖는다). *)
Definition full (b : nat) : Z := Z.of_nat (Nat.pow 2 b).
Definition half (b : nat) : Z := Z.of_nat (Nat.pow 2 (b - 1)).

Definition in_range (b : nat) (s : bool) (z : Z) : Prop :=
  if s then - half b <= z < half b else 0 <= z < full b.
Definition in_range_b (b : nat) (s : bool) (z : Z) : bool :=
  if s then (- half b <=? z) && (z <? half b) else (0 <=? z) && (z <? full b).

Lemma in_range_b_iff : forall b s z, in_range_b b s z = true <-> in_range b s z.
Proof.
  intros b s z. unfold in_range_b, in_range. destruct s;
    rewrite andb_true_iff, Z.leb_le, Z.ltb_lt; split; intros [H1 H2]; split; assumption.
Qed.

(* ── 2. 항 ───────────────────────────────────────────────────────────────── *)

Inductive tm : Type :=
  | TmInt   (z : Z) (b : nat) (s : bool)
  | TmBool  (v : bool)
  | TmUnit
  | TmLoc   (l : nat)                    (* 참조 값 — store 의 칸 번호 *)
  | TmSlice (base len : nat)             (* 슬라이스 값 — 시작 칸과 길이 *)
  | TmVar   (x : nat)
  | TmLet   (x : nat) (t1 t2 : tm)
  | TmIf    (c a b : tm)
  | TmAdd   (t1 t2 : tm)                 (* 검사된 덧셈 — 넘치면 덫 *)
  | TmDeref (t : tm)                     (* 참조에서 읽기 *)
  | TmSet   (t1 t2 : tm)                 (* mut 참조에 쓰기 *)
  | TmIndex (t1 t2 : tm)                 (* 슬라이스 색인 — 범위 밖이면 덫 *)
  | TmLen   (t : tm)
  | TmCall  (f : nat) (t : tm)           (* op 호출 (인자 하나) *)
  | TmTrap.

Inductive value : tm -> Prop :=
  | V_Int   : forall z b s, value (TmInt z b s)
  | V_Bool  : forall v, value (TmBool v)
  | V_Unit  : value TmUnit
  | V_Loc   : forall l, value (TmLoc l)
  | V_Slice : forall b l, value (TmSlice b l).

(* store 는 값의 목록이다. **크기가 고정**이다(할당 없음). *)
Definition store := list tm.
Definition stty  := list ty.

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
Proof. intros. unfold update. destruct (Nat.eqb y x); reflexivity. Qed.
Lemma update_permute : forall G x1 x2 T1 T2 y,
  x1 <> x2 -> update (update G x1 T1) x2 T2 y = update (update G x2 T2) x1 T1 y.
Proof.
  intros G x1 x2 T1 T2 y H. unfold update.
  destruct (Nat.eqb y x2) eqn:E2; destruct (Nat.eqb y x1) eqn:E1; try reflexivity.
  apply Nat.eqb_eq in E1. apply Nat.eqb_eq in E2. congruence.
Qed.

(* op 표: 이름 → (매개변수 이름, 인자 타입, 결과 타입, 본문). *)
Definition prog := list (nat * (nat * ty * ty * tm)).
Fixpoint lookup (P : prog) (f : nat) : option (nat * ty * ty * tm) :=
  match P with
  | [] => None
  | (g, v) :: r => if Nat.eqb f g then Some v else lookup r f
  end.

(* ★ 슬라이스가 적격이다 = 그 범위의 **모든 칸이 store 에 있고 원소 타입이 같다.**
   이것이 색인 안전의 뿌리다. *)
Definition slice_ok (ST : stty) (base len : nat) (T : ty) : Prop :=
  forall i, (i < len)%nat -> nth_error ST (base + i) = Some T.

Inductive has_ty : prog -> stty -> ctx -> tm -> ty -> Prop :=
  | T_Int : forall P ST G z b s,
      in_range b s z -> has_ty P ST G (TmInt z b s) (TInt b s)
  | T_Bool : forall P ST G v, has_ty P ST G (TmBool v) TBool
  | T_Unit : forall P ST G, has_ty P ST G TmUnit TUnit
  | T_Loc : forall P ST G l T m,
      nth_error ST l = Some T -> has_ty P ST G (TmLoc l) (TRef m T)
  | T_SliceV : forall P ST G base len T,
      slice_ok ST base len T -> has_ty P ST G (TmSlice base len) (TSlice T)
  | T_Var : forall P ST G x T, G x = Some T -> has_ty P ST G (TmVar x) T
  | T_Let : forall P ST G x t1 t2 T1 T2,
      has_ty P ST G t1 T1 -> has_ty P ST (update G x T1) t2 T2 ->
      has_ty P ST G (TmLet x t1 t2) T2
  | T_If : forall P ST G c a b T,
      has_ty P ST G c TBool -> has_ty P ST G a T -> has_ty P ST G b T ->
      has_ty P ST G (TmIf c a b) T
  | T_Add : forall P ST G t1 t2 b s,
      has_ty P ST G t1 (TInt b s) -> has_ty P ST G t2 (TInt b s) ->
      has_ty P ST G (TmAdd t1 t2) (TInt b s)
  | T_Deref : forall P ST G t m T,
      has_ty P ST G t (TRef m T) -> has_ty P ST G (TmDeref t) T
  (* ★★ **쓰기는 mut 참조로만** — `TRef false` 로는 이 규칙이 붙지 않는다 *)
  | T_Set : forall P ST G t1 t2 T,
      has_ty P ST G t1 (TRef true T) -> has_ty P ST G t2 T ->
      has_ty P ST G (TmSet t1 t2) TUnit
  | T_Index : forall P ST G t1 t2 T b s,
      has_ty P ST G t1 (TSlice T) -> has_ty P ST G t2 (TInt b s) ->
      has_ty P ST G (TmIndex t1 t2) T
  | T_Len : forall P ST G t T,
      has_ty P ST G t (TSlice T) -> has_ty P ST G (TmLen t) (TInt 64 false)
  | T_Call : forall P ST G f t x Tin Tout body,
      lookup P f = Some (x, Tin, Tout, body) ->
      has_ty P ST G t Tin ->
      has_ty P ST G (TmCall f t) Tout
  | T_Trap : forall P ST G T, has_ty P ST G TmTrap T.

(* store 가 적격하다 = 칸 수가 같고, 각 칸의 값이 그 칸의 타입을 갖는다. *)
Definition store_ok (P : prog) (ST : stty) (st : store) : Prop :=
  length st = length ST /\
  forall l T, nth_error ST l = Some T ->
    exists v, nth_error st l = Some v /\ value v /\ has_ty P ST empty_ctx v T.

(* 프로그램이 적격하다 = 모든 op 본문이 자기 서명대로 타입이 붙는다. *)
Definition prog_ok (P : prog) (ST : stty) : Prop :=
  forall f x Tin Tout body,
    lookup P f = Some (x, Tin, Tout, body) ->
    has_ty P ST (update empty_ctx x Tin) body Tout.

(* ── 4. 치환과 한 걸음 ───────────────────────────────────────────────────── *)

Fixpoint subst (x : nat) (v : tm) (t : tm) : tm :=
  match t with
  | TmVar y => if Nat.eqb y x then v else t
  | TmLet y t1 t2 => TmLet y (subst x v t1) (if Nat.eqb y x then t2 else subst x v t2)
  | TmIf c a b => TmIf (subst x v c) (subst x v a) (subst x v b)
  | TmAdd a b => TmAdd (subst x v a) (subst x v b)
  | TmDeref a => TmDeref (subst x v a)
  | TmSet a b => TmSet (subst x v a) (subst x v b)
  | TmIndex a b => TmIndex (subst x v a) (subst x v b)
  | TmLen a => TmLen (subst x v a)
  | TmCall f a => TmCall f (subst x v a)
  | TmInt _ _ _ | TmBool _ | TmUnit | TmLoc _ | TmSlice _ _ | TmTrap => t
  end.

Fixpoint store_upd (st : store) (l : nat) (v : tm) : store :=
  match st, l with
  | [], _ => []
  | _ :: r, O => v :: r
  | a :: r, S k => a :: store_upd r k v
  end.

Definition add_result (b : nat) (s : bool) (z1 z2 : Z) : tm :=
  if in_range_b b s (z1 + z2) then TmInt (z1 + z2) b s else TmTrap.

(* 색인: 범위 안이면 그 칸을 읽고, 밖이면 **덫**. *)
Definition index_result (st : store) (base len : nat) (z : Z) : tm :=
  if (0 <=? z) && (z <? Z.of_nat len)
  then match nth_error st (base + Z.to_nat z) with Some v => v | None => TmTrap end
  else TmTrap.

Inductive step : prog -> store -> tm -> store -> tm -> Prop :=
  (* let *)
  | ST_Let1 : forall P st st' x t1 t1' t2,
      step P st t1 st' t1' -> step P st (TmLet x t1 t2) st' (TmLet x t1' t2)
  | ST_LetTrap : forall P st x t2, step P st (TmLet x TmTrap t2) st TmTrap
  | ST_LetV : forall P st x v t2, value v -> step P st (TmLet x v t2) st (subst x v t2)
  (* if *)
  | ST_If1 : forall P st st' c c' a b,
      step P st c st' c' -> step P st (TmIf c a b) st' (TmIf c' a b)
  | ST_IfTrap : forall P st a b, step P st (TmIf TmTrap a b) st TmTrap
  | ST_IfT : forall P st a b, step P st (TmIf (TmBool true) a b) st a
  | ST_IfF : forall P st a b, step P st (TmIf (TmBool false) a b) st b
  (* 덧셈 *)
  | ST_Add1 : forall P st st' t1 t1' t2,
      step P st t1 st' t1' -> step P st (TmAdd t1 t2) st' (TmAdd t1' t2)
  | ST_Add2 : forall P st st' v t2 t2',
      value v -> step P st t2 st' t2' -> step P st (TmAdd v t2) st' (TmAdd v t2')
  | ST_AddTrap1 : forall P st t2, step P st (TmAdd TmTrap t2) st TmTrap
  | ST_AddTrap2 : forall P st v, value v -> step P st (TmAdd v TmTrap) st TmTrap
  | ST_AddV : forall P st z1 b1 s1 z2 b2 s2,
      step P st (TmAdd (TmInt z1 b1 s1) (TmInt z2 b2 s2)) st (add_result b1 s1 z1 z2)
  (* 참조 읽기 *)
  | ST_Deref1 : forall P st st' t t', step P st t st' t' -> step P st (TmDeref t) st' (TmDeref t')
  | ST_DerefTrap : forall P st, step P st (TmDeref TmTrap) st TmTrap
  | ST_DerefV : forall P st l v,
      nth_error st l = Some v -> step P st (TmDeref (TmLoc l)) st v
  (* 참조 쓰기 *)
  | ST_Set1 : forall P st st' t1 t1' t2,
      step P st t1 st' t1' -> step P st (TmSet t1 t2) st' (TmSet t1' t2)
  | ST_Set2 : forall P st st' v t2 t2',
      value v -> step P st t2 st' t2' -> step P st (TmSet v t2) st' (TmSet v t2')
  | ST_SetTrap1 : forall P st t2, step P st (TmSet TmTrap t2) st TmTrap
  | ST_SetTrap2 : forall P st v, value v -> step P st (TmSet v TmTrap) st TmTrap
  | ST_SetV : forall P st l v,
      value v -> step P st (TmSet (TmLoc l) v) (store_upd st l v) TmUnit
  (* 색인 *)
  | ST_Idx1 : forall P st st' t1 t1' t2,
      step P st t1 st' t1' -> step P st (TmIndex t1 t2) st' (TmIndex t1' t2)
  | ST_Idx2 : forall P st st' v t2 t2',
      value v -> step P st t2 st' t2' -> step P st (TmIndex v t2) st' (TmIndex v t2')
  | ST_IdxTrap1 : forall P st t2, step P st (TmIndex TmTrap t2) st TmTrap
  | ST_IdxTrap2 : forall P st v, value v -> step P st (TmIndex v TmTrap) st TmTrap
  | ST_IdxV : forall P st base len z b s,
      step P st (TmIndex (TmSlice base len) (TmInt z b s)) st (index_result st base len z)
  (* 길이 *)
  | ST_Len1 : forall P st st' t t', step P st t st' t' -> step P st (TmLen t) st' (TmLen t')
  | ST_LenTrap : forall P st, step P st (TmLen TmTrap) st TmTrap
  | ST_LenV : forall P st base len,
      step P st (TmLen (TmSlice base len)) st
           (if in_range_b 64 false (Z.of_nat len) then TmInt (Z.of_nat len) 64 false else TmTrap)
  (* 호출 *)
  | ST_Call1 : forall P st st' f t t', step P st t st' t' -> step P st (TmCall f t) st' (TmCall f t')
  | ST_CallTrap : forall P st f, step P st (TmCall f TmTrap) st TmTrap
  | ST_CallV : forall P st f v x Tin Tout body,
      value v -> lookup P f = Some (x, Tin, Tout, body) ->
      step P st (TmCall f v) st (subst x v body).

(* ── 5. 정규형 ───────────────────────────────────────────────────────────── *)

Lemma canonical_bool : forall P ST G v, value v -> has_ty P ST G v TBool -> exists b, v = TmBool b.
Proof. intros P ST G v Hv H. inversion Hv; subst; inversion H; subst. exists v0; reflexivity. Qed.

Lemma canonical_int : forall P ST G v b s,
  value v -> has_ty P ST G v (TInt b s) -> exists z, v = TmInt z b s /\ in_range b s z.
Proof.
  intros P ST G v b s Hv H. inversion Hv; subst; inversion H; subst.
  exists z. split; [ reflexivity | assumption ].
Qed.

Lemma canonical_ref : forall P ST G v m T,
  value v -> has_ty P ST G v (TRef m T) -> exists l, v = TmLoc l /\ nth_error ST l = Some T.
Proof.
  intros P ST G v m T Hv H. inversion Hv; subst; inversion H; subst.
  exists l. split; [ reflexivity | assumption ].
Qed.

Lemma canonical_slice : forall P ST G v T,
  value v -> has_ty P ST G v (TSlice T) -> exists base len, v = TmSlice base len /\ slice_ok ST base len T.
Proof.
  intros P ST G v T Hv H. inversion Hv; subst; inversion H; subst.
  eexists; eexists. split; [ reflexivity | eassumption ].
Qed.

(* ── 6. ★★ 메모리 안전의 뿌리 ───────────────────────────────────────────── *)

(* **참조가 가리키는 칸은 언제나 store 에 있다** — dangling 이 없다. *)
Lemma ref_points_to_a_live_cell : forall P ST st l T,
  store_ok P ST st -> nth_error ST l = Some T ->
  exists v, nth_error st l = Some v /\ value v /\ has_ty P ST empty_ctx v T.
Proof. intros P ST st l T [_ H] Hl. exact (H l T Hl). Qed.

(* **범위 안의 색인은 반드시 읽을 칸이 있다** — 그래서 색인은 막히지 않는다. *)
Lemma index_in_bounds_reads : forall P ST st base len T i,
  store_ok P ST st -> slice_ok ST base len T -> (i < len)%nat ->
  exists v, nth_error st (base + i) = Some v /\ value v /\ has_ty P ST empty_ctx v T.
Proof.
  intros P ST st base len T i Hst Hsl Hi.
  apply (ref_points_to_a_live_cell P ST st (base + i) T Hst).
  apply Hsl. exact Hi.
Qed.

Lemma index_result_types : forall P ST st base len T z,
  store_ok P ST st -> slice_ok ST base len T ->
  has_ty P ST empty_ctx (index_result st base len z) T.
Proof.
  intros P ST st base len T z Hst Hsl. unfold index_result.
  destruct ((0 <=? z) && (z <? Z.of_nat len)) eqn:E; [ | apply T_Trap ].
  apply andb_true_iff in E as [E1 E2]. apply Z.leb_le in E1. apply Z.ltb_lt in E2.
  assert (Hi : (Z.to_nat z < len)%nat) by lia.
  destruct (index_in_bounds_reads P ST st base len T (Z.to_nat z) Hst Hsl Hi)
    as [v [Hv [_ Hty]]].
  rewrite Hv. exact Hty.
Qed.

(* 쓰기는 store 를 적격한 채로 둔다(같은 타입의 값으로 덮으므로). *)
Lemma nth_error_store_upd_same : forall st l v,
  (l < length st)%nat -> nth_error (store_upd st l v) l = Some v.
Proof.
  induction st as [| a r IH]; intros [| k] v H; simpl in *; try lia; [ reflexivity | ].
  apply IH. lia.
Qed.

Lemma nth_error_store_upd_other : forall st l k v,
  l <> k -> nth_error (store_upd st l v) k = nth_error st k.
Proof.
  induction st as [| a r IH]; intros [| l'] [| k'] v H; simpl; try reflexivity.
  - congruence.
  - apply IH. lia.
Qed.

Lemma store_upd_length : forall st l v, length (store_upd st l v) = length st.
Proof.
  induction st as [| a r IH]; intros [| k] v; simpl; try reflexivity. rewrite IH. reflexivity.
Qed.

Lemma store_ok_upd : forall P ST st l v T,
  store_ok P ST st -> nth_error ST l = Some T ->
  value v -> has_ty P ST empty_ctx v T ->
  store_ok P ST (store_upd st l v).
Proof.
  intros P ST st l v T [Hlen Hcell] Hl Hv Hty. split.
  - rewrite store_upd_length. exact Hlen.
  - intros k Tk Hk. destruct (Nat.eq_dec l k) as [He | Hne].
    + subst k. rewrite Hl in Hk. inversion Hk; subst Tk.
      exists v. split; [ | split; assumption ].
      apply nth_error_store_upd_same.
      assert (Hb : (l < length ST)%nat) by (apply nth_error_Some; rewrite Hl; discriminate).
      lia.
    + destruct (Hcell k Tk Hk) as [w [Hw [Hvw Htw]]].
      exists w. rewrite nth_error_store_upd_other by exact Hne.
      split; [ exact Hw | split; assumption ].
Qed.

(* ── 7. ★★ 치환과 보존 ─────────────────────────────────────────────────── *)

Lemma ctx_ext : forall P ST G G' t T,
  (forall y, G y = G' y) -> has_ty P ST G t T -> has_ty P ST G' t T.
Proof.
  intros P ST G G' t T Heq H. revert G' Heq.
  induction H; intros G' Heq.
  - apply T_Int; assumption.
  - apply T_Bool.
  - apply T_Unit.
  - eapply T_Loc; eassumption.
  - apply T_SliceV; assumption.
  - apply T_Var. rewrite <- Heq. assumption.
  - eapply T_Let; [ apply IHhas_ty1; exact Heq | ].
    apply IHhas_ty2. intros y. unfold update. destruct (Nat.eqb y x); [ reflexivity | apply Heq ].
  - apply T_If; [ apply IHhas_ty1 | apply IHhas_ty2 | apply IHhas_ty3 ]; exact Heq.
  - eapply T_Add; [ apply IHhas_ty1 | apply IHhas_ty2 ]; exact Heq.
  - eapply T_Deref. apply IHhas_ty. exact Heq.
  - eapply T_Set; [ apply IHhas_ty1 | apply IHhas_ty2 ]; exact Heq.
  - eapply T_Index; [ apply IHhas_ty1 | apply IHhas_ty2 ]; exact Heq.
  - eapply T_Len. apply IHhas_ty. exact Heq.
  - eapply T_Call; [ eassumption | apply IHhas_ty; exact Heq ].
  - apply T_Trap.
Qed.

Lemma closed_value_any_ctx : forall P ST G v T,
  has_ty P ST empty_ctx v T -> value v -> has_ty P ST G v T.
Proof.
  intros P ST G v T H Hv. inversion Hv; subst; inversion H; subst.
  - apply T_Int; assumption.
  - apply T_Bool.
  - apply T_Unit.
  - eapply T_Loc; eassumption.
  - apply T_SliceV; assumption.
Qed.

Lemma substitution : forall t P ST G x U T v,
  has_ty P ST (update G x U) t T ->
  has_ty P ST empty_ctx v U -> value v ->
  has_ty P ST G (subst x v t) T.
Proof.
  induction t as [ z b s | bv | | l | base len | y | y t1 IH1 t2 IH2 | c IHc a IHa b0 IHb
                 | t1 IH1 t2 IH2 | t0 IH | t1 IH1 t2 IH2 | t1 IH1 t2 IH2 | t0 IH
                 | f t0 IH | ];
    intros P ST G x U T w Ht Hv Hval; simpl; inversion Ht; subst.
  - apply T_Int; assumption.
  - apply T_Bool.
  - apply T_Unit.
  - eapply T_Loc; eassumption.
  - apply T_SliceV; assumption.
  - (* 변수 *)
    match goal with [ Hg : update G x U _ = Some _ |- _ ] => rename Hg into Hlk end.
    destruct (Nat.eqb y x) eqn:E.
    + apply Nat.eqb_eq in E; subst y.
      rewrite update_eq in Hlk. inversion Hlk; subst.
      apply closed_value_any_ctx; assumption.
    + apply T_Var. rewrite <- Hlk. symmetry. apply update_neq.
      intro Hc. subst y. rewrite Nat.eqb_refl in E. discriminate.
  - (* let *)
    match goal with [ Hb : has_ty _ _ (update (update G x U) y _) _ _ |- _ ] =>
      rename Hb into Hin end.
    destruct (Nat.eqb y x) eqn:E.
    + apply Nat.eqb_eq in E; subst y.
      eapply T_Let; [ eapply IH1; eauto | ].
      eapply ctx_ext; [ | exact Hin ]. intros y0. apply update_shadow.
    + eapply T_Let; [ eapply IH1; eauto | ].
      eapply IH2; [ | exact Hv | exact Hval ].
      eapply ctx_ext; [ | exact Hin ]. intros y0. apply update_permute.
      intro Hc. subst y. rewrite Nat.eqb_refl in E. discriminate.
  - apply T_If; [ eapply IHc | eapply IHa | eapply IHb ]; eauto.
  - eapply T_Add; [ eapply IH1 | eapply IH2 ]; eauto.
  - eapply T_Deref. eapply IH; eauto.
  - eapply T_Set; [ eapply IH1 | eapply IH2 ]; eauto.
  - eapply T_Index; [ eapply IH1 | eapply IH2 ]; eauto.
  - eapply T_Len. eapply IH; eauto.
  - eapply T_Call; [ eassumption | eapply IH; eauto ].
  - apply T_Trap.
Qed.

Lemma add_result_types : forall P ST G b s z1 z2,
  has_ty P ST G (add_result b s z1 z2) (TInt b s).
Proof.
  intros. unfold add_result. destruct (in_range_b b s (z1 + z2)) eqn:E; [ | apply T_Trap ].
  apply T_Int. apply in_range_b_iff. exact E.
Qed.

(* ★★★ 보존 — 타입도 store 적격성도 그대로다. *)
Theorem preservation : forall P ST st t st' t' T,
  prog_ok P ST -> store_ok P ST st ->
  has_ty P ST empty_ctx t T ->
  step P st t st' t' ->
  has_ty P ST empty_ctx t' T /\ store_ok P ST st'.
Proof.
  intros P ST st t st' t' T HP Hst Ht Hs. revert T Ht.
  induction Hs; intros T Ht; inversion Ht; subst.
  - destruct (IHHs HP Hst T1 ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Let; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; [ | exact Hst ]. eapply substitution; eassumption.
  - destruct (IHHs HP Hst TBool ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    apply T_If; assumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; assumption.
  - split; assumption.
  - destruct (IHHs HP Hst (TInt b s) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Add; eassumption.
  - destruct (IHHs HP Hst (TInt b s) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Add; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; [ apply T_Trap | exact Hst ].
  - split; [ | exact Hst ].
    match goal with [ H : has_ty _ _ _ (TmInt z1 _ _) _ |- _ ] => inversion H; subst end.
    apply add_result_types.
  - destruct (IHHs HP Hst (TRef m T) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Deref; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - (* ★ 참조 읽기 — store 적격성이 값의 타입을 준다 *)
    split; [ | exact Hst ].
    match goal with [ Hl : has_ty _ _ _ (TmLoc _) _ |- _ ] => inversion Hl; subst end.
    destruct (ref_points_to_a_live_cell P ST st l T Hst ltac:(eassumption))
      as [w [Hw [_ Hty]]].
    rewrite H in Hw. inversion Hw; subst. exact Hty.
  - destruct (IHHs HP Hst (TRef true T0) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Set; eassumption.
  - destruct (IHHs HP Hst T0 ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Set; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; [ apply T_Trap | exact Hst ].
  - (* ★ 참조 쓰기 — 같은 타입으로 덮으므로 store 는 여전히 적격 *)
    match goal with [ Hl : has_ty _ _ _ (TmLoc _) _ |- _ ] => inversion Hl; subst end.
    split; [ apply T_Unit | ].
    eapply store_ok_upd; eassumption.
  - destruct (IHHs HP Hst (TSlice T) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Index; eassumption.
  - destruct (IHHs HP Hst (TInt b s) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Index; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; [ apply T_Trap | exact Hst ].
  - (* ★ 색인 — 범위 안이면 그 칸의 값(타입 T), 밖이면 덫 *)
    split; [ | exact Hst ].
    match goal with [ Hl : has_ty _ _ _ (TmSlice _ _) _ |- _ ] => inversion Hl; subst end.
    eapply index_result_types; eassumption.
  - destruct (IHHs HP Hst (TSlice T0) ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Len; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - split; [ | exact Hst ].
    destruct (in_range_b 64 false (Z.of_nat len)) eqn:E; [ | apply T_Trap ].
    apply T_Int. apply in_range_b_iff. exact E.
  - destruct (IHHs HP Hst Tin ltac:(eassumption)) as [Hty Hst']. split; [ | exact Hst' ].
    eapply T_Call; eassumption.
  - split; [ apply T_Trap | exact Hst ].
  - (* ★ 호출 — 본문이 서명대로 타입이 붙고, 인자를 치환한다 *)
    split; [ | exact Hst ].
    match goal with
    | [ Hl2 : lookup P f = Some (_, _, _, _), Hl1 : lookup P f = Some (_, _, _, _) |- _ ] =>
        rewrite Hl1 in Hl2; inversion Hl2; subst
    end.
    eapply substitution; [ eapply HP; eassumption | eassumption | eassumption ].
Qed.

(* ── 8. ★★★ 진행 — 막히지 않는다 ───────────────────────────────────────── *)

Theorem progress : forall P ST st t T,
  prog_ok P ST -> store_ok P ST st ->
  has_ty P ST empty_ctx t T ->
  value t \/ t = TmTrap \/ exists st' t', step P st t st' t'.
Proof.
  intros P ST st t T HP Hst Ht.
  remember empty_ctx as G eqn:HG. revert HG.
  induction Ht; intros HG; subst.
  - left; apply V_Int.
  - left; apply V_Bool.
  - left; apply V_Unit.
  - left; apply V_Loc.
  - left; apply V_Slice.
  - unfold empty_ctx in H. discriminate.
  - right; right.
    destruct (IHHt1 HP Hst eq_refl) as [Hv | [Htr | [st1 [t1' Hs]]]].
    + exists st, (subst x t1 t2). apply ST_LetV; exact Hv.
    + subst t1. exists st, TmTrap. apply ST_LetTrap.
    + exists st1, (TmLet x t1' t2). apply ST_Let1; exact Hs.
  - right; right.
    destruct (IHHt1 HP Hst eq_refl) as [Hv | [Htr | [st1 [c' Hs]]]].
    + destruct (canonical_bool P ST empty_ctx c Hv Ht1) as [bv Hb]. subst c.
      destruct bv; [ exists st, a; apply ST_IfT | exists st, b; apply ST_IfF ].
    + subst c. exists st, TmTrap. apply ST_IfTrap.
    + exists st1, (TmIf c' a b). apply ST_If1; exact Hs.
  - right; right.
    destruct (IHHt1 HP Hst eq_refl) as [Hv1 | [Htr1 | [st1 [t1' Hs]]]].
    + destruct (canonical_int P ST empty_ctx t1 b s Hv1 Ht1) as [z1 [Hz1 _]]. subst t1.
      destruct (IHHt2 HP Hst eq_refl) as [Hv2 | [Htr2 | [st2 [t2' Hs]]]].
      * destruct (canonical_int P ST empty_ctx t2 b s Hv2 Ht2) as [z2 [Hz2 _]]. subst t2.
        exists st, (add_result b s z1 z2). apply ST_AddV.
      * subst t2. exists st, TmTrap. apply ST_AddTrap2. apply V_Int.
      * exists st2, (TmAdd (TmInt z1 b s) t2'). apply ST_Add2; [ apply V_Int | exact Hs ].
    + subst t1. exists st, TmTrap. apply ST_AddTrap1.
    + exists st1, (TmAdd t1' t2). apply ST_Add1; exact Hs.
  - right; right.
    destruct (IHHt HP Hst eq_refl) as [Hv | [Htr | [st1 [t' Hs]]]].
    + destruct (canonical_ref P ST empty_ctx t m T Hv Ht) as [l [Hl Hnth]]. subst t.
      destruct (ref_points_to_a_live_cell P ST st l T Hst Hnth) as [w [Hw _]].
      exists st, w. apply ST_DerefV. exact Hw.
    + subst t. exists st, TmTrap. apply ST_DerefTrap.
    + exists st1, (TmDeref t'). apply ST_Deref1; exact Hs.
  - right; right.
    destruct (IHHt1 HP Hst eq_refl) as [Hv1 | [Htr1 | [st1 [t1' Hs]]]].
    + destruct (canonical_ref P ST empty_ctx t1 true T Hv1 Ht1) as [l [Hl _]]. subst t1.
      destruct (IHHt2 HP Hst eq_refl) as [Hv2 | [Htr2 | [st2 [t2' Hs]]]].
      * exists (store_upd st l t2), TmUnit. apply ST_SetV; exact Hv2.
      * subst t2. exists st, TmTrap. apply ST_SetTrap2. apply V_Loc.
      * exists st2, (TmSet (TmLoc l) t2'). apply ST_Set2; [ apply V_Loc | exact Hs ].
    + subst t1. exists st, TmTrap. apply ST_SetTrap1.
    + exists st1, (TmSet t1' t2). apply ST_Set1; exact Hs.
  - right; right.
    destruct (IHHt1 HP Hst eq_refl) as [Hv1 | [Htr1 | [st1 [t1' Hs]]]].
    + destruct (canonical_slice P ST empty_ctx t1 T Hv1 Ht1) as [base [len [Hb _]]]. subst t1.
      destruct (IHHt2 HP Hst eq_refl) as [Hv2 | [Htr2 | [st2 [t2' Hs]]]].
      * destruct (canonical_int P ST empty_ctx t2 b s Hv2 Ht2) as [z [Hz _]]. subst t2.
        exists st, (index_result st base len z). apply ST_IdxV.
      * subst t2. exists st, TmTrap. apply ST_IdxTrap2. apply V_Slice.
      * exists st2, (TmIndex (TmSlice base len) t2'). apply ST_Idx2; [ apply V_Slice | exact Hs ].
    + subst t1. exists st, TmTrap. apply ST_IdxTrap1.
    + exists st1, (TmIndex t1' t2). apply ST_Idx1; exact Hs.
  - right; right.
    destruct (IHHt HP Hst eq_refl) as [Hv | [Htr | [st1 [t' Hs]]]].
    + destruct (canonical_slice P ST empty_ctx t T Hv Ht) as [base [len [Hb _]]]. subst t.
      eexists st, _. apply ST_LenV.
    + subst t. exists st, TmTrap. apply ST_LenTrap.
    + exists st1, (TmLen t'). apply ST_Len1; exact Hs.
  - right; right.
    destruct (IHHt HP Hst eq_refl) as [Hv | [Htr | [st1 [t' Hs]]]].
    + exists st, (subst x t body). eapply ST_CallV; eassumption.
    + subst t. exists st, TmTrap. apply ST_CallTrap.
    + exists st1, (TmCall f t'). apply ST_Call1; exact Hs.
  - right; left; reflexivity.
Qed.

(* ── 9. ★★★ 여러 걸음 — 타입 안전성과 메모리 안전 ──────────────────────── *)

Inductive multi : prog -> store -> tm -> store -> tm -> Prop :=
  | multi_refl : forall P st t, multi P st t st t
  | multi_step : forall P st1 t1 st2 t2 st3 t3,
      step P st1 t1 st2 t2 -> multi P st2 t2 st3 t3 -> multi P st1 t1 st3 t3.

Theorem type_safety : forall P ST st t st' t' T,
  prog_ok P ST -> store_ok P ST st ->
  has_ty P ST empty_ctx t T -> multi P st t st' t' ->
  value t' \/ t' = TmTrap \/ exists st'' t'', step P st' t' st'' t''.
Proof.
  intros P ST st t st' t' T HP Hst Ht Hm. revert T Ht Hst.
  induction Hm; intros T Ht Hst.
  - eapply progress; eassumption.
  - destruct (preservation P ST st1 t1 st2 t2 T HP Hst Ht H) as [Hty Hst2].
    eapply IHHm; eassumption.
Qed.

(* ★★★ **store 는 실행 내내 적격하다** — 참조가 죽은 칸을 가리키는 일이 없다.
   이것이 이 파일의 메모리 안전 진술이다. *)
Theorem store_stays_ok : forall P ST st t st' t' T,
  prog_ok P ST -> store_ok P ST st ->
  has_ty P ST empty_ctx t T -> multi P st t st' t' ->
  store_ok P ST st'.
Proof.
  intros P ST st t st' t' T HP Hst Ht Hm. revert T Ht Hst.
  induction Hm; intros T Ht Hst; [ exact Hst | ].
  destruct (preservation P ST st1 t1 st2 t2 T HP Hst Ht H) as [Hty Hst2].
  eapply IHHm; eassumption.
Qed.

(* ── 10. ★★ 공허하지 않다 · 그리고 **공유 참조로는 쓸 수 없다** ─────────── *)

(* ★★★ 이 언어의 규칙이 타입 층에서 그대로 나온다:
   `set` 은 **mut 참조**를 요구한다. 공유 참조 타입만 갖는 항을 첫 인자로 쓰면
   **타입이 붙지 않는다** — 06장의 배타성이 *차용*을 막는다면, 여기서는 **타입**이 막는다. *)
Theorem set_requires_mut : forall P ST G t1 t2 T U,
  has_ty P ST G t1 (TRef false T) ->      (* 공유 참조 *)
  (forall m T', has_ty P ST G t1 (TRef m T') -> m = false /\ T' = T) ->
  ~ has_ty P ST G (TmSet t1 t2) U.
Proof.
  intros P ST G t1 t2 T U Hshr Huniq Hset.
  inversion Hset; subst.
  match goal with [ Hm : has_ty _ _ _ t1 (TRef true _) |- _ ] =>
    destruct (Huniq true _ Hm) as [Hbad _]; discriminate end.
Qed.

(* 색인은 **범위 밖에서 값을 내지 않는다** — 덫이다(계약이 이것을 컴파일 시각에 막는다). *)
Example index_out_of_bounds_traps : forall st base len,
  index_result st base len (Z.of_nat len) = TmTrap.
Proof.
  intros. unfold index_result.
  destruct ((0 <=? Z.of_nat len) && (Z.of_nat len <? Z.of_nat len)) eqn:E; [ | reflexivity ].
  apply andb_true_iff in E as [_ E2]. apply Z.ltb_lt in E2. lia.
Qed.

(* ── 11. ★ 증명하지 않은 것 ──────────────────────────────────────────────
 *
 * · **할당·해제가 없다.** store 는 크기가 고정이다. 그래서 이 파일은 *"dangling 이 없다"* 를
 *   **쉬운 방식으로** 얻는다(칸이 사라지지 않으므로). 진짜 dangling 문제(해제 후 사용)는
 *   소유·수명의 일이고 06장·`LowentEXCL.v` 가 다른 축에서 본다. **여기서 증명한 것은
 *   그것이 아니다** — 그 차이를 흐리지 않는다.
 * · **차용 규율이 타입 층에 없다.** `TRef true` 가 둘 있어도 이 타입 체계는 막지 않는다.
 *   그것을 막는 것은 06장의 EXCL 규칙이고, **두 층이 각각 다른 것을 막는다.**
 * · **제네릭·comptime·enum 페이로드**의 타입 규칙은 여전히 밖이다(RFC-0021·0080).
 * · **effect 는 이 타입 체계에 없다** — `LowentEffect.v` 가 따로 본다. 둘을 한 판정으로
 *   합치는 것(effect-row)은 미구현이다(08장 ⑥).
 * · 구현(`low_typecheck.c`)이 이 규칙과 같다는 것은 여전히 골든·diff-sweep 이 받친다(16장 ⑤).
 *)

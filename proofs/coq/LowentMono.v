(* LowentMono.v — ★★★ **단형화가 타입·크기·소유를 인스턴스 시점으로 옮긴다**. 순수 Coq.
 *
 * 16장 타입 건전성 줄의 남은 낱말: *"**제네릭**·effect-row 는 여전히 미증명"*.
 * (기능은 이미 있다 — RFC-0021 제네릭 op 은 **구현됨**이고 RFC-0084 제네릭 타입도 선다.
 *  없던 것은 **그 규칙의 증명**이다.)
 *
 * RFC-0084 §2 가 "세 개의 진짜 벽" 을 적었고 넘는 방법이 **각각 다르다**고 했다.
 * 이 파일이 그 셋을 정리로 만든다:
 *
 *   ① **크기** — 틀에서는 `T` 의 크기가 미정이다. 단형화하면 **반드시 정해진다**(§5).
 *   ② **소유** — `vec T` 의 drop 여부는 `T` 에 달렸다. 단형화하면 **반드시 판정된다**(§6).
 *   ③ **진단** — 인스턴스 안에서 터지는 오류를 **경계**로 막는다. 그것이 중심 정리다(§4):
 *
 *        **경계를 만족하는 인스턴스는 반드시 타입이 붙는다.**
 *
 *      대우로 읽으면 RFC-0084 §2.3 의 문장이 그대로 나온다 —
 *      *"인스턴스 안까지 흘러 들어가 터지는 오류는 **경계에 안 적힌 요구** 뿐이다."*
 *      C++ 템플릿 오류가 악명 높은 자리를 이 규율이 막는다.
 *
 * ★★ 그리고 `dyn`(런타임 다형)을 **영구 거절**한 이유가 ①②에서 보인다(§8): `dyn` 은 크기와
 *   drop 을 **런타임으로 미루는** 선택이고 단형화는 **컴파일 시각으로 당기는** 선택이다.
 *   같은 벽을 반대 방향으로 넘는다 — 그래서 하나를 고르면 다른 하나의 비용을 안 낸다.
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. 타입 — 파라미터가 하나 있는 틀 ───────────────────────────────────── *)

Inductive ty : Type :=
  | TInt  (bits : nat) (signed : bool)
  | TBool
  | TVec  (t : ty)          (* 소유 컨테이너 — 원소를 **가진다**(RFC-0044) *)
  | TPar.                   (* ★ 타입 파라미터 `t` (RFC-0084 의 `comptime t type`) *)

Fixpoint closed (T : ty) : bool :=
  match T with
  | TPar => false
  | TVec t => closed t
  | _ => true
  end.

(* 단형화 = 파라미터를 구체 타입으로 **치환**한다. `vec#u32` 가 이 함수의 결과다. *)
Fixpoint subst_ty (A : ty) (T : ty) : ty :=
  match T with
  | TPar => A
  | TVec t => TVec (subst_ty A t)
  | _ => T
  end.

Lemma subst_closes : forall A T, closed A = true -> closed (subst_ty A T) = true.
Proof.
  intros A T HA. induction T; simpl; try reflexivity; [ exact IHT | exact HA ].
Qed.

(* ── 2. 트레이트 = **성질의 이름** ───────────────────────────────────────── *)

(* 견줄 수 있나 · 소유하지 않나(= drop 이 필요 없나). 파라미터는 **모른다**(false). *)
Definition ord_ok (T : ty) : bool :=
  match T with TInt _ _ => true | _ => false end.
Definition plain_ok (T : ty) : bool :=
  match T with TInt _ _ => true | TBool => true | _ => false end.

Inductive tr : Type := ORD | PLAIN.

Definition tr_eqb (a b : tr) : bool :=
  match a, b with ORD, ORD | PLAIN, PLAIN => true | _, _ => false end.

(* ★ 트레이트 충족 = **그 성질을 실제로 갖는가**. 구조적 충족(SPEC-003 §3.3)이 이 모양이다. *)
Definition sat (A : ty) (t : tr) : bool :=
  match t with ORD => ord_ok A | PLAIN => plain_ok A end.

Definition bounds := list tr.
Definition has_bound (B : bounds) (t : tr) : bool := existsb (tr_eqb t) B.

(* 인스턴스가 **경계를 만족한다** = 적힌 요구를 구체 타입이 전부 충족한다(`E-BOUND-UNSAT` 의 반대). *)
Definition meets (A : ty) (B : bounds) : Prop := forall t, In t B -> sat A t = true.

Lemma has_bound_In : forall B t, has_bound B t = true -> In t B.
Proof.
  intros B t H. unfold has_bound in H. apply existsb_exists in H as [x [Hin Hx]].
  destruct t; destruct x; simpl in Hx; try discriminate; exact Hin.
Qed.

(* ★★ 이 체계의 규율 한 줄:
     **파라미터의 성질은 오직 경계에서 오고, 구체 타입의 성질은 그 자리에서 판정된다.** *)
Definition usable (B : bounds) (T : ty) (t : tr) : Prop :=
  match T with
  | TPar => has_bound B t = true
  | _    => sat T t = true
  end.

(* ── 3. 항과 경계 아래의 타입 규칙 ───────────────────────────────────────── *)

Inductive tm : Type :=
  | TmLit    (T : ty)          (* T 타입의 값 *)
  | TmLt     (a b : tm)        (* 견주기 — ★ ORD 가 필요하다 *)
  | TmPush   (v x : tm)        (* vec 에 넣기 *)
  | TmForget (v : tm)          (* ★ drop 없이 버리기 — PLAIN 이 필요하다 *)
  | TmFst    (a b : tm).       (* 짝에서 앞을 고른다 *)

Fixpoint subst_tm (A : ty) (t : tm) : tm :=
  match t with
  | TmLit T => TmLit (subst_ty A T)
  | TmLt a b => TmLt (subst_tm A a) (subst_tm A b)
  | TmPush v x => TmPush (subst_tm A v) (subst_tm A x)
  | TmForget v => TmForget (subst_tm A v)
  | TmFst a b => TmFst (subst_tm A a) (subst_tm A b)
  end.

Inductive wt : bounds -> tm -> ty -> Prop :=
  | WT_Lit : forall B T, wt B (TmLit T) T
  | WT_Lt : forall B a b T,
      wt B a T -> wt B b T ->
      usable B T ORD ->                       (* ★ 경계 또는 구체 성질 *)
      wt B (TmLt a b) TBool
  | WT_Push : forall B v x T,
      wt B v (TVec T) -> wt B x T -> wt B (TmPush v x) (TVec T)
  | WT_Forget : forall B v T,
      wt B v T ->
      usable B T PLAIN ->                     (* ★ 소유가 있으면 그냥 버릴 수 없다 *)
      wt B (TmForget v) TBool
  | WT_Fst : forall B a b T U,
      wt B a T -> wt B b U -> wt B (TmFst a b) T.

(* ── 4. ★★★ 중심 정리 — 경계를 만족하면 인스턴스는 반드시 선다 ─────────── *)

(* 구체 타입에서는 `usable` 이 곧 `sat` 이다 — 파라미터가 아니므로 경계를 안 본다. *)
Lemma usable_nil_closed : forall A t,
  closed A = true -> sat A t = true -> usable [] A t.
Proof. intros A t HA Hs. destruct A; simpl in *; try exact Hs; discriminate HA. Qed.

Lemma usable_subst : forall B A T t,
  closed A = true -> meets A B -> usable B T t -> usable [] (subst_ty A T) t.
Proof.
  intros B A T t HA Hm Hu. destruct T; simpl in *.
  - exact Hu.                                  (* TInt — 그 자리에서 판정됨 *)
  - exact Hu.                                  (* TBool *)
  - exact Hu.                                  (* TVec — 성질은 치환과 무관하다 *)
  - (* ★ TPar — 경계에서 온 요구가 **충족으로 갚아진다** *)
    apply usable_nil_closed; [ exact HA | ].
    apply Hm. apply has_bound_In. exact Hu.
Qed.

(* ★★★ **단형화는 타입을 보존한다.** 그리고 인스턴스에는 **경계가 남지 않는다**(`[]`) —
   즉 인스턴스는 더 이상 아무 가정도 지고 있지 않다. *)
Theorem mono_preserves_typing : forall B A t T,
  closed A = true ->
  meets A B ->
  wt B t T ->
  wt [] (subst_tm A t) (subst_ty A T).
Proof.
  intros B A t T HA Hm Hwt. induction Hwt; simpl.
  - apply WT_Lit.
  - eapply WT_Lt; [ exact (IHHwt1 Hm) | exact (IHHwt2 Hm) | ].
    eapply usable_subst; eassumption.
  - eapply WT_Push; [ exact (IHHwt1 Hm) | exact (IHHwt2 Hm) ].
  - eapply WT_Forget; [ exact (IHHwt Hm) | ].
    eapply usable_subst; eassumption.
  - eapply WT_Fst; [ exact (IHHwt1 Hm) | exact (IHHwt2 Hm) ].
Qed.

(* ★★ 대우 — RFC-0084 §2.3 의 문장 그대로:
   **인스턴스에서 오류가 났다면, 그것은 경계에 안 적힌 요구다.**
   ⇒ 잘못은 사용자의 호출이 아니라 **틀의 경계**에 있다. 고칠 자리가 정해진다. *)
Corollary instance_failure_means_a_missing_bound : forall B A t T,
  closed A = true ->
  wt B t T ->
  ~ wt [] (subst_tm A t) (subst_ty A T) ->
  ~ meets A B.
Proof.
  intros B A t T HA Hwt Hbad Hm. apply Hbad.
  eapply mono_preserves_typing; eassumption.
Qed.

(* ── 5. ★ 벽 ① 크기 — 틀에서는 미정, 인스턴스에서는 반드시 정해진다 ────── *)

(* 크기는 구체 타입에서만 나온다. 파라미터는 **모른다**(None) — RFC-0084 §2.1. *)
Definition size_of (T : ty) : option nat :=
  match T with
  | TInt b _ => Some b
  | TBool => Some 1
  | TVec _ => Some 128            (* 뚱뚱한 핸들(포인터+길이) — 원소 타입과 무관 *)
  | TPar => None                  (* ★ 여기서 `index` 의 주소 산술이 못 나온다 *)
  end.

Theorem size_is_unknown_in_the_template : size_of TPar = None.
Proof. reflexivity. Qed.

Theorem size_is_decided_after_mono : forall A T,
  closed A = true -> size_of (subst_ty A T) <> None.
Proof.
  intros A T HA. destruct T; simpl; try discriminate.
  (* TPar 는 A 로 바뀌었고, A 는 구체 타입이라 크기가 있다 *)
  destruct A; simpl in *; try discriminate.
Qed.

(* ── 6. ★ 벽 ② 소유 — drop 여부도 인스턴스 시점에 판정된다 ─────────────── *)

Definition needs_drop (T : ty) : option bool :=
  match T with
  | TInt _ _ => Some false
  | TBool => Some false
  | TVec _ => Some true          (* 컨테이너는 원소를 소유한다 *)
  | TPar => None                 (* ★ `T=u64` 면 아니고 `T=handle` 이면 그렇다 — 미정 *)
  end.

Theorem drop_is_undecided_in_the_template : needs_drop TPar = None.
Proof. reflexivity. Qed.

Theorem drop_is_decided_after_mono : forall A T,
  closed A = true -> needs_drop (subst_ty A T) <> None.
Proof.
  intros A T HA. destruct T; simpl; try discriminate.
  destruct A; simpl in *; try discriminate.
Qed.

(* ── 7. ★★ 공허하지 않다 — 서는 것과 거절되는 것 ────────────────────────── *)

Definition u32 : ty := TInt 32 false.

(* 틀: `fn max input a t . input b t . input comptime t type . requires ordered t .` *)
Definition tmpl_max : tm := TmLt (TmLit TPar) (TmLit TPar).

Theorem template_typechecks_under_its_bound : wt [ORD] tmpl_max TBool.
Proof.
  eapply WT_Lt; [ apply WT_Lit | apply WT_Lit | ].
  simpl. reflexivity.
Qed.

(* ① 경계를 만족하는 인스턴스 `max#u32` 는 **선다** — 정리가 그것을 준다. *)
Theorem instance_u32_stands : wt [] (subst_tm u32 tmpl_max) (subst_ty u32 TBool).
Proof.
  eapply mono_preserves_typing; [ reflexivity | | apply template_typechecks_under_its_bound ].
  intros t Hin. simpl in Hin. destruct Hin as [He | []]; subst t. reflexivity.
Qed.

(* ② 경계를 **못 만족하는** 인스턴스 `max#(vec u32)` 는 호출 자리에서 거절된다
      (`E-BOUND-UNSAT`) — 그리고 실제로 인스턴스는 타입이 **안 붙는다**. *)
Theorem vec_does_not_meet_ord : ~ meets (TVec u32) [ORD].
Proof. intro H. pose proof (H ORD (or_introl eq_refl)) as Hs. discriminate Hs. Qed.

Lemma wt_lit_inv : forall B T U, wt B (TmLit T) U -> U = T.
Proof. intros B T U H. inversion H; subst; reflexivity. Qed.

Theorem instance_vec_does_not_typecheck :
  ~ wt [] (subst_tm (TVec u32) tmpl_max) (subst_ty (TVec u32) TBool).
Proof.
  simpl. intro H.
  inversion H as [ | B0 a0 b0 T0 Ha0 Hb0 Hu0 | | | ]; subst.
  apply wt_lit_inv in Ha0. subst T0.
  unfold usable, sat, ord_ok in Hu0. discriminate.
Qed.

(* ③ 소유 경계: `PLAIN` 없이 파라미터를 그냥 버리는 틀은 **타입이 안 붙는다** —
      "소유가 있으면 그냥 버릴 수 없다" 가 경계로 표현된다(RFC-0044). *)
Theorem forgetting_without_a_bound_is_rejected :
  ~ wt [] (TmForget (TmLit TPar)) TBool.
Proof.
  intro H.
  inversion H as [ | | | B0 v0 T0 Hv0 Hu0 | ]; subst.
  apply wt_lit_inv in Hv0. subst T0.
  unfold usable, has_bound in Hu0. simpl in Hu0. discriminate.
Qed.

Theorem forgetting_with_the_bound_is_fine : wt [PLAIN] (TmForget (TmLit TPar)) TBool.
Proof. eapply WT_Forget; [ apply WT_Lit | simpl; reflexivity ]. Qed.

(* ── 8. ★ 왜 `dyn` 이 거절되는지가 여기서 보인다 ─────────────────────────
 *
 * §5·§6 이 같은 모양이다: **틀에서는 `None`, 인스턴스에서는 `Some`.**
 * 런타임 다형(`dyn`·인터페이스 값)은 그 `None` 을 **런타임까지 들고 가는** 선택이다 —
 * 크기를 값과 함께 나르고(뚱뚱한 포인터), drop 을 vtable 로 찾는다. 그러면:
 *   · `index` 의 주소 산술이 **런타임 곱셈**이 되고(비용이 숨는다)
 *   · drop 이 **간접 호출**이 된다(비용이 숨는다)
 * 둘 다 P2(비용 가시)를 깬다 — 그래서 RFC-0084 §4 가 **영구 거절**이라고 적었다.
 * ⇒ 이 파일은 그 결정이 *"취향"* 이 아니라 **두 정리의 방향 선택**임을 보인다.
 *
 * ── ★ 증명하지 않은 것 ────────────────────────────────────────────────────
 * · **파라미터가 하나**다. `map K V`(둘)로 늘리는 것은 이 구조로 곧장 되지만(치환이 둘이 된다),
 *   하지 않았다. RFC-0084 는 **둘이 상한**이라고 적는다.
 * · **인스턴스화 깊이**(`vec (vec T)` 의 무한 전개)는 다루지 않았다 — 구현은 상한 + 진단으로
 *   막는다(RFC-0084 §4, `E-ENUM-INFINITE` 와 같은 종류).
 * · **effect 경계**(`E-TRAIT-EFFECT`)는 이 모델에 없다 — 트레이트가 효과도 요구할 수 있다.
 * · **내용주소화 dedup**(구조-동일 인스턴스가 합쳐진다, SPEC-004 §15.4)은 `LowentHash.v` 의
 *   층이고 여기서 잇지 않았다.
 * · 구현(`low_mono`)이 이 규칙과 같은지는 여전히 골든·diff-sweep 이 받친다(16장 ⑤).
 *)

(* LowentCert.v — **증명 운반 검사의 규칙을 Coq 으로 옮기고, 그 검사기를 추출한다.**
 *
 * RFC-0086 §4 가 남긴 다음 단계다. 지금 검증기는 파이썬 250줄이고, 그것을 믿는다.
 *   ⇒ 여기서 **같은 규칙을 Coq 으로 적고**, ① 그 규칙이 **건전함을 증명**하고,
 *     ② 검사기를 **OCaml 로 추출**한다. 그러면 검증기는 *사람이 쓴 것* 이 아니라
 *     **증명에서 파생된 것**이 된다.
 *
 * ★ 무엇이 나아지나 — 파이썬 검증기는 "이 산술을 내가 옳게 옮겼나?" 를 아무도 확인하지
 *   않는다. 추출본은 **정리가 말하는 함수 그 자체**다. 그리고 파이썬 검증기는 그 추출본과
 *   **같은 답을 내는지** 대조된다(교차 검증 — `scripts/check-cert-extract.sh`).
 *
 * ★ 정직한 범위: 여기서 증명하는 것은 **규칙의 산술적 건전성**이다.
 *   "그 자리에 그 사실이 정말 성립하는가" 는 여전히 검증기 밖이다(RFC-0086 §3).
 *   즉 이 파일은 검증기를 **믿을 만하게** 만들지만, 검증기의 **관할**을 넓히지는 않는다.
 *)

Require Import ZArith Bool List Lia.
(* ★ `nia` 는 Lia(Micromega)가 준다 — `lia` 는 곱을 못 다룬다. 그 한 줄이 mul_hull 을 닫는다. *)
Import ListNotations.
Open Scope Z_scope.

(* ── 1. 타입 범위 — 분석기와 **독립적으로** 다시 정의한다 ─────────────── *)

(* 2^n. 폭은 1..64 만 다룬다(그 밖은 규칙이 적용되지 않는다고 말한다). *)
Definition two_pow (n : nat) : Z := two_power_nat n.

Definition ty_lo (bits : nat) (signed : bool) : Z :=
  if signed then - (two_pow (Nat.pred bits)) else 0.
Definition ty_hi (bits : nat) (signed : bool) : Z :=
  if signed then two_pow (Nat.pred bits) - 1 else two_pow bits - 1.

Definition in_ty (v : Z) (bits : nat) (signed : bool) : bool :=
  Z.leb (ty_lo bits signed) v && Z.leb v (ty_hi bits signed).

(* ── 2. 규칙 — 증명서의 수만 보고 판정한다 ───────────────────────────── *)

Inductive aop := AAdd | ASub | AMul.

(* 구간 산술. ★ 뺄셈이 **뒤집히고** 곱셈이 **네 곱의 최소/최대**다 — 흔한 실수 자리라
   여기서 한 번 적고, 아래 정리가 그것이 옳음을 못 박는다. *)
Definition iv_op (o : aop) (alo ahi blo bhi : Z) : Z * Z :=
  match o with
  | AAdd => (alo + blo, ahi + bhi)
  | ASub => (alo - bhi, ahi - blo)
  | AMul => let c1 := alo * blo in let c2 := alo * bhi in
            let c3 := ahi * blo in let c4 := ahi * bhi in
            (Z.min (Z.min c1 c2) (Z.min c3 c4),
             Z.max (Z.max c1 c2) (Z.max c3 c4))
  end.

Definition check_arith_range (o : aop) (alo ahi blo bhi : Z)
                             (bits : nat) (signed : bool) : bool :=
  Z.leb alo ahi && Z.leb blo bhi &&
  Nat.leb 1 bits && Nat.leb bits 64 &&
  let (lo, hi) := iv_op o alo ahi blo bhi in
  in_ty lo bits signed && in_ty hi bits signed.

Definition check_narrow_fits (lo hi : Z) (bits : nat) (signed : bool) : bool :=
  Z.leb lo hi && Nat.leb 1 bits && Nat.leb bits 64 &&
  in_ty lo bits signed && in_ty hi bits signed.

Definition check_div_nz (alo ahi blo bhi : Z) (bits : nat) (signed : bool) : bool :=
  (* 제수 구간이 0 을 **포함하지 않는다** *)
  (Z.ltb 0 blo || Z.ltb bhi 0) &&
  (* 부호면 MIN/-1 짝이 **동시에 가능하지 않아야** 한다 *)
  (if signed
   then negb (Z.leb alo (ty_lo bits true) && Z.leb (ty_lo bits true) ahi &&
              Z.leb blo (-1) && Z.leb (-1) bhi)
   else true).

Definition check_idx_nonneg (lo hi : Z) : bool := Z.leb 0 lo.

(* ★ R-SUB-DIFF — 차분 제약(y ≤ x)으로 **하한을 끌어올린** 뺄셈.
   그 사실은 관계 영역의 산물이라 여기서 재도출할 수 없다. ⇒ **상한만** 본다.
   그래서 규칙이 갈려 있어야 한다: `check_arith_range` 로 보내면 순수 구간의 하한
   (alo − bhi)이 타입 밖이라 **거절된다** — 컴파일러가 옳은데 검증기가 틀리는 자리다.
   ★ 실제로 그렇게 됐다: 처음 매핑에서 R-SUB-DIFF 를 RArith 로 보내 Coq 이 거절했고,
     그것은 **컴파일러의 결함이 아니라 매핑의 결함**이었다. 규칙을 가르는 이유가 이것이다. *)
Definition check_sub_hi (alo ahi blo bhi : Z) (bits : nat) (signed : bool) : bool :=
  Z.leb alo ahi && Z.leb blo bhi && Nat.leb 1 bits && Nat.leb bits 64 &&
  in_ty (ahi - blo) bits signed.

(* ★ `ensures` 술어를 **구간만으로** 판정한 자리 — 규칙 이름은 `R-ASSERT-CMP-LT` 등이다.
   비교 낱말은 규칙 **이름**으로 오므로 여기서는 생성자로 받는다 — 숫자 코드표를 두 벌
   두지 않는다. *)
Inductive cmpop := CLt | CLe | CGt | CGe | CEq | CNe.

Definition check_pred_cmp (c : cmpop) (vlo vhi rlo rhi : Z) : bool :=
  Z.leb vlo vhi && Z.leb rlo rhi &&
  match c with
  | CLt => Z.ltb vhi rlo
  | CLe => Z.leb vhi rlo
  | CGt => Z.ltb rhi vlo
  | CGe => Z.leb rhi vlo
  | CEq => Z.eqb vlo vhi && Z.eqb rlo rhi && Z.eqb vlo rlo
  | CNe => Z.ltb vhi rlo || Z.ltb rhi vlo
  end.

Definition holds (c : cmpop) (v r : Z) : Prop :=
  match c with
  | CLt => v < r | CLe => v <= r | CGt => v > r | CGe => v >= r
  | CEq => v = r | CNe => v <> r
  end.

(* ★ R-CALL-ARGRANGE — 인자 구간이 파라미터 **선언 범위** 안이다.
   전에는 첫 인자만 실려 재도출이 불가능했다. **인자마다 한 줄**로 바꾸니 완전히 따져진다.
   ☞ 한 줄에 다 담으려 한 것이 문제였고, **줄을 늘리면 되는 것**이었다. *)
Definition check_arg_range (alo ahi rlo rhi : Z) : bool :=
  Z.leb alo ahi && Z.leb rlo rhi && Z.leb rlo alo && Z.leb ahi rhi.

(* ── 3. ★★ 건전성 정리 — 규칙이 통과시키면 **정말 안전하다** ───────────── *)

(* ★ 곱셈 구간의 심장: `a*b` 는 **네 모서리 곱의 최소·최대 사이**에 있다.
   부호가 섞이면 자명하지 않다 — 그래서 부호로 갈라 `nia`(비선형 정수 산술)에 넘긴다.
   (`lia` 는 곱을 못 다룬다. 이 한 줄을 몰라서 손으로 여덟 경우를 쓰려던 것을 아낀다.) *)
Lemma mul_hull : forall alo ahi blo bhi a b,
  alo <= a <= ahi -> blo <= b <= bhi ->
  Z.min (Z.min (alo*blo) (alo*bhi)) (Z.min (ahi*blo) (ahi*bhi)) <= a*b <=
  Z.max (Z.max (alo*blo) (alo*bhi)) (Z.max (ahi*blo) (ahi*bhi)).
Proof.
  intros alo ahi blo bhi a b [H1 H2] [H3 H4].
  destruct (Z_le_gt_dec 0 a); destruct (Z_le_gt_dec 0 b);
  destruct (Z_le_gt_dec 0 alo); destruct (Z_le_gt_dec 0 blo);
  destruct (Z_le_gt_dec 0 ahi); destruct (Z_le_gt_dec 0 bhi);
  repeat (rewrite Z.min_le_iff || rewrite Z.max_le_iff); nia.
Qed.

Definition apply_op (o : aop) (a b : Z) : Z :=
  match o with AAdd => a + b | ASub => a - b | AMul => a * b end.

(* 구간 산술이 실제 값을 **덮는다**(과근사) — 이것이 규칙의 심장이다. *)
Lemma iv_op_covers : forall o alo ahi blo bhi a b,
  alo <= a <= ahi -> blo <= b <= bhi ->
  fst (iv_op o alo ahi blo bhi) <= apply_op o a b <= snd (iv_op o alo ahi blo bhi).
Proof.
  intros o alo ahi blo bhi a b Ha Hb. destruct Ha as [Ha1 Ha2]. destruct Hb as [Hb1 Hb2].
  destruct o; simpl.
  - split; lia.
  - split; lia.
  - apply (mul_hull alo ahi blo bhi a b); split; assumption.
Qed.

(* ★★★ **정리 — R-ARITH-RANGE 는 건전하다.**
   증명서가 통과하면, 그 구간 안의 **어떤 두 값에 대해서도** 결과가 선언 타입 범위 안이다.
   ⇒ 넘칠 수 없다. ⇒ 런타임 검사를 지워도 된다. (`radd_no_check` 의 일반형) *)
Theorem check_arith_range_sound : forall o alo ahi blo bhi bits signed a b,
  check_arith_range o alo ahi blo bhi bits signed = true ->
  alo <= a <= ahi -> blo <= b <= bhi ->
  ty_lo bits signed <= apply_op o a b <= ty_hi bits signed.
Proof.
  intros o alo ahi blo bhi bits signed a b Hc Ha Hb.
  unfold check_arith_range in Hc.
  destruct (iv_op o alo ahi blo bhi) as [lo hi] eqn:Eiv.
  repeat (apply andb_true_iff in Hc as [Hc ?]).
  (* 구간 끝이 타입 안이고, 실제 값이 구간 안이므로 — 타입 안이다 *)
  pose proof (iv_op_covers o alo ahi blo bhi a b Ha Hb) as Hcov.
  rewrite Eiv in Hcov. simpl in Hcov.
  unfold in_ty in *.
  (* in_ty lo · in_ty hi 를 풀어 부등식으로 바꾼다 *)
  repeat match goal with
  | [ H : (_ && _)%bool = true |- _ ] => apply andb_true_iff in H as [? ?]
  end.
  repeat match goal with
  | [ H : Z.leb _ _ = true |- _ ] => apply Z.leb_le in H
  end.
  split; lia.
Qed.

(* ★★★ **정리 — R-NARROW-FITS 는 건전하다.** 절단이 없다(`narrow_ok_iff` 의 일반형). *)
Theorem check_narrow_fits_sound : forall lo hi bits signed v,
  check_narrow_fits lo hi bits signed = true ->
  lo <= v <= hi ->
  ty_lo bits signed <= v <= ty_hi bits signed.
Proof.
  intros lo hi bits signed v Hc Hv. unfold check_narrow_fits, in_ty in Hc.
  repeat match goal with
  | [ H : (_ && _)%bool = true |- _ ] => apply andb_true_iff in H as [? ?]
  end.
  repeat match goal with
  | [ H : Z.leb _ _ = true |- _ ] => apply Z.leb_le in H
  end.
  destruct Hv. split; lia.
Qed.

(* ★★★ **정리 — R-DIV-NZ 는 건전하다.** 제수가 0 이 아니고, 부호면 MIN/-1 도 아니다
   ⇒ 나눗셈의 트랩 지점이 **둘뿐**이므로(`div_signed_failure_is_only_min_neg1`) 안전하다. *)
Theorem check_div_nz_sound : forall alo ahi blo bhi bits signed a b,
  check_div_nz alo ahi blo bhi bits signed = true ->
  alo <= a <= ahi -> blo <= b <= bhi ->
  b <> 0 /\ (signed = true -> ~ (a = ty_lo bits true /\ b = -1)).
Proof.
  intros alo ahi blo bhi bits signed a b Hc Ha Hb.
  unfold check_div_nz in Hc. apply andb_true_iff in Hc as [Hnz Hmn].
  destruct Ha as [Ha1 Ha2]. destruct Hb as [Hb1 Hb2].
  split.
  - apply orb_true_iff in Hnz. destruct Hnz as [H | H].
    + apply Z.ltb_lt in H. lia.
    + apply Z.ltb_lt in H. lia.
  - intros Hs [Hae Hbe]. subst signed. simpl in Hmn.
    apply negb_true_iff in Hmn.
    (* MIN/-1 짝이 불가능하다고 했는데 실제로 그 짝이다 — 모순 *)
    (* Hmn 은 그 네 부등식의 곱이 false 라고 말한다. 그런데 실제로 넷 다 참이다.
       ★ `simpl` 이 Hmn 안의 `ty_lo` 만 펼쳐 놓아 Hae 와 모양이 어긋난다 — 양쪽을 같은
         모양으로 만들어야 `lia` 가 잇는다(펼치는 자리를 맞추는 것이 이 증명의 전부다). *)
    unfold ty_lo, two_pow, two_power_nat in Hae. simpl in Hae. subst a b.
    repeat (apply andb_false_iff in Hmn as [Hmn | Hmn]);
      apply Z.leb_gt in Hmn; lia.
Qed.

(* ★★ **정리 — R-SUB-DIFF 의 상한은 건전하다.** 하한은 **주장하지 않는다** —
   주장하지 않는 것을 정리의 결론에서 빼는 것이 이 파일이 정직한 방법이다. *)
Theorem check_sub_hi_sound : forall alo ahi blo bhi bits signed a b,
  check_sub_hi alo ahi blo bhi bits signed = true ->
  alo <= a <= ahi -> blo <= b <= bhi ->
  a - b <= ty_hi bits signed.
Proof.
  intros alo ahi blo bhi bits signed a b Hc Ha Hb.
  unfold check_sub_hi, in_ty in Hc.
  repeat match goal with
  | [ H : (_ && _)%bool = true |- _ ] => apply andb_true_iff in H as [? ?]
  end.
  repeat match goal with
  | [ H : Z.leb _ _ = true |- _ ] => apply Z.leb_le in H
  end.
  destruct Ha; destruct Hb. lia.
Qed.

(* ★★★ **정리 — R-ASSERT-CMP-* 는 건전하다.** 두 구간 안의 **어떤 값 짝**에 대해서도
   그 비교가 성립한다 ⇒ 그 `ensures` 검사는 실패할 수 없다. *)
Theorem check_pred_cmp_sound : forall c vlo vhi rlo rhi v r,
  check_pred_cmp c vlo vhi rlo rhi = true ->
  vlo <= v <= vhi -> rlo <= r <= rhi ->
  holds c v r.
Proof.
  intros c vlo vhi rlo rhi v r Hc Hv Hr.
  unfold check_pred_cmp in Hc. destruct Hv as [Hv1 Hv2]. destruct Hr as [Hr1 Hr2].
  apply andb_true_iff in Hc as [Hc12 Hcc].
  apply andb_true_iff in Hc12 as [Hvv Hrr].
  apply Z.leb_le in Hvv. apply Z.leb_le in Hrr.
  destruct c; simpl in Hcc |- *.
  - apply Z.ltb_lt in Hcc. lia.
  - apply Z.leb_le in Hcc. lia.
  - apply Z.ltb_lt in Hcc. lia.
  - apply Z.leb_le in Hcc. lia.
  - repeat (apply andb_true_iff in Hcc as [Hcc ?]).
    repeat match goal with [ H : Z.eqb _ _ = true |- _ ] => apply Z.eqb_eq in H end.
    lia.
  - apply orb_true_iff in Hcc. destruct Hcc as [H | H]; apply Z.ltb_lt in H; lia.
Qed.

(* ★★ **정리 — R-CALL-ARGRANGE 는 건전하다.** 그 구간 안의 **어떤 인자값**도 파라미터의
   선언 범위 안이다 ⇒ 진입 범위 검사는 실패할 수 없다. *)
Theorem check_arg_range_sound : forall alo ahi rlo rhi a,
  check_arg_range alo ahi rlo rhi = true -> alo <= a <= ahi -> rlo <= a <= rhi.
Proof.
  intros alo ahi rlo rhi a Hc Ha. unfold check_arg_range in Hc.
  repeat match goal with [ H : (_ && _)%bool = true |- _ ] => apply andb_true_iff in H as [? ?] end.
  repeat match goal with [ H : Z.leb _ _ = true |- _ ] => apply Z.leb_le in H end.
  destruct Ha. split; lia.
Qed.

(* ★ R-IDX-* 는 **음수 아님**만 본다(사실은 재도출하지 않는다 — RFC-0086 §3).
   그 좁은 주장이라도 정리로 적어 둔다: 통과하면 인덱스가 음수가 아니다. *)
Theorem check_idx_nonneg_sound : forall lo hi i,
  check_idx_nonneg lo hi = true -> lo <= i -> 0 <= i.
Proof.
  intros lo hi i Hc Hle. unfold check_idx_nonneg in Hc. apply Z.leb_le in Hc. lia.
Qed.

(* ── 4. ★★ 추출 — 검증기를 **증명에서 파생시킨다** ─────────────────────── *)

(* 증명서 한 줄의 판정: 규칙 이름 대신 **태그**로 받는다(문자열 파싱은 껍데기의 일이다).
   ★ 껍데기(파싱)와 **판정**을 가른다. 판정만이 증명의 대상이고, 그래서 판정만 추출한다. *)
Inductive rule :=
  | RArith (o : aop)
  | RNarrow
  | RDivNz
  | RIdxNonneg
  | RSubHi
  | RPredCmp (c : cmpop)
  | RArgRange
  | RInventory.   (* ★ 재도출 불가 — 근거가 경로 사실이거나 피호출자 쪽 분석이다.
                     그래도 **재고에 올린다**: 세어지지 않는 것은 관리되지 않는다.
                     판정자는 이것을 **통과시키지 않는다** — 아무 주장도 하지 않는다. *)

(* f : 증명서에 실린 수들(순서는 RFC-0086 의 방출 순서와 같다). *)
Definition check_cert (r : rule) (f : list Z) (bits : nat) (signed : bool) : bool :=
  match r, f with
  | RArith o, [alo; ahi; blo; bhi] => check_arith_range o alo ahi blo bhi bits signed
  | RNarrow,  [lo; hi]             => check_narrow_fits lo hi bits signed
  | RDivNz,   [alo; ahi; blo; bhi] => check_div_nz alo ahi blo bhi bits signed
  | RIdxNonneg, [lo; hi]           => check_idx_nonneg lo hi
  | RSubHi,   [alo; ahi; blo; bhi]  => check_sub_hi alo ahi blo bhi bits signed
  | RPredCmp c, [vlo; vhi; rlo; rhi] => check_pred_cmp c vlo vhi rlo rhi
  | RArgRange, [alo; ahi; rlo; rhi]  => check_arg_range alo ahi rlo rhi
  | RInventory, _ => true   (* ★ **주장 없음**. true 를 내지만 그것은 "검사했다" 가 아니라
                               "이 판정자의 관할이 아니다" 다 — 그 구별을 스크립트가 센다. *)
  | _, _ => false      (* 수의 개수가 규칙과 맞지 않으면 **통과시키지 않는다** *)
  end.

(* ★★★ **추출을 하지 않고 Coq 자신을 판정자로 쓴다** — 그 이유를 적는다.
   처음 계획은 이 검사기를 OCaml 로 **추출**하는 것이었다(RFC-0086 §4 의 "다음 단계").
   실제로 추출은 된다(`Extraction "…" check_cert.`). 그런데 이 환경에는 zarith 의 **인터페이스
   (.cmi)가 없다** — `zarith.cma` 는 있지만 컴파일할 수가 없다. 그러면 남는 선택은 둘이다:
     (가) Z 를 OCaml 의 63비트 `int` 로 매핑한다 → 증명서에 실리는 2^63−1 이 **표현되지 않는다.**
          검증기가 조용히 틀린 답을 낸다. **가장 나쁜 종류다.**
     (나) Z 를 Int64 로 잇는 브리지를 **손으로 쓴다** → 그것이 바로 **없애려던 신뢰 대상**이다.
          검증기를 증명에서 파생시키는 이유가 "손으로 쓴 산술을 믿지 않는 것" 인데, 그 아래에
          손으로 쓴 산술을 새로 깔면 아무것도 나아지지 않는다.
   ⇒ 그래서 **Coq 을 그대로 판정자로 쓴다**: `scripts/check-cert-coq.sh` 가 컴파일러가 낸
     증명서를 Coq 파일로 옮겨 `check_cert` 를 **여기서 계산하고**, 전부 `true` 여야 통과한다.
     Z 는 임의 정밀도이므로 폭 문제가 없고, 판정자는 **위 정리들이 말하는 함수 그 자체**다.
     파이썬 검증기는 빠른 앞단으로 남고, 둘이 어긋나면 게이트가 실패한다(두 벌이 아니라 대조).
   ★ 추출이 더 나은 환경(zarith 인터페이스가 있는 곳)에서는 (가)/(나) 없이 바로 추출하면 된다.
     그때 이 주석을 지우면 된다 — **지금 무엇이 막고 있는지**를 남겨 두는 것이 그 조건이다. *)

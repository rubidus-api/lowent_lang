(* NumericLattice.v — RFC-0052 rev.b 의 수치 코어 기계 검증.
 *
 * 이 파일이 증명하는 것은 RFC 의 *중심 주장* 이다:
 *   "값 보존이 보장되는 확대만 암묵 허용한다" 가 실제로 안전한가?
 * 안전 확대 관계 ⊑ 를 결정 가능한 술어로 정의하고, 그것이
 *   (1) 값을 보존하며(sub_preserves),
 *   (2) 부분 순서이고(refl/trans/antisym),
 *   (3) 비교 가능한 두 타입의 join 이 둘 다를 안전히 담는다(join_sound)
 * 를 기계 증명한다. 여기에 나눗셈의 전역성(div_nz_total)과 폭 절단의
 * 정확한 실패 조건(narrow_ok_iff)을 더한다.
 *
 * 대응: docs/rfc/rfc-0052-numeric-operand-rules.md §6
 *)

Require Import ZArith Bool Lia.
Open Scope Z_scope.

(* ── 1. 타입 우주 ─────────────────────────────────────────────────────────── *)

Inductive width := W8 | W16 | W32 | W64.

Definition wbits (w : width) : Z :=
  match w with W8 => 8 | W16 => 16 | W32 => 32 | W64 => 64 end.

Record ity := mk_ity { iw : width; isigned : bool }.

Lemma wbits_pos : forall w, 1 <= wbits w. Proof. destruct w; simpl; lia. Qed.

(* 표현 범위 — 2의 보수 *)
Definition lo (t : ity) : Z :=
  if isigned t then - 2 ^ (wbits (iw t) - 1) else 0.
Definition hi (t : ity) : Z :=
  if isigned t then 2 ^ (wbits (iw t) - 1) - 1 else 2 ^ wbits (iw t) - 1.

Definition inrange (t : ity) (v : Z) : Prop := lo t <= v <= hi t.

(* ── 2. 안전 확대 관계 ⊑ ──────────────────────────────────────────────────── *)
(* 결정 가능한 술어. 구현(low_typecheck.c)이 그대로 옮겨 쓸 수 있는 형태. *)

Definition sub (t u : ity) : bool :=
  match isigned t, isigned u with
  | false, false => wbits (iw t) <=? wbits (iw u)   (* uN ⊑ uM  (N ≤ M) *)
  | true,  true  => wbits (iw t) <=? wbits (iw u)   (* iN ⊑ iM  (N ≤ M) *)
  | false, true  => wbits (iw t) <?  wbits (iw u)   (* uN ⊑ iM  (N < M — 엄격히 넓어야) *)
  | true,  false => false                           (* iN ⋢ uM  — 음수를 담을 수 없다 *)
  end.

Lemma pow2_le : forall a b, 0 <= a <= b -> 2 ^ a <= 2 ^ b.
Proof. intros. apply Z.pow_le_mono_r; lia. Qed.

(* ★ 핵심 정리 1 — ⊑ 는 값을 보존한다.
   "지능적 암묵 확대" 가 안전하다는 주장의 전부가 이 한 줄이다. *)
Theorem sub_preserves :
  forall t u v, sub t u = true -> inrange t v -> inrange u v.
Proof.
  intros [wt st] [wu su] v H Hr.
  unfold sub, inrange, lo, hi in *; simpl in *.
  pose proof (wbits_pos wt) as Pt. pose proof (wbits_pos wu) as Pu.
  destruct st, su.
  - (* i → i *) apply Z.leb_le in H.
    assert (2 ^ (wbits wt - 1) <= 2 ^ (wbits wu - 1)) by (apply pow2_le; lia).
    lia.
  - (* i → u : 불가 *) discriminate.
  - (* u → i : 엄격히 넓다 *) apply Z.ltb_lt in H.
    assert (2 ^ wbits wt <= 2 ^ (wbits wu - 1)) by (apply pow2_le; lia).
    lia.
  - (* u → u *) apply Z.leb_le in H.
    assert (2 ^ wbits wt <= 2 ^ wbits wu) by (apply pow2_le; lia).
    lia.
Qed.

(* ── 3. ⊑ 는 부분 순서다 (join 규칙이 well-defined 이려면 필요) ───────────── *)

Lemma sub_refl : forall t, sub t t = true.
Proof. intros [w s]; destruct s; simpl; apply Z.leb_refl. Qed.

Lemma sub_trans : forall t u r, sub t u = true -> sub u r = true -> sub t r = true.
Proof.
  (* 도메인이 유한(폭 4 × 부호 2)이라 전수 case 로 끝난다 — 512 케이스 *)
  intros [wt st] [wu su] [wr sr];
    destruct wt, st, wu, su, wr, sr; cbn; congruence.
Qed.

Lemma sub_antisym : forall t u, sub t u = true -> sub u t = true -> t = u.
Proof.
  intros [wt st] [wu su];
    destruct wt, st, wu, su; cbn; congruence.
Qed.

(* ── 4. 이항 연산의 피연산자 규칙 ─────────────────────────────────────────── *)
(* 규칙(RFC-0052 rev.b D1): 두 피연산자는 ⊑ 로 *비교 가능* 해야 한다.
   결과 타입 = 넓은 쪽(join). 비교 불가면 정적 오류.
   ★ 새 타입을 발명하지 않는다 — join 은 반드시 두 피연산자 중 하나다.
     (u8 + i8 → i16 같은 "발명된 join" 은 폭을 소리 없이 키워 비용을 숨긴다.) *)

Definition comparable (t u : ity) : bool := sub t u || sub u t.
Definition join (t u : ity) : ity := if sub t u then u else t.

(* ★ 핵심 정리 2 — 비교 가능하면, join 은 두 피연산자를 모두 안전히 담는다. *)
Theorem join_sound :
  forall t u v,
    comparable t u = true ->
    (inrange t v \/ inrange u v) ->
    inrange (join t u) v.
Proof.
  intros t u v Hc [Hv | Hv]; unfold comparable, join in *;
  destruct (sub t u) eqn:Etu.
  - eapply sub_preserves; eauto.
  - simpl in Hc. exact Hv.
  - exact Hv.
  - simpl in Hc. eapply sub_preserves; eauto.
Qed.

(* join 은 두 피연산자 중 하나다 — 발명되지 않는다 *)
Theorem join_is_an_operand : forall t u, join t u = t \/ join t u = u.
Proof. intros; unfold join; destruct (sub t u); auto. Qed.

(* 비교 불가의 대표 사례: 같은 폭의 부호 다른 두 타입은 서로 담지 못한다.
   ⇒ u32 + i32 는 정적 오류이며, 이는 *올바르다*(어느 쪽도 상대를 담지 못함). *)
Example u32_i32_incomparable :
  comparable (mk_ity W32 false) (mk_ity W32 true) = false.
Proof. reflexivity. Qed.

(* 반면 폭이 엄격히 넓으면 무부호 → 부호 확대가 안전하다 *)
Example u8_into_i16_ok : sub (mk_ity W8 false) (mk_ity W16 true) = true.
Proof. reflexivity. Qed.

(* ── 5. 폭 절단(narrow) — 실패 조건이 정확히 무엇인가 ─────────────────────── *)

Definition fits (u : ity) (v : Z) : bool := (lo u <=? v) && (v <=? hi u).

Theorem fits_iff : forall u v, fits u v = true <-> inrange u v.
Proof.
  intros; unfold fits, inrange; split.
  - intros H. apply andb_true_iff in H as [H1 H2].
    apply Z.leb_le in H1; apply Z.leb_le in H2; lia.
  - intros [H1 H2]. apply andb_true_iff; split; apply Z.leb_le; lia.
Qed.

(* narrow 는 fits 일 때 값을 그대로 주고, 아니면 실패한다 — 조용한 마스킹 없음 *)
Definition narrow (u : ity) (v : Z) : option Z := if fits u v then Some v else None.

Theorem narrow_ok_iff : forall u v, narrow u v = Some v <-> inrange u v.
Proof.
  intros; unfold narrow; split.
  - intros H. destruct (fits u v) eqn:E.
    + apply fits_iff; exact E.
    + discriminate.
  - intros H. apply fits_iff in H. rewrite H. reflexivity.
Qed.

(* 확대는 절대 실패하지 않는다 — 따라서 `widen`(= 암묵 확대의 명시형)은 전역 함수다 *)
Theorem widen_never_fails :
  forall t u v, sub t u = true -> inrange t v -> narrow u v = Some v.
Proof.
  intros. apply narrow_ok_iff. eapply sub_preserves; eauto.
Qed.

(* ── 6. 나눗셈 — 0 나누기와 MIN/-1 ───────────────────────────────────────── *)
(* Z.quot = 0 방향 절단, Z.rem = 피제수 부호 (RFC-0052 §6.3 의 div/rem 과 일치). *)

Lemma wbits_ge8 : forall w, 8 <= wbits w. Proof. destruct w; simpl; lia. Qed.

Lemma quot_abs_le : forall a b, b <> 0 -> Z.abs (a ÷ b) <= Z.abs a.
Proof.
  intros a b Hb. rewrite <- (Z.quot_abs a b Hb).
  apply Z.quot_le_upper_bound; [ lia | nia ].
Qed.

Definition div_trap (t : ity) (a b : Z) : option Z :=
  if Z.eqb b 0 then None                     (* E-VM-DIV0 *)
  else if fits t (a ÷ b) then Some (a ÷ b)
  else None.                                 (* MIN / -1 — 몫이 표현 불가 *)

(* ★ 핵심 정리 3a — 무부호 나눗셈에서 실패는 **0 나누기뿐이다.**
   제수가 0 이 아님이 보장되면(타입 `nonzero τ` 또는 계약/구간 분석),
   무부호 div 는 완전히 전역이다 — 트랩이 없다. *)
Theorem div_unsigned_total :
  forall t a b, isigned t = false -> b <> 0 ->
    inrange t a -> inrange t b -> exists q, div_trap t a b = Some q.
Proof.
  intros [w s] a b Hs Hb Ha Hbr; simpl in Hs; subst s.
  unfold div_trap, inrange, lo, hi in *; simpl in *.
  destruct (Z.eqb b 0) eqn:E0; [ apply Z.eqb_eq in E0; lia | ].
  assert (Hb0 : 0 < b) by lia.
  assert (Hq0 : 0 <= a ÷ b) by (apply Z.quot_pos; lia).
  assert (Hqa : a ÷ b <= a) by (apply Z.quot_le_upper_bound; nia).
  destruct (fits (mk_ity w false) (a ÷ b)) eqn:Ef.
  - eexists; reflexivity.
  - exfalso.
    assert (Hin : inrange (mk_ity w false) (a ÷ b))
      by (unfold inrange, lo, hi; simpl; lia).
    apply fits_iff in Hin; congruence.
Qed.

(* ★ 핵심 정리 3b — 부호 있는 나눗셈에서 실패는 **0 나누기와 MIN/-1 뿐이다.**
   다른 어떤 (a, b) 도 트랩하지 않는다 — 트랩 지점이 정확히 둘이라는 것이
   구간 분석(RFC-0053)이 방전해야 할 의무의 전부다. *)
Theorem div_signed_failure_is_only_min_neg1 :
  forall t a b, isigned t = true -> b <> 0 ->
    inrange t a -> inrange t b ->
    div_trap t a b = None -> (a = lo t /\ b = -1).
Proof.
  intros [w s] a b Hs Hb Ha Hbr Hnone; simpl in Hs; subst s.
  unfold div_trap in Hnone.
  destruct (Z.eqb b 0) eqn:E0; [ apply Z.eqb_eq in E0; lia | ].
  destruct (fits (mk_ity w true) (a ÷ b)) eqn:Ef; [ discriminate | clear Hnone ].
  assert (Hnf : ~ inrange (mk_ity w true) (a ÷ b))
    by (intro Hc; apply fits_iff in Hc; congruence).
  unfold inrange, lo, hi in *; simpl in *.
  pose proof (wbits_ge8 w) as W8b.
  set (M := 2 ^ (wbits w - 1)) in *.
  assert (HM : 128 <= M) by
    (subst M; replace 128 with (2 ^ 7) by reflexivity; apply pow2_le; lia).
  (* |quot| ≤ |a| ≤ M  ⇒  범위를 벗어나려면 quot = M *)
  pose proof (quot_abs_le a b Hb) as Hle.
  assert (Hq : a ÷ b = M) by lia.
  (* |a| = M 이어야 하고, a ≤ M-1 이므로 a = -M = lo *)
  assert (Ha' : a = - M) by lia.
  (* |b| = 1 이어야 한다: |b| ≥ 2 면 |quot| ≤ M-1 이라 모순 *)
  assert (Hb1 : Z.abs b = 1).
  { destruct (Z.eq_dec (Z.abs b) 1) as [E | E]; [ exact E | exfalso ].
    assert (Hb2 : 2 <= Z.abs b) by lia.
    assert (Hup : Z.abs a ÷ Z.abs b <= M - 1)
      by (apply Z.quot_le_upper_bound; [ lia | nia ]).
    rewrite (Z.quot_abs a b Hb) in Hup. lia. }
  (* a < 0 이고 quot > 0 이므로 b < 0 ⇒ b = -1 *)
  assert (b < 0).
  { destruct (Z_lt_le_dec b 0) as [Hlt | Hge]; [ exact Hlt | exfalso ].
    assert (b = 1) by lia. subst b. rewrite Z.quot_1_r in Hq. lia. }
  split; [ lia | lia ].
Qed.

(* 따름: `nonzero τ` 타입이 제수를 보장하면 —
     무부호: div 는 전역(트랩 0개)
     부호  : 트랩 지점이 MIN/-1 단 하나  (구간 분석이 방전할 대상이 정확히 이것)
   이것이 `div_nz : (τ, nonzero τ) -> τ` 를 calc(순수) 문맥에 둘 수 있는 근거다. *)

(* ── 7. rem 과 mod — 두 관례를 이름으로 구분한다 (RFC-0052 D7) ─────────────── *)
(* div      = 0 방향 절단 (Z.quot)   ↔  rem : **피제수(a)** 의 부호
 * div_floor= 바닥        (Z.div )   ↔  mod : **제수(b)**   의 부호
 * 두 관례는 짝을 이루며, 섞어 쓰면 항등식이 깨진다. *)

Definition divT (a b : Z) : Z := a ÷ b.        (* truncate  — RFC 의 `div`       *)
Definition remT (a b : Z) : Z := Z.rem a b.    (*            RFC 의 `rem`        *)
Definition divF (a b : Z) : Z := a / b.        (* floor     — RFC 의 `div_floor` *)
Definition modF (a b : Z) : Z := a mod b.      (*            RFC 의 `mod`        *)

(* ★ 항등식 — 각 나머지는 자기 몫과만 짝을 이룬다. 섞으면 성립하지 않는다. *)
Theorem rem_pairs_with_div :
  forall a b, b <> 0 -> divT a b * b + remT a b = a.
Proof.
  intros a b Hb. unfold divT, remT.
  rewrite Z.rem_eq by exact Hb. lia.
Qed.

Theorem mod_pairs_with_div_floor :
  forall a b, b <> 0 -> divF a b * b + modF a b = a.
Proof.
  intros a b Hb. unfold divF, modF.
  pose proof (Z.div_mod a b Hb). lia.
Qed.

(* ★ rem 의 부호 = 피제수의 부호 *)
Theorem rem_sign_follows_dividend :
  forall a b, b <> 0 ->
    (0 <= a -> 0 <= remT a b) /\ (a <= 0 -> remT a b <= 0).
Proof.
  intros a b Hb; unfold remT; split; intro H.
  - apply Z.rem_nonneg; assumption.
  - apply Z.rem_nonpos; assumption.
Qed.

(* ★ mod 의 부호 = 제수의 부호 — 그리고 크기도 제수가 가둔다 *)
Theorem mod_sign_follows_divisor :
  forall a b,
    (0 < b -> 0 <= modF a b < b) /\ (b < 0 -> b < modF a b <= 0).
Proof.
  intros a b; unfold modF; split; intro H.
  - apply Z.mod_pos_bound; assumption.
  - apply Z.mod_neg_bound; assumption.
Qed.

(* ★ 실전에서 이것이 전부다 — `mod` 는 **항상 유효한 인덱스**를 준다.
   음수 i 에 대해서도 0 <= i mod n < n. `rem` 은 음수를 낼 수 있다(아래 반례). *)
Theorem mod_is_a_safe_index :
  forall i n, 0 < n -> 0 <= modF i n < n.
Proof. intros; apply Z.mod_pos_bound; assumption. Qed.

(* rem 은 그 보장을 주지 못한다 — 구체적 반례 *)
Example rem_can_be_negative : remT (-7) 3 = -1.  Proof. reflexivity. Qed.
Example mod_stays_in_range  : modF (-7) 3 = 2.   Proof. reflexivity. Qed.
Example rem_mod_differ      : remT (-7) 3 <> modF (-7) 3. Proof. discriminate. Qed.

(* ★ 무부호(둘 다 음이 아님)에서는 두 관례가 **일치한다** —
   그래서 이름 변경의 실제 파급이 부호 있는 사용처에 한정된다(RFC §9). *)
Theorem rem_eq_mod_when_nonneg :
  forall a b, 0 <= a -> 0 < b -> remT a b = modF a b.
Proof. intros; unfold remT, modF; apply Z.rem_mod_nonneg; assumption. Qed.

(* 부호가 같으면 몫도 일치한다(절단 = 바닥) *)
Theorem divT_eq_divF_when_nonneg :
  forall a b, 0 <= a -> 0 < b -> divT a b = divF a b.
Proof. intros; unfold divT, divF; apply Z.quot_div_nonneg; assumption. Qed.

(* ── 8. range 타입 — 폭이 아니라 **범위**가 계약이다 (RFC-0055) ─────────────── *)
(* Ada 의 통찰: "흥미로운 규칙은 폭(width)이 아니라 범위(range)에 대한 것이다."
 * range lo hi 를 타입으로 두면 계약이 **시그니처에 실려 op 경계를 넘는다.**
 * 여기서 그 부분타이핑이 §2 의 ⊑ 와 같은 성질(값 보존)을 갖는지 확인한다. *)

Record rng := mk_rng { rlo : Z; rhi : Z }.
Definition rin (r : rng) (v : Z) : Prop := rlo r <= v <= rhi r.

(* 포함 = 부분타입 *)
Definition rsub (r s : rng) : bool := (rlo s <=? rlo r) && (rhi r <=? rhi s).

(* ★ 정리 — range 부분타이핑도 **값을 보존한다**(⊑ 와 같은 성질). *)
Theorem rsub_preserves : forall r s v, rsub r s = true -> rin r v -> rin s v.
Proof.
  intros [a b] [c d] v H Hr. unfold rsub, rin in *; simpl in *.
  apply andb_true_iff in H as [H1 H2].
  apply Z.leb_le in H1; apply Z.leb_le in H2. lia.
Qed.

Theorem rsub_refl : forall r, rsub r r = true.
Proof. intros [a b]; unfold rsub; simpl; rewrite !Z.leb_refl; reflexivity. Qed.

Theorem rsub_trans : forall r s t, rsub r s = true -> rsub s t = true -> rsub r t = true.
Proof.
  intros [a b] [c d] [e f]; unfold rsub; simpl; intros H1 H2.
  apply andb_true_iff in H1 as [H1a H1b]; apply andb_true_iff in H2 as [H2a H2b].
  apply Z.leb_le in H1a; apply Z.leb_le in H1b.
  apply Z.leb_le in H2a; apply Z.leb_le in H2b.
  apply andb_true_iff; split; apply Z.leb_le; lia.
Qed.

(* ★ range 산술 — 결과 범위가 선언 범위 안이면 **검사가 필요 없다**.
   이것이 구간 분석(RFC-0053)을 *타입 수준*으로 올린 것이다. *)
Definition radd (r s : rng) : rng := mk_rng (rlo r + rlo s) (rhi r + rhi s).

Theorem radd_sound : forall r s a b,
  rin r a -> rin s b -> rin (radd r s) (a + b).
Proof. intros [x y] [z w] a b Ha Hb; unfold rin, radd in *; simpl in *; lia. Qed.

(* ★ 따름 — 선언 범위가 결과 범위를 담으면, 그 덧셈은 **절대 넘치지 않는다.** *)
Theorem radd_no_check : forall r s t a b,
  rsub (radd r s) t = true -> rin r a -> rin s b -> rin t (a + b).
Proof.
  intros r s t a b Hsub Ha Hb.
  eapply rsub_preserves; [ exact Hsub | apply radd_sound; assumption ].
Qed.

(* range 를 기계 타입에 앉히기: 폭은 **범위에서 유도된다**(비용 가시). *)
Definition rfits (r : rng) (t : ity) : bool :=
  (lo t <=? rlo r) && (rhi r <=? hi t).

Theorem rfits_preserves : forall r t v, rfits r t = true -> rin r v -> inrange t v.
Proof.
  intros r t v H Hr. unfold rfits, rin, inrange in *.
  apply andb_true_iff in H as [H1 H2].
  apply Z.leb_le in H1; apply Z.leb_le in H2. lia.
Qed.

(* 인덱스 경계 — array n t 의 인덱스가 range 0 (n-1) 이면 **경계 검사가 사라진다**. *)
Definition idx_ok (n : Z) (r : rng) : bool := (0 <=? rlo r) && (rhi r <=? n - 1).

Theorem idx_no_check : forall n r i,
  idx_ok n r = true -> rin r i -> 0 <= i < n.
Proof.
  intros n r i H Hr. unfold idx_ok, rin in *.
  apply andb_true_iff in H as [H1 H2].
  apply Z.leb_le in H1; apply Z.leb_le in H2. lia.
Qed.

(* ── 9. **타입을 명시한** range: `range τ lo hi` (RFC-0055 D7) ───────────────── *)
(* §8 은 범위만 다뤘다. 그러면 표현(폭·부호)은 *유도*되고, 프로그래머는 그것을 고를 수
 * 없다. τ 를 적으면 표현이 못 박히고, 그 대신 **선언이 참인지** 검사해야 한다:
 *   range u8 0 300  ← u8 은 300 을 담지 못한다. 이 선언은 **거짓말**이다.
 * 여기서 그 검사(twf)가 정확히 옳음을, 그리고 두 형태(τ 명시·생략)가 같은 격자에
 * 산다는 것을 확인한다. *)

Record trng := mk_trng { tty : ity; trg : rng }.

(* 선언의 적합성 — 이것이 E-TYPE-RANGE 다. *)
Definition twf (t : trng) : bool := rfits (trg t) (tty t).

(* ★ 정리 — 적합한 선언에서는 범위의 **모든 값이 그 타입에 표현 가능**하다.
   즉 twf 검사를 통과하면 표현은 결코 조용히 잘리지 않는다. *)
Theorem twf_repr : forall t v, twf t = true -> rin (trg t) v -> inrange (tty t) v.
Proof. intros t v H Hr. apply rfits_preserves with (r := trg t); assumption. Qed.

(* 타입이 실린 range 의 부분타이핑 — **두 축을 모두 요구한다.**
   · sub  : 표현이 좁아지지 않는다 (RFC-0052 G2 — 암묵 변환은 ⊑ 를 따를 때만)
   · rsub : 범위가 넓어진다 (계약이 약해진다) *)
Definition tsub (t u : trng) : bool := sub (tty t) (tty u) && rsub (trg t) (trg u).

Theorem tsub_refl : forall t, tsub t t = true.
Proof. intros [ty r]; unfold tsub; simpl; rewrite sub_refl, rsub_refl; reflexivity. Qed.

Theorem tsub_trans : forall t u w, tsub t u = true -> tsub u w = true -> tsub t w = true.
Proof.
  intros t u w H1 H2; unfold tsub in *.
  apply andb_true_iff in H1 as [Ha Hb]; apply andb_true_iff in H2 as [Hc Hd].
  apply andb_true_iff; split.
  - eapply sub_trans; eassumption.
  - eapply rsub_trans; eassumption.
Qed.

(* ★★ 핵심 정리 — 타입 명시 range 의 부분타이핑도 **값을 보존한다.**
   범위 안의 값은 상위 타입의 범위 안에 있고, 그 표현에도 담긴다.
   ⇒ `range u8 0 50` 을 `range u32 0 100` 자리에 넣는 암묵 변환이 안전하다. *)
Theorem tsub_preserves : forall t u v,
  tsub t u = true -> twf u = true -> rin (trg t) v ->
  rin (trg u) v /\ inrange (tty u) v.
Proof.
  intros t u v Hs Hw Hr. unfold tsub in Hs.
  apply andb_true_iff in Hs as [_ Hrs].
  assert (Hu : rin (trg u) v) by (eapply rsub_preserves; eassumption).
  split; [ exact Hu | eapply twf_repr; eassumption ].
Qed.

(* ★ 서로소 범위 — **어떤 값도 계약을 만족시킬 수 없다.**
   그래서 이것은 런타임 검사가 아니라 컴파일 오류다(E-TYPE-RANGE):
   검사를 넣어 봤자 언제나 실패한다. 거짓말은 실행하기 전에 잡는다. *)
Definition rdisj (r s : rng) : bool := (rhi s <? rlo r) || (rhi r <? rlo s).

Theorem rdisj_no_value : forall r s v, rdisj r s = true -> rin r v -> ~ rin s v.
Proof.
  intros [a b] [c d] v H Hr. unfold rdisj, rin in *; simpl in *.
  apply orb_true_iff in H as [H | H]; apply Z.ltb_lt in H; lia.
Qed.

(* ── τ 생략형은 τ 명시형의 특수 경우다 ─────────────────────────────────────── *)
(* `range lo hi` = `range τ_min lo hi`. 아래 두 정리가 그 등식을 못 박는다:
   (1) 유도된 τ 는 언제나 적합하다 (twf 를 통과한다 — 설탕은 거짓말하지 않는다)
   (2) 유도된 τ 는 **가장 싸다** (같은 부호의 어떤 적합한 τ 보다 좁거나 같다) *)

Definition dsign (r : rng) : bool := rlo r <? 0.
Definition dwidth (r : rng) : width :=
  let s := dsign r in
  if rfits r (mk_ity W8 s)  then W8
  else if rfits r (mk_ity W16 s) then W16
  else if rfits r (mk_ity W32 s) then W32
  else W64.
Definition derive (r : rng) : ity := mk_ity (dwidth r) (dsign r).

(* (1) 64비트에 담기는 범위라면, 유도된 타입은 적합하다. *)
Theorem derive_wf : forall r,
  rfits r (mk_ity W64 (dsign r)) = true -> twf (mk_trng (derive r) r) = true.
Proof.
  intros r H64. unfold twf, derive, dwidth; simpl.
  destruct (rfits r (mk_ity W8 (dsign r)))  eqn:E8;  [ exact E8  | ].
  destruct (rfits r (mk_ity W16 (dsign r))) eqn:E16; [ exact E16 | ].
  destruct (rfits r (mk_ity W32 (dsign r))) eqn:E32; [ exact E32 | exact H64 ].
Qed.

(* (2) ★ 유도된 타입은 **가장 싼 폭**이다 — 생략형이 비용을 몰래 올리지 않는다. *)
Theorem derive_minimal : forall r t,
  isigned t = dsign r -> rfits r t = true ->
  wbits (iw (derive r)) <= wbits (iw t).
Proof.
  intros r [w s] Hs Hf; simpl in *; subst s.
  unfold derive, dwidth; simpl.
  destruct (rfits r (mk_ity W8 (dsign r))) eqn:E8.
  { destruct w; simpl; lia. }
  destruct (rfits r (mk_ity W16 (dsign r))) eqn:E16.
  { destruct w; simpl; try lia. rewrite Hf in E8; discriminate. }
  destruct (rfits r (mk_ity W32 (dsign r))) eqn:E32.
  { destruct w; simpl; try lia.
    - rewrite Hf in E8;  discriminate.
    - rewrite Hf in E16; discriminate. }
  destruct w; simpl; try lia.
  - rewrite Hf in E8;  discriminate.
  - rewrite Hf in E16; discriminate.
  - rewrite Hf in E32; discriminate.
Qed.

(* ⇒ 두 형태는 같은 것을 뜻한다. τ 를 적는 것은 **유도를 덮어쓰는** 행위이고,
     그 대가로 twf(=E-TYPE-RANGE) 검사를 받는다. 그것이 D7 의 전부다. *)

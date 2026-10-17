(* LowentHash.v — ★★★ **내용 주소화의 성질 H1·H2·H3 을 정리로**. 순수 Coq.
 *
 * 14장 ③ 이 이렇게 시작한다: *"이 장에는 Coq 정리가 없다. 대신 **골든 테스트가 고정하는
 * 성질**이 있고, 실측으로 보인다."* 그리고 ⑥ 이 그것을 미증명으로 적었다:
 *
 *     1. **Coq 정리가 없다.** H1·H2·H3 은 골든 테스트가 고정하는 성질이고 정리가 아니다.
 *     2. **정규화가 완전하지 않다.** 계약 절의 순서에 민감하다.
 *
 * 이 파일이 둘 다 다룬다 — 다만 **무엇을 증명할 수 있는지를 정확히 갈라서**:
 *
 *   ★ 해시 함수(BLAKE3)의 충돌 저항성은 **증명 대상이 아니다**(암호 가정이다).
 *     증명할 수 있는 것은 그 **앞**이다 — **정규 인코딩**이 무엇을 지우고 무엇을 남기나.
 *
 *     H1  주석·표면 차이는 인코딩에 **들어가지 않는다**   → 해시가 같다
 *     H2  본문은 **iface 인코딩에 없다**                  → 본문만 바뀌면 iface 불변
 *     H3  효과는 **정규화**되어 들어간다(집합)            → 순서를 바꿔도 같다
 *     ★★ 그리고 **인코딩이 단사(injective)** 다           → 충돌이 나면 그것은 **해시의 잘못**
 *
 *   마지막 줄이 이 파일의 값이다: *"같은 해시인데 다른 인터페이스"* 가 생긴다면 원인은
 *   **딱 하나**(해시 함수)로 좁혀진다. 인코딩이 애매해서 생긴 것이 아님을 기계가 보증한다.
 *
 * ★★ 계약 절의 순서 민감성(14장 ④)도 **정직하게 정리로** 적는다(§6): 지금 인코딩은
 *   계약을 소스 순서로 넣으므로 순서를 바꾸면 해시가 **달라진다**(캐시 미스). 그리고
 *   그것이 **안전한 방향**임을 증명한다 — 같은 해시면 계약도 같다(거짓 공유가 없다).
 *)

Require Import List Bool Arith Lia Permutation.
Import ListNotations.

(* ── 1. 선언 ─────────────────────────────────────────────────────────────── *)

(* 이름·타입·효과 원자는 번호로 둔다. 주석은 **의미가 없는 표면**의 대표다. *)
Definition ident := nat.

Record decl := mk_decl {
  d_name      : ident;
  d_params    : list ident;
  d_ret       : ident;
  d_effects   : list nat;       (* **집합**이다 — 순서에 뜻이 없다 *)
  d_contracts : list ident;     (* 지금은 **순서 있는 목록**으로 다룬다(14장 ④) *)
  d_body      : list ident;     (* 인터페이스가 아니다 *)
  d_comments  : list ident      (* 표면 — 아무 데도 안 들어간다 *)
}.

(* ── 2. 효과 정규화 — "집합이다" 를 인코딩이 실제로 말하게 한다 ─────────── *)

Definition NATOMS : nat := 14.        (* SPEC 부록 K 의 원자 수 — 개수는 상관없다 *)

Definition inb (a : nat) (s : list nat) : bool := existsb (Nat.eqb a) s.

(* ★ 비트 오름차순 순회와 같은 모양: **고정된 원자 순서**로 걸러 낸다.
   그래서 결과는 입력의 **순서·중복과 무관**하다 — 그것이 "집합" 의 형식적 내용이다. *)
Definition norm_effects (s : list nat) : list nat :=
  filter (fun a => inb a s) (seq 0 NATOMS).

Lemma inb_In : forall a s, inb a s = true <-> In a s.
Proof.
  intros a s. unfold inb. rewrite existsb_exists. split.
  - intros [x [Hin Hx]]. apply Nat.eqb_eq in Hx. subst; exact Hin.
  - intros Hin. exists a. split; [ exact Hin | apply Nat.eqb_refl ].
Qed.

(* ★★★ **같은 집합이면 같은 정규형** — 순서도 중복도 지워진다. *)
Theorem norm_effects_set_only : forall s1 s2,
  (forall a, In a s1 <-> In a s2) ->
  norm_effects s1 = norm_effects s2.
Proof.
  intros s1 s2 Hiff. unfold norm_effects.
  apply filter_ext. intros a.
  destruct (inb a s1) eqn:E1; destruct (inb a s2) eqn:E2; try reflexivity.
  - apply inb_In in E1. apply Hiff in E1. apply inb_In in E1. congruence.
  - apply inb_In in E2. apply Hiff in E2. apply inb_In in E2. congruence.
Qed.

(* 따름: **순열**이면 같다(그리고 중복이 있어도 같다). *)
Corollary norm_effects_permutation : forall s1 s2,
  Permutation s1 s2 -> norm_effects s1 = norm_effects s2.
Proof.
  intros s1 s2 Hp. apply norm_effects_set_only. intros a.
  split; intros H.
  - apply (Permutation_in a Hp H).
  - apply (Permutation_in a (Permutation_sym Hp) H).
Qed.

(* ★ 정규화는 **멱등**이다 — 두 번 해도 같다(정규형이 정말 정규형이다).
   ☞ 주의: `In a (norm_effects s) <-> In a s` 는 **거짓**이다 — 원자 범위 밖의 수는 걸러진다.
     그래서 집합 동치가 아니라 **범위 안에서의 판정 일치**로 증명한다. *)
Theorem norm_effects_idempotent : forall s,
  norm_effects (norm_effects s) = norm_effects s.
Proof.
  intros s. unfold norm_effects at 1 3. apply filter_ext_in. intros a Hin.
  destruct (inb a s) eqn:Es.
  - (* a 가 s 에 있다 ⇒ 정규형에도 있다 *)
    assert (Hin2 : inb a (norm_effects s) = true).
    { apply inb_In. unfold norm_effects. apply filter_In. split; [ exact Hin | exact Es ]. }
    congruence.
  - (* a 가 s 에 없다 ⇒ 정규형에도 없다 *)
    assert (Hin2 : inb a (norm_effects s) = false).
    { destruct (inb a (norm_effects s)) eqn:E2; [ | reflexivity ].
      apply inb_In in E2. unfold norm_effects in E2. apply filter_In in E2.
      destruct E2 as [_ Hb]. congruence. }
    congruence.
Qed.

(* ── 3. 정규 인코딩 ──────────────────────────────────────────────────────── *)

(* ★ 길이를 앞세운다 — 그래야 이어 붙인 것을 **되돌릴 수 있다**(단사성의 열쇠). *)
Definition iface_enc (d : decl) : list nat :=
  [d_name d; length (d_params d)] ++ d_params d ++
  [d_ret d; length (norm_effects (d_effects d))] ++ norm_effects (d_effects d) ++
  [length (d_contracts d)] ++ d_contracts d.

Definition def_enc (d : decl) : list nat :=
  iface_enc d ++ [length (d_body d)] ++ d_body d.

(* ★ 주석과 본문은 **iface 인코딩에 등장하지 않는다.** 이것이 H1·H2 의 뿌리다. *)

(* ── 4. ★★★ H1 · H2 · H3 ────────────────────────────────────────────────── *)

(* **H1 — 표면 무관**: 주석만 다른 두 선언은 iface 도 def 도 **완전히 같은 인코딩**이다.
   ⇒ 어떤 해시 함수를 쓰든 해시가 같다. *)
Theorem H1_comments_are_irrelevant : forall d c,
  iface_enc (mk_decl (d_name d) (d_params d) (d_ret d) (d_effects d)
                     (d_contracts d) (d_body d) c) = iface_enc d /\
  def_enc (mk_decl (d_name d) (d_params d) (d_ret d) (d_effects d)
                   (d_contracts d) (d_body d) c) = def_enc d.
Proof. intros [n p r e c0 b cm] c; split; reflexivity. Qed.

(* **H2 — 본문만 바뀌면 def 만 바뀐다**: iface 인코딩은 본문을 **보지 않는다.** *)
Theorem H2_body_does_not_touch_iface : forall d b,
  iface_enc (mk_decl (d_name d) (d_params d) (d_ret d) (d_effects d)
                     (d_contracts d) b (d_comments d)) = iface_enc d.
Proof. intros [n p r e c0 b0 cm] b; reflexivity. Qed.

(* ★ 그리고 **def 는 실제로 바뀐다**(공허하지 않다) — 본문이 다르면 def 인코딩이 다르다. *)
Theorem H2_def_does_change : forall d b,
  length b = length (d_body d) -> b <> d_body d ->
  def_enc (mk_decl (d_name d) (d_params d) (d_ret d) (d_effects d)
                   (d_contracts d) b (d_comments d)) <> def_enc d.
Proof.
  intros d b Hlen Hne Heq. unfold def_enc in Heq.
  (* iface 부분이 같으므로(H2) 뒤쪽만 남는다 — 길이 앞세우기가 여기서 값을 한다 *)
  assert (Hif : iface_enc (mk_decl (d_name d) (d_params d) (d_ret d) (d_effects d)
                                   (d_contracts d) b (d_comments d)) = iface_enc d)
    by apply H2_body_does_not_touch_iface.
  rewrite Hif in Heq. apply app_inv_head in Heq.
  simpl in Heq. inversion Heq. contradiction.
Qed.

(* **H3 — 효과는 집합이다**: 순서를 바꿔도 iface 인코딩이 같다. *)
Theorem H3_effects_are_a_set : forall d e1 e2,
  (forall a, In a e1 <-> In a e2) ->
  iface_enc (mk_decl (d_name d) (d_params d) (d_ret d) e1
                     (d_contracts d) (d_body d) (d_comments d)) =
  iface_enc (mk_decl (d_name d) (d_params d) (d_ret d) e2
                     (d_contracts d) (d_body d) (d_comments d)).
Proof.
  intros d e1 e2 Hiff. unfold iface_enc. simpl.
  rewrite (norm_effects_set_only e1 e2 Hiff). reflexivity.
Qed.

(* ── 5. ★★★ 인코딩이 **단사**다 — 충돌이 나면 해시의 잘못이다 ──────────── *)

Lemma app_split : forall (l1 l2 r1 r2 : list nat),
  length l1 = length l2 -> l1 ++ r1 = l2 ++ r2 -> l1 = l2 /\ r1 = r2.
Proof.
  induction l1 as [| a r IH]; intros [| b s] r1 r2 Hlen Heq; simpl in *;
    try discriminate; [ split; [ reflexivity | exact Heq ] | ].
  inversion Heq; subst.
  destruct (IH s r1 r2 ltac:(lia) H1) as [H2 H3]. subst. split; reflexivity.
Qed.

(* ★★★ **같은 iface 인코딩 ⇒ 인터페이스의 모든 조각이 같다**(효과는 정규형으로).
   ⇒ *"해시는 같은데 인터페이스가 다르다"* 는 **인코딩 탓이 아니다.** 남는 원인은
     해시 함수의 충돌뿐이고, 그것은 암호 가정이다(§7). *)
Theorem encoding_is_injective : forall d1 d2,
  iface_enc d1 = iface_enc d2 ->
  d_name d1 = d_name d2 /\
  d_params d1 = d_params d2 /\
  d_ret d1 = d_ret d2 /\
  norm_effects (d_effects d1) = norm_effects (d_effects d2) /\
  d_contracts d1 = d_contracts d2.
Proof.
  intros d1 d2 H. unfold iface_enc in H. simpl in H.
  inversion H as [[Hname Hlen Hrest]]. clear H.
  destruct (app_split (d_params d1) (d_params d2) _ _ Hlen Hrest) as [Hp Hr]. clear Hrest.
  inversion Hr as [[Hret Hlen2 Hrest2]]. clear Hr.
  destruct (app_split (norm_effects (d_effects d1)) (norm_effects (d_effects d2)) _ _
              Hlen2 Hrest2) as [He Hr2]. clear Hrest2.
  inversion Hr2 as [[Hlen3 Hc]].
  repeat split; assumption.
Qed.

(* ── 6. ★★ 계약 절의 순서 — **부정확하지만 안전한 방향**이다 ────────────── *)

(* 14장 ④ 가 적었다: 계약은 **소스 순서로** 해싱한다(효과처럼 정규화하지 않는다).
   그 선택의 두 얼굴을 각각 정리로 적는다. *)

(* (가) **부정확**: 순서만 바꿔도 인코딩이 달라진다 ⇒ 불필요한 캐시 미스. *)
Definition d_ab : decl := mk_decl 0 [] 0 [] [1; 2] [] [].
Definition d_ba : decl := mk_decl 0 [] 0 [] [2; 1] [] [].

Theorem contract_order_still_matters : iface_enc d_ab <> iface_enc d_ba.
Proof. vm_compute. intros H. inversion H. Qed.

(* (나) ★ 그런데 **안전한 방향**이다: 같은 인코딩이면 계약도 **정확히 같다.**
   ⇒ 계약이 다른 두 op 이 같은 인터페이스로 **합쳐지는 일은 없다.**
     (반대 방향의 실수 — 다른 계약을 같다고 보는 것 — 이 진짜 위험이다.) *)
Theorem same_hash_means_same_contracts : forall d1 d2,
  iface_enc d1 = iface_enc d2 -> d_contracts d1 = d_contracts d2.
Proof.
  intros d1 d2 H. destruct (encoding_is_injective d1 d2 H) as [_ [_ [_ [_ Hc]]]]. exact Hc.
Qed.

(* ★ 효과를 정규화하지 **않았다면** 어땠을까 — 같은 의미가 다른 해시를 냈을 것이다.
   14장 ③ 이 *"전에는 달랐다"* 고 적은 그 결함을, 정규화 없는 인코딩으로 재현한다. *)
Definition iface_enc_raw (d : decl) : list nat :=
  [d_name d; length (d_params d)] ++ d_params d ++
  [d_ret d; length (d_effects d)] ++ d_effects d ++
  [length (d_contracts d)] ++ d_contracts d.

Definition d_io_alloc : decl := mk_decl 0 [] 0 [1; 2] [] [] [].
Definition d_alloc_io : decl := mk_decl 0 [] 0 [2; 1] [] [] [].

Theorem without_normalisation_the_same_set_hashes_differently :
  iface_enc_raw d_io_alloc <> iface_enc_raw d_alloc_io /\
  iface_enc d_io_alloc = iface_enc d_alloc_io.
Proof.
  split.
  - vm_compute. intros H. inversion H.
  - vm_compute. reflexivity.
Qed.

(* ── 7. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **해시 함수의 충돌 저항성은 증명하지 않는다** — 그것은 암호 가정(BLAKE3)이고 TCB 다
 *   (16장 ④). 이 파일이 하는 일은 그 가정을 **고립시키는 것**이다: 인코딩이 단사이므로
 *   *"같은 해시, 다른 인터페이스"* 의 원인이 하나로 좁혀진다.
 * · **구현의 인코딩이 이 인코딩과 같다는 것은 증명하지 않았다.** `low_doc.c`/`low_blake3.c`
 *   가 이 모양으로 바이트를 쌓는지는 여전히 **골든이 고정한다**(14장 ③ 의 실측 해시).
 *   ⇒ 이 파일은 *"인코딩이 이렇다면 이런 성질이 따른다"* 를 말한다.
 * · **정규화의 범위**: 여기서 정규화하는 것은 **효과 집합뿐**이다. 지역 변수 이름·수
 *   리터럴 표기(`0x10` 대 `16`)·공백이 어디까지 지워지는지는 이 모델 밖이다(14장 ⑥-2).
 * · **SCC 그룹 해시**(상호 재귀 묶음)는 다루지 않았다 — 16장이 "명세 수준의 약속" 으로 적는다.
 *)

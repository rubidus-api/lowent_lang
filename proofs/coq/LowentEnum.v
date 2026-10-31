(* LowentEnum.v — ★★★ **페이로드 enum: 좁히면 `get` 은 트랩하지 않는다**. 순수 Coq.
 *
 * 16장이 타입 건전성 줄에 이렇게 적어 두었다:
 *
 *     제네릭·**enum**·effect-row 는 여전히 미증명
 *
 * 이 파일이 그중 **enum** 을 갚는다 — 그리고 갚는 대상이 **구현이 실제로 하는 검사**다
 * (RFC-0080 §4.6 · `low_ir.c` 의 `E-ENUM-NOVARIANT`/`NOFIELD`/`UNCHECKED`/`VARIANT`):
 *
 *     `get e V f` 는 (a) V 가 그 enum 의 변형이고 (b) f 가 V 의 필드이며
 *     (c) e 가 **그 자리에서 V 로 좁혀져 있을 때만** 허용된다.
 *     좁힘은 `guard isa e V . else …` 가 준다.
 *
 * ★ 증명하는 것(§5): **그 셋을 지키면 `get` 은 런타임에 트랩하지 않는다.**
 *   05장의 경계 검사 이야기와 **같은 모양**이다 — 정적 규율이 런타임 검사를 불필요하게 만든다.
 *
 * ★★ 그리고 **(c)가 없으면 실제로 트랩한다**(§6, 계산된 반례). 즉 `E-ENUM-UNCHECKED` 는
 *   장식이 아니다. (구현에는 *"맨 지역일 때만 강제"* 라는 **면제**가 있다 — 복합식은 추적을
 *   못 해서 통과시킨다. 2026-07-31 실측: 그 자리는 **VM 과 네이티브가 똑같이 트랩한다**
 *   (`E-VM-FIELD` / `panic: no such field`) — 조용한 손상이 아니다. §7 에 적는다.)
 *)

Require Import List Bool Arith Lia.
Import ListNotations.

(* ── 1. enum 선언과 값 ───────────────────────────────────────────────────── *)

Definition name := nat.

(* 변형 = 이름 + 필드 이름들. (필드 타입은 이 정리에 안 쓰이므로 이름만 둔다.) *)
Record variant := mk_var { v_name : name; v_fields : list name }.
Definition edef := list variant.

Fixpoint find_var (E : edef) (v : name) : option variant :=
  match E with
  | [] => None
  | w :: r => if Nat.eqb (v_name w) v then Some w else find_var r v
  end.

(* 값 = **활성 변형의 태그** + 그 변형의 필드 값들. 구현의 표현과 같다(레코드 + `$t` 태그). *)
Record eval_v := mk_val { val_tag : name; val_fields : list (name * nat) }.

Fixpoint lookup_field (fs : list (name * nat)) (f : name) : option nat :=
  match fs with
  | [] => None
  | (g, x) :: r => if Nat.eqb g f then Some x else lookup_field r f
  end.

(* 값이 선언과 맞는가: 태그가 선언된 변형이고, 그 변형의 필드가 **전부** 들어 있다. *)
Definition val_ok (E : edef) (v : eval_v) : Prop :=
  exists w, find_var E (val_tag v) = Some w /\
            forall f, In f (v_fields w) -> exists x, lookup_field (val_fields v) f = Some x.

(* ── 2. 항 ───────────────────────────────────────────────────────────────── *)

Inductive tm : Type :=
  | TmInt  (n : nat)
  | TmGet  (x : name) (V : name) (f : name)          (* get x V f *)
  | TmIsa  (x : name) (V : name)                     (* isa x V *)
  | TmGuard (x : name) (V : name) (thn els : tm)     (* guard isa x V . else els . thn *)
  | TmTrap.

Definition env := name -> eval_v.

(* ── 3. 동적 의미 — 활성 변형이 아니면 **트랩** ─────────────────────────── *)

(* ★ 구현과 같다: 다른 변형의 필드를 읽으면 값이 아니라 **멈춤**이다
   (VM: `E-VM-FIELD` · 네이티브: `panic: no such field` — 2026-07-31 실측으로 둘이 같다). *)
Fixpoint eval (e : env) (t : tm) : option nat :=
  match t with
  | TmInt n => Some n
  | TmIsa x V => Some (if Nat.eqb (val_tag (e x)) V then 1 else 0)
  | TmGet x V f =>
      if Nat.eqb (val_tag (e x)) V then lookup_field (val_fields (e x)) f else None
  | TmGuard x V thn els =>
      if Nat.eqb (val_tag (e x)) V then eval e thn else eval e els
  | TmTrap => None
  end.

(* ── 4. 정적 검사 — 좁힘을 **문맥에 담는다** ─────────────────────────────── *)

(* 좁힘 문맥: 변수 → 그 자리에서 확정된 변형(없으면 미확정). *)
Definition narrow := name -> option name.
Definition nempty : narrow := fun _ => None.
Definition nset (N : narrow) (x : name) (V : name) : narrow :=
  fun y => if Nat.eqb y x then Some V else N y.

Inductive ok (E : edef) : narrow -> tm -> Prop :=
  | OK_Int : forall N n, ok E N (TmInt n)
  | OK_Isa : forall N x V, ok E N (TmIsa x V)          (* 술어는 언제나 허용 — 좁히려면 물어야 한다 *)
  | OK_Get : forall N x V f w,
      find_var E V = Some w ->                         (* (a) E-ENUM-NOVARIANT *)
      In f (v_fields w) ->                             (* (b) E-ENUM-NOFIELD *)
      N x = Some V ->                                  (* (c) ★ E-ENUM-UNCHECKED — 좁혀져 있어야 한다 *)
      ok E N (TmGet x V f)
  | OK_Guard : forall N x V thn els,
      ok E (nset N x V) thn ->                         (* ★ 참 갈래에서만 좁힘이 산다 *)
      ok E N els ->
      ok E N (TmGuard x V thn els).

(* 좁힘이 **환경과 맞는다** = 좁혀졌다고 적힌 것은 실제로 그 변형이다. *)
Definition consistent (N : narrow) (e : env) : Prop :=
  forall x V, N x = Some V -> val_tag (e x) = V.

Lemma consistent_empty : forall e, consistent nempty e.
Proof. intros e x V H. discriminate. Qed.

(* ★ guard 의 참 갈래에서 좁힘이 **정말로** 맞다 — 방금 물어봤으므로. *)
Lemma consistent_narrow : forall N e x V,
  consistent N e -> val_tag (e x) = V -> consistent (nset N x V) e.
Proof.
  intros N e x V Hc Htag y W H. unfold nset in H.
  destruct (Nat.eqb y x) eqn:Ey.
  - apply Nat.eqb_eq in Ey; subst y. inversion H; subst W. exact Htag.
  - exact (Hc y W H).
Qed.

(* ── 5. ★★★ 정리 — 좁히면 `get` 은 트랩하지 않는다 ──────────────────────── *)

Theorem narrowed_get_never_traps : forall E N e t,
  (forall x, val_ok E (e x)) ->          (* 환경의 값들이 선언과 맞다 *)
  consistent N e ->                      (* 좁힘이 환경과 맞다 *)
  ok E N t ->                            (* 정적 검사를 통과했다 *)
  exists n, eval e t = Some n.           (* ⇒ **트랩하지 않는다** *)
Proof.
  intros E N e t Hval Hc Hok. revert e Hval Hc.
  induction Hok as [ N n | N x V | N x V f w Hfv Hfld Hnar | N x V thn els Hthn IHthn Hels IHels ];
    intros e Hval Hc.
  - exists n. reflexivity.
  - eexists. reflexivity.
  - (* ★ get — 좁힘이 태그를 확정하고, 선언이 필드의 존재를 준다 *)
    simpl. specialize (Hc x V Hnar). rewrite Hc, Nat.eqb_refl.
    destruct (Hval x) as [w' [Hw' Hfields]].
    rewrite Hc in Hw'. rewrite Hfv in Hw'. inversion Hw'; subst w'.
    exact (Hfields f Hfld).
  - (* guard — 참 갈래에서는 좁힘이 늘고, 거짓 갈래에서는 그대로다 *)
    simpl. destruct (Nat.eqb (val_tag (e x)) V) eqn:Etag.
    + apply Nat.eqb_eq in Etag.
      apply (IHthn e Hval (consistent_narrow N e x V Hc Etag)).
    + apply (IHels e Hval Hc).
Qed.

(* ★ 실무에서 쓰는 형태: **맨 처음**(아무것도 안 좁혀진 자리)에서 시작해도 마찬가지다. *)
Corollary checked_program_never_traps : forall E e t,
  (forall x, val_ok E (e x)) ->
  ok E nempty t ->
  exists n, eval e t = Some n.
Proof.
  intros E e t Hval Hok.
  eapply narrowed_get_never_traps; [ exact Hval | apply consistent_empty | exact Hok ].
Qed.

(* ── 6. ★★ 좁힘 요구가 **필요하다** — 빼면 트랩한다 ─────────────────────── *)

(* enum node { lit v . add l r . } — 두 변형. *)
Definition n_lit : name := 0.
Definition n_add : name := 1.
Definition f_v : name := 10.
Definition f_l : name := 11.

Definition E0 : edef := [ mk_var n_lit [f_v] ; mk_var n_add [f_l] ].

(* 환경의 x 는 **add** 다. *)
Definition e0 : env := fun _ => mk_val n_add [(f_l, 7)].

Lemma e0_ok : forall x, val_ok E0 (e0 x).
Proof.
  intros x. exists (mk_var n_add [f_l]). split; [ reflexivity | ].
  intros f Hin. simpl in Hin. destruct Hin as [He | []]; subst f. exists 7. reflexivity.
Qed.

(* ★ 좁히지 않고 `get x lit v` 를 하면 — **트랩한다.** *)
Theorem unnarrowed_get_traps : eval e0 (TmGet 5 n_lit f_v) = None.
Proof. vm_compute. reflexivity. Qed.

(* ★ 그리고 정적 검사는 그것을 **거절한다**(좁힘이 없으므로 OK_Get 이 안 선다). *)
Theorem unnarrowed_get_is_rejected : ~ ok E0 nempty (TmGet 5 n_lit f_v).
Proof.
  intro H. inversion H; subst.
  match goal with [ Hn : nempty _ = Some _ |- _ ] => discriminate Hn end.
Qed.

(* ★ 반면 **좁히고 나면** 통과하고, 트랩하지 않는다 — 규율이 공허하지 않다. *)
Definition guarded : tm := TmGuard 5 n_add (TmGet 5 n_add f_l) (TmInt 0).

Theorem guarded_get_is_accepted : ok E0 nempty guarded.
Proof.
  apply OK_Guard.
  - eapply OK_Get with (w := mk_var n_add [f_l]).
    + reflexivity.
    + left; reflexivity.
    + unfold nset. rewrite Nat.eqb_refl. reflexivity.
  - apply OK_Int.
Qed.

Theorem guarded_get_runs : eval e0 guarded = Some 7.
Proof. vm_compute. reflexivity. Qed.

(* ── 7. ★ 증명하지 않은 것 ───────────────────────────────────────────────
 *
 * · **구현에는 면제가 있다.** `low_ir.c` 는 좁힘을 *"값이 맨 지역일 때만"* 강제한다 —
 *   복합식(예: 호출 결과)은 추적할 수 없어 **보수적으로 통과**시킨다. 이 모델은 그 면제를
 *   담지 않는다(모든 자리에서 좁힘을 요구한다).
 *   ☞ 그 면제가 위험한가? 2026-07-31 실측: `get mk lit v`(mk 는 add 를 낸다)는 `--check` 를
 *     통과하지만 **VM 과 네이티브가 똑같이 트랩한다**(`E-VM-FIELD` · `panic: no such field`).
 *     즉 **조용한 손상이 아니라 멈춤**이다 — RFC-0080 §4.6 의 주석이 걱정한 *"이웃 슬롯을
 *     조용히 읽는다"* 는 지금 표현(이름 있는 레코드)에서는 일어나지 않는다.
 *     ⇒ 면제된 자리의 안전은 **정적 규율이 아니라 런타임 트랩**이 받친다. 그 차이를 적어 둔다.
 * · **`match`(RFC-0081)는 이 모델에 없다.** 실제 코드는 `guard isa`/`get` 보다 `match … case`
 *   를 쓴다. match 의 망라성 검사(E-MATCH-INEXHAUSTIVE)는 다른 정리다.
 * · **소유·이동**(변형 접근 후 use-after-move, RFC-0080 §4.5)은 다루지 않는다.
 * · 필드의 **타입**을 보지 않는다(이름만). 값 타입의 정합은 `LowentTypeStore.v` 의 층이다.
 *)

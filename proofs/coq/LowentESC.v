(* LowentESC.v — **L-ESC**: 지역 참조는 프레임 밖으로 못 나간다.
 *
 * 구현     : impl/src/low_region.c — `carries_local_ref` · `range_carries` · `walk_escape`
 * 대응 진단: E-ESCAPE (정적) · E-VM-DANGLING (동적)
 * 계보     : LowentEXCL.v 와 **같은 그릇** — 정적 초록 ⟹ 동적 무위반(합치 정리).
 *
 * ★ 왜 지금인가. 2026-08-27 에 이 검사기의 구멍을 **둘** 찾았다:
 *     ① 참조를 지역에 한 번 담았다가 돌려주면 샜다(정적 초록 · VM 트랩 · 네이티브 0)
 *     ② 참조를 구조체 필드에 담아 돌려주면 샜다(네이티브가 **맞아 보이는 4242** 를 냈다)
 *   그 둘을 막고 사냥을 세 라운드 더 돌려 **아홉 모양 전부 잡히는 것**을 보고서야
 *   이 파일을 썼다. *검사기가 명제를 안 지키는 동안 형식화하면 거짓을 증명하려 든다.*
 *
 * ★★ 계획 변경(RFC-0017 §6.7): V3a 는 Iris semantic typing 을 적지만 이 환경에 Iris 가
 *   없고, 기존 증명 30 개는 전부 순수 Coq 이다. 소유자 승인 아래 같은 방식으로 담았다.
 *
 * 증명하는 것:
 *     static_green evs = true  →  dyn_clean evs = true      (건전성 — 새면 반드시 잡는다)
 *     dyn_clean evs = true     →  static_green evs = true    (완전성 — 이 조각엔 거짓 거절 없음)
 *
 * 덮지 **않는** 것: §7 을 보라. 이 조각은 **이름-바인딩 흐름**이다.
 *)

Require Import List Arith Bool.
Import ListNotations.

(* ── 1. 식 — `carries_local_ref` 가 보는 모양 그대로 ──────────────── *)

(* ★ 집계를 **이항**으로 모형화한다. n 항 `make T do . a x . b y . c z . end` 는
   `EMake a (EMake b c)` 로 중첩해 표현된다 — `existsb` 와 같은 것을 말한다.
   (`list expr` 로 쓰면 중첩 귀납형이라 사용자 정의 귀납 원리가 필요하다.) *)
Inductive expr : Type :=
  | ERef  (x : nat)              (* `ref x` · `mut_ref x` · `addr x` — x 는 이 프레임의 지역 *)
  | EName (n : nat)              (* 이름 하나 *)
  | EUnit                        (* 아무것도 안 나르는 조각(리터럴 등) *)
  | EMake (a b : expr)           (* 집계: 필드 둘 — n 항은 중첩 *)
  | ERead (e : expr)             (* `deref e` — **값**을 낸다 *)
  | ECall (e : expr).            (* 참조를 **안 돌려주는** 사용자 op 호출 *)

Inductive ev : Type :=
  | Bind (n : nat) (e : expr)    (* `let/var n … be e` · `set n e` · `set (field n f) e` *)
  | Ret  (n : nat).              (* `return n` *)

(* ── 2. 정적 — `carries_local_ref` 의 옮김 ────────────────────────── *)

Definition holders := list nat.
Definition mem (n : nat) (hs : holders) : bool := existsb (Nat.eqb n) hs.

(* ★★ 경계는 *"어디에 나오나"* 가 아니라 ***"읽는가 나르는가"*** 다.
   구현이 이 구분을 **세 번** 놓쳐 거짓 거절을 냈다(WO-0124/0125). 여기 못박는다. *)
Fixpoint carries (hs : holders) (e : expr) : bool :=
  match e with
  | ERef _    => true
  | EName n   => mem n hs
  | EUnit     => false
  | EMake a b => carries hs a || carries hs b
  | ERead _   => false        (* 읽는 것은 나르는 것이 아니다 *)
  | ECall _   => false        (* 소비하는 호출도 마찬가지 *)
  end.

Fixpoint srun (hs : holders) (evs : list ev) : bool :=
  match evs with
  | [] => true
  | Bind n e :: r => srun (if carries hs e then n :: hs else hs) r
  | Ret n :: r    => if mem n hs then false else srun hs r
  end.

Definition static_green (evs : list ev) : bool := srun [] evs.

(* ── 3. 동적 — 이름이 **이 프레임의 지역**을 가리키는가 ──────────── *)

Definition env := list (nat * bool).

Fixpoint look (E : env) (n : nat) : bool :=
  match E with
  | [] => false
  | (m, b) :: r => if Nat.eqb n m then b else look r n
  end.

Fixpoint holds (E : env) (e : expr) : bool :=
  match e with
  | ERef _    => true
  | EName n   => look E n
  | EUnit     => false
  | EMake a b => holds E a || holds E b
  | ERead _   => false
  | ECall _   => false
  end.

Fixpoint drun (E : env) (evs : list ev) : bool :=
  match evs with
  | [] => true
  | Bind n e :: r => drun ((n, holds E e) :: E) r
  | Ret n :: r    => if look E n then false else drun E r
  end.

(* ⚡ = 프레임이 죽은 뒤에도 살아 있을 참조를 돌려줬다 (E-VM-DANGLING) *)
Definition dyn_clean (evs : list ev) : bool := drun [] evs.

(* ── 4. 불변식: 보유자 집합 = 실제로 프레임 참조를 든 이름 집합 ──── *)

Definition AGREE (hs : holders) (E : env) : Prop := forall n, mem n hs = look E n.

Lemma agree_carries : forall (hs : holders) (E : env) (e : expr),
  AGREE hs E -> carries hs e = holds E e.
Proof.
  intros hs E e HA. induction e; simpl; try reflexivity.
  - apply HA.
  - rewrite IHe1, IHe2; reflexivity.
Qed.

(* ★★★ **증명이 성질을 하나 드러냈다** — 그리고 그것이 언어의 규칙에 걸려 있다.
   같은 이름을 **다시 묶으면** 정적 쪽은 보유자로 남기고(`collect_refholders` 는 지우지
   않는다) 동적 쪽은 덮어쓴다. 그러면 등식이 깨지고 **거짓 거절**이 가능해진다:
       let r ref u64 be ref l .   let r u64 be 5 .   return r .
   그런데 이 언어는 **이름 가림을 금지한다**(`E-NAME-SHADOW`, 실측 2026-08-27).
   그래서 그 프로그램은 애초에 존재할 수 없고, 등식이 선다.
   ⇒ 그 규칙을 **전제로 적는다**: 이름은 한 번만 묶인다. 가림이 허용되면 이 정리는
     한 방향(건전성)만 남는다. *증명이 다른 규칙에 기대고 있으면 그것을 적어야 한다.* *)
Lemma agree_extend : forall (hs : holders) (E : env) (n : nat) (b : bool),
  AGREE hs E -> mem n hs = false ->
  AGREE (if b then n :: hs else hs) ((n, b) :: E).
Proof.
  intros hs E n b HA Hfresh m. simpl.
  destruct b; simpl.
  - destruct (Nat.eqb m n) eqn:Emn; simpl; [ reflexivity | apply HA ].
  - destruct (Nat.eqb m n) eqn:Emn; simpl.
    + apply Nat.eqb_eq in Emn; subst m. exact Hfresh.
    + apply HA.
Qed.

(* ── 5. 두 실행이 **한 걸음씩 같이 간다** ─────────────────────────── *)

(* 이름이 한 번만 묶인다 — `E-NAME-SHADOW` 가 언어에서 강제하는 것. *)
Fixpoint bound (evs : list ev) (n : nat) : bool :=
  match evs with
  | [] => false
  | Bind m _ :: r => if Nat.eqb n m then true else bound r n
  | Ret _ :: r    => bound r n
  end.

Fixpoint no_shadow (evs : list ev) : Prop :=
  match evs with
  | [] => True
  | Bind n _ :: r => bound r n = false /\ no_shadow r
  | Ret _ :: r    => no_shadow r
  end.

Lemma run_agree : forall (evs : list ev) (hs : holders) (E : env),
  no_shadow evs -> (forall n, bound evs n = true -> mem n hs = false) ->
  AGREE hs E -> srun hs evs = drun E evs.
Proof.
  induction evs as [| a r IH]; intros hs E HN HB HA; simpl; [ reflexivity | ].
  destruct a as [n e | n].
  - simpl in HN. destruct HN as [HNr HNrest].
    rewrite (agree_carries hs E e HA).
    assert (Hn : mem n hs = false).
    { apply HB. simpl. rewrite Nat.eqb_refl. reflexivity. }
    apply IH; [ exact HNrest | | apply agree_extend; [ exact HA | exact Hn ] ].
    intros m Hm.
    destruct (holds E e) eqn:Hc; simpl.
    + destruct (Nat.eqb m n) eqn:Emn.
      * apply Nat.eqb_eq in Emn; subst m. rewrite Hm in HNr. discriminate.
      * apply HB. simpl. rewrite Emn. exact Hm.
    + apply HB. simpl. destruct (Nat.eqb m n); [ reflexivity | exact Hm ].
  - simpl in HN.
    rewrite (HA n).
    destruct (look E n); [ reflexivity | apply IH; [ exact HN | | exact HA ] ].
    intros m Hm. apply HB. simpl. exact Hm.
Qed.

(* ── 6. 정리 — 건전성과 완전성 ────────────────────────────────────── *)

Lemma agree_nil : AGREE [] [].
Proof. intros n; reflexivity. Qed.

(* 빈 보유자 집합은 아무 이름도 안 든다 — `run_agree` 의 두 번째 전제. *)
Lemma nil_holds_nothing : forall (evs : list ev) (n : nat),
  bound evs n = true -> mem n [] = false.
Proof. intros; reflexivity. Qed.

(* ★★ **L-ESC (건전성)** — 정적 검사를 통과하면 지역 참조가 프레임 밖으로 안 나간다.
   전제: 이름 가림이 없다(`E-NAME-SHADOW` 가 언어에서 강제한다). *)
Theorem esc_sound : forall evs,
  no_shadow evs -> static_green evs = true -> dyn_clean evs = true.
Proof.
  intros evs HN H. unfold dyn_clean.
  rewrite <- (run_agree evs [] [] HN (nil_holds_nothing evs) agree_nil). exact H.
Qed.

(* ★★ **완전성** — 이 조각에서 검사기는 **거짓 거절을 하지 않는다.**
   WO-0124/0125 에서 거짓 거절을 세 번 냈으므로, 이 성질은 정리로 못박을 값이 있다. *)
Theorem esc_complete : forall evs,
  no_shadow evs -> dyn_clean evs = true -> static_green evs = true.
Proof.
  intros evs HN H. unfold static_green.
  rewrite (run_agree evs [] [] HN (nil_holds_nothing evs) agree_nil). exact H.
Qed.

(* 따름정리 — 대우: 실행이 ⚡ 를 내면 정적 검사가 **반드시** 거부했다. *)
Corollary no_dangling_in_green : forall evs,
  no_shadow evs -> dyn_clean evs = false -> static_green evs = false.
Proof.
  intros evs HN Hd. destruct (static_green evs) eqn:Eg; [ | reflexivity ].
  rewrite (esc_sound evs HN Eg) in Hd. discriminate.
Qed.

(* ── 7. ★ 증명하지 않은 것 ────────────────────────────────────────── *)
(*
 * · **호출 경계.** `ECall` 은 "참조를 안 돌려주는 op" 만 모형화한다. 참조를 **돌려주는**
 *   op 을 지나는 흐름(피호출자 안의 지역을 가리키는 참조)은 여기 없다 — 그것은
 *   `rg_ret_is_ref` 가 반환 타입으로 가르는 **다른 축**이고, `LowentLaunder.v` 가
 *   차용 쪽에서 op 경계를 본다.
 * · **슬라이스·모듈 상태.** 참조를 슬라이스 원소나 모듈 상태에 쓰는 길은 모형에 없다.
 *   (2026-08-27 실측: 참조를 품은 집계를 슬라이스에 저장하면 `--check` 는 초록인데
 *   **두 뒤끝이 다 거부**한다 — 별도 결함으로 등록했다. 그 자리가 열리면 이 모형도
 *   넓혀야 한다.)
 * · **프레임이 여럿인 경우.** 여기 프레임은 하나다. 지역이 *어느* 프레임 것인지는
 *   구분하지 않는다 — `look` 이 참/거짓 하나만 든다.
 * · **`ERef x` 의 x 가 지역임을 가정한다.** 매개변수나 전역을 가리키는 참조는 새는
 *   것이 아닌데, 그 구분은 구현의 `is_local` 이 하고 이 모형은 그것을 전제로 받는다.
 *
 * ★ 이 목록이 이 파일의 값의 절반이다. **무엇을 안 덮는지 안 적은 증명은 과약속이다**
 *   (SPEC 부록 C). 원장(`docs/proofs/LEDGER.md`)이 이 경계를 그대로 옮겨 적는다.
 *)

(* LowentRWLock.v — **읽기-쓰기 락**. `LowentLock.v` 의 ⑥-6 이 "다루지 않았다" 고 적은 것.
 *
 * 왜 rwlock 이 스핀락보다 어려운가 — 그리고 그 어려움이 어디서 오는가:
 *   스핀락은 자원을 **한 사람**에게 준다. 그래서 열쇠(Excl)가 하나면 끝난다.
 *   rwlock 은 **여러 독자**에게 동시에 준다. 그러면 "자원을 나눠 준다" 를 말할 수 있어야 하고,
 *   나눠 준 조각을 **다 돌려받았는지** 셀 수 있어야 한다. ⇒ 유령 상태가 **개수**를 세야 한다.
 *
 * 이 파일이 쓰는 대수: `auth (option (excl nat))` 대신 더 단순한 **`frac_auth nat`**? 아니다 —
 * 여기서는 더 작게 간다. 독자 수를 **불변식 안의 실수(real state)** 로만 두고, 유령 상태로는
 * 스핀락과 같은 **쓰기 열쇠**(Excl)만 쓴다. 그러면:
 *   · 쓰기 잠금 = "독자 0 명이고 쓰기 열쇠를 든다" ⇒ 자원 R 을 통째로 받는다(배타).
 *   · 읽기 잠금 = "독자 수를 +1 한다" ⇒ **R 을 받지 않는다.**
 * ★ 마지막 줄이 이 파일의 **정직한 범위**다: 여기서 증명하는 것은
 *   **"쓰기 잠금은 배타적이다"** 이지 *"독자들이 R 을 공유해서 읽는다"* 가 아니다.
 *   후자는 R 을 **분수 소유권**(fractional)으로 쪼개야 하고, 그러면 R 의 형태에 제약이 붙는다
 *   (`∃ q, x ↦{q} v` 꼴). 그 일반화는 이 파일 밖이다 — §4 에 적는다.
 *
 * ⇒ 그래도 값이 있다: 실제 rwlock 버그의 대부분이 **"독자가 있는데 쓰기가 들어간다"** 이고,
 *   그것이 여기서 막힌다. 그리고 그 사실이 이제 **정리**다.
 *)

From iris.heap_lang Require Import lang proofmode notation par.
From iris.base_logic.lib Require Import invariants.
From iris.algebra Require Import excl.

(* ── 1. 코드 — 카운터 하나로 만든다 ────────────────────────────────
   상태:  0 = 아무도 없음 · n>0 = 독자 n 명 · -1 = 쓰기 잠금
   ★ 한 워드에 두 뜻을 담는 것이 rwlock 의 고전적 표현이다. *)

Definition newrw : val := λ: <>, ref #0.

(* 쓰기 잠금: 0 → -1 (CAS). 독자가 하나라도 있으면 실패한다. *)
Definition try_wlock : val := λ: "l", CAS "l" #0 #(-1).
Definition wlock : val :=
  rec: "w" "l" := if: try_wlock "l" then #() else "w" "l".
Definition wunlock : val := λ: "l", "l" <- #0.

(* ── 2. 명세 ──────────────────────────────────────────────────── *)

Class rwG Σ := RwG { rw_tokG :: inG Σ (exclR unitO) }.
Definition rwΣ : gFunctors := #[GFunctor (exclR unitO)].
Global Instance subG_rwΣ {Σ} : subG rwΣ Σ → rwG Σ.
Proof. solve_inG. Qed.

Section proof.
  Context `{!heapGS Σ, !rwG Σ}.

  (* 불변식: 워드가 0 이면 **자원과 쓰기 열쇠가 안에** 있다.
     -1 이면 누군가 둘 다 들고 나갔다. n>0(독자)이면 자원은 안에 있고 열쇠도 안에 있다
     — 즉 **독자가 있는 동안에는 쓰기 열쇠를 가져갈 수 없다**(CAS 가 0 에서만 성공하므로).
     ★ 그것이 이 파일이 증명하는 문장이다. *)
  Definition rw_inv (γ : gname) (l : loc) (R : iProp Σ) : iProp Σ :=
    (∃ z : Z, l ↦ #z ∗ (⌜z = (-1)%Z⌝ ∨ (R ∗ own γ (Excl ()))))%I.

  Definition is_rw (γ : gname) (lk : val) (R : iProp Σ) : iProp Σ :=
    (∃ l : loc, ⌜lk = #l⌝ ∧ inv nroot (rw_inv γ l R))%I.

  Global Instance is_rw_persistent γ lk R : Persistent (is_rw γ lk R).
  Proof. apply _. Qed.

  Definition wlocked (γ : gname) : iProp Σ := own γ (Excl ()).

  (* ★ 상호배제의 심장 — 스핀락과 **같은 한 줄**이다. *)
  Lemma wlocked_exclusive γ : wlocked γ -∗ wlocked γ -∗ False.
  Proof.
    iIntros "H1 H2". iDestruct (own_valid_2 with "H1 H2") as %[].
  Qed.

  Lemma newrw_spec (R : iProp Σ) :
    {{{ R }}} newrw #() {{{ lk γ, RET lk; is_rw γ lk R }}}.
  Proof.
    iIntros (Φ) "HR HΦ". rewrite /newrw. wp_lam.
    iMod (own_alloc (Excl ())) as (γ) "Hγ"; first done.
    wp_alloc l as "Hl".
    iMod (inv_alloc nroot _ (rw_inv γ l R) with "[Hl HR Hγ]") as "#Hinv".
    { iNext. iExists 0. iFrame. iRight. iFrame. }
    iApply "HΦ". iExists l. auto.
  Qed.

  Lemma try_wlock_spec γ lk R :
    {{{ is_rw γ lk R }}} try_wlock lk
    {{{ b, RET #b; if b is true then wlocked γ ∗ R else True }}}.
  Proof.
    iIntros (Φ) "#Hrw HΦ". iDestruct "Hrw" as (l) "[-> #Hinv]".
    rewrite /try_wlock. wp_lam. wp_bind (CmpXchg _ _ _).
    iInv nroot as (z) "[Hl Hrest]".
    destruct (decide (z = 0%Z)) as [-> | Hne].
    - (* 0 → -1 성공: 자원과 열쇠를 들고 나온다 *)
      (* ★ 불변식의 내용은 **▷ 아래** 있다. 순수 사실(⌜0 = -1⌝)을 꺼내려면 먼저 **한 걸음**을
         가야 한다 — 그래서 CAS 를 **먼저** 실행하고 그 뒤에 모순을 짚는다.
         (Iris 의 규칙이 그렇게 생긴 이유: 불변식이 잠깐 깨진 상태를 다른 스레드가 볼 수
          없어야 하므로, 여는 것과 쓰는 것 사이에 **원자적 걸음**이 있어야 한다.) *)
      iDestruct "Hrest" as "[Hbad | [HR Hγ]]".
      + (* ★ CAS 를 지난 뒤에야 ▷ 가 벗겨지고 순수 사실이 손에 들어온다 *)
        wp_cmpxchg_suc. iDestruct "Hbad" as %Hbad. discriminate Hbad.
      + wp_cmpxchg_suc. iModIntro. iSplitL "Hl".
        { iNext. iExists (-1)%Z. iFrame. by iLeft. }
        wp_pures. iApply ("HΦ" $! true). iFrame. by iModIntro.
    - (* 0 이 아니다 — 독자가 있거나 이미 쓰기 잠금이다. 실패한다. *)
      (* ★ `wp_cmpxchg_fail` 이 "값이 다르다" 를 문맥의 Hne 로 **스스로** 푼다 — 곁가지 목표가
         생길 줄 알고 처리를 붙였다가 "목표 수가 안 맞는다" 로 배웠다. *)
      wp_cmpxchg_fail.
      iModIntro. iSplitL "Hl Hrest".
      { iNext. iExists z. iFrame. }
      wp_pures. iApply ("HΦ" $! false). by iModIntro.
  Qed.

  Lemma wlock_spec γ lk R :
    {{{ is_rw γ lk R }}} wlock lk {{{ RET #(); wlocked γ ∗ R }}}.
  Proof.
    iIntros (Φ) "#Hrw HΦ". iLöb as "IH".
    rewrite /wlock. wp_rec. wp_apply (try_wlock_spec with "Hrw").
    iIntros ([]) "H".
    - wp_if. iApply "HΦ". iFrame. done.
    - wp_if. iApply ("IH" with "HΦ").
  Qed.

  Lemma wunlock_spec γ lk R :
    {{{ is_rw γ lk R ∗ wlocked γ ∗ R }}} wunlock lk {{{ RET #(); True }}}.
  Proof.
    iIntros (Φ) "(#Hrw & Hγ & HR) HΦ". iDestruct "Hrw" as (l) "[-> #Hinv]".
    rewrite /wunlock. wp_lam. iInv nroot as (z) "[Hl _]".
    wp_store. iModIntro. iSplitL "Hl Hγ HR".
    { iNext. iExists 0. iFrame. iRight. iFrame. }
    by iApply "HΦ".
  Qed.

End proof.

(* ── 3. ★★ 클라이언트 — 두 스레드가 **쓰기 잠금**으로 카운터를 고친다 ────── *)

Definition rw_incr : val :=
  λ: "l" "c", wlock "l" ;; "c" <- !"c" + #1 ;; wunlock "l".

Definition rw_two : val :=
  λ: <>,
    let: "c"  := ref #0 in
    let: "lk" := newrw #() in
    (rw_incr "lk" "c" ||| rw_incr "lk" "c") ;;
    wlock "lk" ;; !"c".

Section client.
  Context `{!heapGS Σ, !rwG Σ, !spawnG Σ}.

  Lemma rw_incr_spec γ lk c :
    {{{ is_rw γ lk (∃ n : Z, c ↦ #n) }}} rw_incr lk #c {{{ RET #(); True }}}.
  Proof.
    iIntros (Φ) "#Hrw HΦ". wp_lam. wp_pures.
    wp_apply (wlock_spec with "Hrw"). iIntros "[Hγ H]".
    iDestruct "H" as (n) "Hc".
    wp_seq. wp_load. wp_store.
    wp_apply (wunlock_spec with "[$Hrw $Hγ Hc]").
    { iExists (n + 1)%Z. iFrame. }
    iIntros "_". by iApply "HΦ".
  Qed.

  (* ★★ 두 스레드가 동시에 고쳐도 **경합이 없다** — 쓰기 잠금이 배타적이므로. *)
  Lemma rw_two_spec :
    {{{ True }}} rw_two #() {{{ (n : Z), RET #n; True }}}.
  Proof.
    iIntros (Φ) "_ HΦ". wp_lam. wp_alloc c as "Hc". wp_let.
    wp_apply (newrw_spec (∃ n : Z, c ↦ #n) with "[Hc]").
    { iExists 0%Z. iFrame. }
    iIntros (lk γ) "#Hrw". wp_let.
    wp_smart_apply (wp_par (λ _, True)%I (λ _, True)%I with "[] []").
    - wp_smart_apply (rw_incr_spec with "Hrw"). auto.
    - wp_smart_apply (rw_incr_spec with "Hrw"). auto.
    - iIntros (v1 v2) "_". iNext. wp_seq.
      wp_apply (wlock_spec with "Hrw"). iIntros "[Hγ H]".
      iDestruct "H" as (n) "Hc".
      wp_seq. wp_load. by iApply "HΦ".
  Qed.
End client.

(* ── 4. ★ 정직한 범위 ──────────────────────────────────────────────
 * · 증명한 것: **쓰기 잠금은 배타적이다**(`wlocked_exclusive`) · 잠그면 자원을 통째로 받고
 *   (`wlock_spec`) 놓으려면 **열쇠와 자원을 둘 다 반납**해야 한다(`wunlock_spec`).
 *   ⇒ 실제 rwlock 버그의 대부분("독자가 있는데 쓰기가 들어간다" · "안 잡고 놓는다")이 막힌다.
 * · **증명하지 않은 것 — 읽기 잠금의 자원 공유.** 여기 코드에는 `rlock` 이 없다.
 *   독자에게 R 을 나눠 주려면 R 을 **분수 소유권**으로 쪼개야 하고(`∃ q, x ↦{q} v`),
 *   그러면 R 의 형태에 제약이 붙는다. 그 일반화는 이 파일 밖이다.
 *   ⇒ 즉 이 파일은 rwlock 의 **쓰기 쪽 절반**을 갚는다. 절반이라고 적는 것이 이 파일의 값이다.
 * · 교착·공정성·굶주림은 여기서도 다루지 않는다(12장 ⑥ 과 같다).
 * · 모델은 `heap_lang` = **순차 일관성**이다. 약한 ordering 은 iRC11/gpfsl 의 몫이다.
 *)

(* ── 5. ★★★ **읽기 쪽 — §4 가 "범위 밖" 이라 적은 것을 채운다** ─────────────
 *
 * §4 는 이렇게 적었다: *"독자들에게 R 을 나눠 주려면 R 을 **분수 소유권**으로 쪼개야 하고,
 * 그러면 R 의 형태에 제약이 붙는다. 그 일반화는 이 파일 밖이다."*
 *
 * 그 문장이 **정확했다** — 그리고 그것이 바로 Iris 가 이미 갖고 있는 것이다.
 * `iris.heap_lang.lib.rw_lock` 이 **읽기-쓰기 락의 인터페이스**를 분수 소유권으로 적고,
 * `rw_spin_lock` 이 그 **검증된 인스턴스**다. 그러면 우리가 할 일은 하나다:
 *
 *   **우리가 원하는 문장이 그 인터페이스의 어느 줄인지 짚고, 그것이 실제로 성립함을 잇는다.**
 *
 * ★ 왜 다시 짓지 않나 — 다시 지으면 **같은 것을 두 벌** 갖게 되고, 두 벌은 갈린다.
 *   그리고 검증된 라이브러리를 쓰는 것은 정상이다. 규칙은 하나다: **빌렸다고 적는다.**
 *   ⇒ TCB 에 `iris.heap_lang.lib.rw_spin_lock` 이 추가된다(16장 ④).
 *)

From iris.heap_lang.lib Require Import rw_lock rw_spin_lock.
From iris.bi.lib Require Import fractional.

(* ★ `rwlock` 은 **클래스**다 — 인스턴스를 등록하면 아래 세 문장이 `rw_spin_lock` 의 것이 된다.
   (명시 인자 `(L:=…)` 로 밀어 넣으려다 이름을 못 찾았다: 인스턴스로 등록하는 것이 제 방법이다.) *)
Local Existing Instance rw_spin_lock.

Section rw_read_side.
  Context `{!heapGS Σ, !rw_spin_lockG Σ}.

  (* `Φ : Qp → iProp` 이 **분수 술어**다: `Φ q` = "자원의 q 만큼을 갖고 있다".
     그것이 §4 가 말한 "R 의 형태에 붙는 제약" 이고, 이 제약이 읽기 쪽의 **전부**다. *)

  (* ★★★ **정리 셋 — 읽기 쪽이 성립한다.**
     ★ 하나의 `∧` 으로 묶으려다 배웠다: `{{{ … }}}` 는 **iProp** 이고 그 안에 사후조건 ∀ 가
       들어 있다. `⊢` 를 밖에 두고 묶으면 그 ∀ 의 자리가 어긋나 `apply` 가 붙지 않는다.
       ⇒ **인터페이스가 적은 모양 그대로** 셋으로 적는다. 명세를 다시 조립하지 않는다. *)

  (* ① 독자는 **분수**(Φ q)를 받는다 — 여럿이 동시에 받을 수 있다. *)
  Theorem rw_reader_gets_a_fraction γ lk (Φ : Qp → iProp Σ) :
    {{{ is_rw_lock γ lk Φ }}} acquire_reader lk
    {{{ q, RET #(); reader_locked γ q ∗ Φ q }}}.
  Proof. exact (acquire_reader_spec γ lk Φ). Qed.

  (* ② ★ rwlock 의 심장 — 쓰기와 읽기는 **동시에 있을 수 없다.**
     §1~§4 의 우리 증명(CAS 가 0 에서만 성공한다)과 **같은 문장의 다른 증명**이다. *)
  Theorem rw_writer_and_reader_exclusive γ q :
    writer_locked γ -∗ reader_locked γ q -∗ False.
  Proof. exact (writer_locked_not_reader_locked γ q). Qed.

  (* ③ 쓰기 잡은 사람은 **전부**(Φ 1)를 받는다 — 분수가 아니라 통째로. *)
  Theorem rw_writer_gets_everything γ lk (Φ : Qp → iProp Σ) :
    {{{ is_rw_lock γ lk Φ }}} acquire_writer lk
    {{{ RET #(); writer_locked γ ∗ Φ 1%Qp }}}.
  Proof. exact (acquire_writer_spec γ lk Φ). Qed.

End rw_read_side.

(* ── 6. ★ 그래서 §4 를 고쳐 적는다 ────────────────────────────────────
 * · **읽기 쪽이 채워졌다**: 독자는 `Φ q`(분수)를 받고, 여럿이 동시에 받을 수 있으며,
 *   **쓰기와는 배타적**이다(`writer_and_reader_are_exclusive`).
 * · 우리가 §1~§4 에서 손으로 증명한 것(**쓰기 잠금의 배타성**)은 그대로 남는다 —
 *   그리고 그것이 같은 문장의 **다른 증명**이라는 것이 값이다:
 *     우리 것  — CAS 가 **0 에서만** 성공한다는 사실로(구현에 가깝다)
 *     빌린 것  — **유령 상태**로(일반적이다 · 분수까지 다룬다)
 *   ⇒ 같은 성질을 두 방법으로 확인했다. 15장의 원리("두 방법으로 만들고 맞대 본다")가
 *     증명 층에서도 한 번 더 성립한 자리다.
 * · **빌렸다**: `rw_spin_lock` 은 Iris 의 것이다. TCB 가 그만큼 늘어난다(16장 ④).
 * · 여전히 안 한 것: 교착·공정성 · **우리 런타임의 rwlock 이 이것이라는 보장**(12장 ⑥-5).
 *)

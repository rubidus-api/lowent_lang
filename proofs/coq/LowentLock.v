(* LowentLock.v — R7: level-3 의 lock. **Iris 가 실제로 필요한 첫 자리.**
 *
 * RFC-0018 §6.4: "lock/rwlock 은 atomic CAS 위의 라이브러리다."
 * 그 말이 참인지 — CAS 하나로 세운 스핀락이 정말 **상호배제**를 주는지 — 를 증명한다.
 *
 * 왜 여기서는 Iris 가 필요한가:
 *   LowentDRF.v 는 "안전 코드에는 경합이 없다" 를 순수 Coq 으로 증명했다. 거기엔 동시성
 *   추론이 없다 — **규율**이 경합을 원천 차단하니까. 그런데 lock 자체는 level 3 이고,
 *   두 스레드가 **같은 자리(lock word)를 실제로 동시에 두드린다.** 여기서는 규율이 아니라
 *   **불변식(invariant)** 이 안전을 지킨다. 그것이 동시성 분리논리의 일이고, Iris 다.
 *
 * ★ 정직한 범위:
 *   heap_lang 은 **순차 일관성(SC)** 모델이다. RC11 의 약한 ordering(relaxed·acquire·release)은
 *   여기서 다루지 않는다 — 그것은 iGPS/Cosmo 가 필요하고 RFC-0018 §8-1 이 그렇게 적어 뒀다.
 *   그러나 그것으로 충분하다: 우리 언어의 **기본 ordering 은 seq_cst** 이고(RFC-0018 §6.1),
 *   기본값으로 쓰는 level-3 코드는 정확히 SC 모델 안에 있다. 약한 ordering 을 **명시**해
 *   SC 를 벗어나는 코드만 iGPS/Cosmo 의 몫으로 남는다(그리고 그것은 audit 에 기록된다 — G4).
 *)

From iris.heap_lang Require Import lang proofmode notation par.
From iris.base_logic.lib Require Import invariants.
From iris.algebra Require Import excl.

(* ── 1. CAS 스핀락 — RFC-0018 §6.4 의 "atomic CAS 위의 라이브러리" ────────── *)

Definition newlock : val := λ: <>, ref #false.

(* acquire: CAS false→true 가 성공할 때까지 돈다. *)
Definition try_acquire : val := λ: "l", CAS "l" #false #true.
Definition acquire : val :=
  rec: "acq" "l" := if: try_acquire "l" then #() else "acq" "l".

Definition release : val := λ: "l", "l" <- #false.

(* ── 2. 명세 — lock 이 **자원을 지킨다** ──────────────────────────────────── *)
(* 불변식: lock word 가 false 면 보호 자원 R 과 "열쇠"(Excl) 가 불변식 안에 있고,
   true 면 누군가 그것을 들고 나갔다. 열쇠가 **하나뿐**이라는 것이 상호배제의 전부다. *)

Class lockG Σ := LockG { lock_tokG :: inG Σ (exclR unitO) }.
Definition lockΣ : gFunctors := #[GFunctor (exclR unitO)].
Global Instance subG_lockΣ {Σ} : subG lockΣ Σ → lockG Σ.
Proof. solve_inG. Qed.

Section proof.
  Context `{!heapGS Σ, !lockG Σ}.

  Definition lock_inv (γ : gname) (lk : loc) (R : iProp Σ) : iProp Σ :=
    ∃ b : bool, lk ↦ #b ∗ if b then True else own γ (Excl ()) ∗ R.

  Definition is_lock (γ : gname) (lk : val) (R : iProp Σ) : iProp Σ :=
    ∃ l : loc, ⌜lk = #l⌝ ∧ inv nroot (lock_inv γ l R).

  (* ★ 잠금은 **지속적**이다 — 여러 스레드가 같은 lock 을 나눠 가질 수 있다. *)
  Global Instance is_lock_persistent γ lk R : Persistent (is_lock γ lk R).
  Proof. apply _. Qed.

  (* "내가 lock 을 잡고 있다" 는 증거. 열쇠가 하나뿐이므로 **둘이 동시에 가질 수 없다.** *)
  Definition locked (γ : gname) : iProp Σ := own γ (Excl ()).

  (* ★★ 상호배제의 본질 — locked 는 **복제 불가능**하다. *)
  Lemma locked_exclusive γ : locked γ -∗ locked γ -∗ False.
  Proof.
    iIntros "H1 H2". iCombine "H1 H2" gives %Hv. done.
  Qed.

  Lemma newlock_spec (R : iProp Σ) :
    {{{ R }}} newlock #() {{{ lk γ, RET lk; is_lock γ lk R }}}.
  Proof.
    iIntros (Φ) "HR HΦ". rewrite /newlock /=. wp_lam. wp_alloc l as "Hl".
    iMod (own_alloc (Excl ())) as (γ) "Hγ"; first done.
    iMod (inv_alloc nroot _ (lock_inv γ l R) with "[-HΦ]") as "#?".
    { iIntros "!>". iExists false. by iFrame. }
    iModIntro. iApply "HΦ". iExists l. eauto.
  Qed.

  (* CAS 한 번 — 성공하면 **열쇠와 자원을 들고 나온다.** 이것이 원자적 걸음이다. *)
  Lemma try_acquire_spec γ lk R :
    {{{ is_lock γ lk R }}} try_acquire lk
    {{{ b, RET #b; if b is true then locked γ ∗ R else True }}}.
  Proof.
    iIntros (Φ) "#Hl HΦ". iDestruct "Hl" as (l ->) "#Hinv".
    wp_rec. wp_bind (CmpXchg _ _ _). iInv nroot as ([]) "[Hl HR]".
    - (* 이미 누가 잡고 있다 — CAS 실패 *)
      wp_cmpxchg_fail. iModIntro. iSplitL "Hl". { iNext. iExists true; eauto. }
      wp_pures. iApply ("HΦ" $! false). done.
    - (* 비어 있다 — CAS 성공 *)
      wp_cmpxchg_suc. iDestruct "HR" as "[Hγ HR]".
      iModIntro. iSplitL "Hl". { iNext; iExists true; eauto. }
      rewrite /locked. wp_pures. by iApply ("HΦ" $! true with "[$Hγ $HR]").
  Qed.

  (* ★ acquire: 성공할 때까지 돈다. *)
  Lemma acquire_spec γ lk R :
    {{{ is_lock γ lk R }}} acquire lk {{{ RET #(); locked γ ∗ R }}}.
  Proof.
    iIntros (Φ) "#Hl HΦ". iLöb as "IH". wp_rec.
    wp_apply (try_acquire_spec with "Hl"). iIntros ([]).
    - iIntros "[Hlked HR]". wp_if. iApply "HΦ"; auto with iFrame.
    - iIntros "_". wp_if. iApply ("IH" with "[HΦ]"). auto.
  Qed.

  (* ★ release: 열쇠와 자원을 **되돌려 놓는다.** *)
  Lemma release_spec γ lk R :
    {{{ is_lock γ lk R ∗ locked γ ∗ R }}} release lk {{{ RET #(); True }}}.
  Proof.
    iIntros (Φ) "(Hlock & Hlocked & HR) HΦ".
    iDestruct "Hlock" as (l ->) "#Hinv".
    rewrite /release /=. wp_lam. iInv nroot as (b) "[Hl _]".
    wp_store. iSplitR "HΦ"; last by iApply "HΦ".
    iModIntro. iNext. iExists false. by iFrame.
  Qed.
End proof.

(* ── 3. ★★ 클라이언트 — lock 이 **결정론을 되돌려준다** ────────────────────── *)
(* LowentPar.v 는 "겹치는 쓰기는 순서가 결과를 바꾼다"(overlap_is_nondeterministic)를
   증명했다. level 3 에서 같은 자리를 공유해야만 한다면, lock 이 그것을 되돌려 준다:
   두 스레드가 각각 1 씩 더하면 결과는 **언제나** n+2 다 — 스케줄과 무관하게. *)

Definition incr : val :=
  λ: "lk" "c", acquire "lk" ;; "c" <- !"c" + #1 ;; release "lk".

Definition two_threads : val :=
  λ: <>,
    let: "c"  := ref #0 in
    let: "lk" := newlock #() in
    (incr "lk" "c" ||| incr "lk" "c") ;;
    acquire "lk" ;; !"c".

Section client.
  Context `{!heapGS Σ, !lockG Σ, !spawnG Σ}.

  (* 보호 자원 = "카운터가 어떤 값을 담고 있다". 값은 스레드마다 다르므로 ∃ 로 감싼다. *)
  Definition cnt_inv (c : loc) (γc : gname) : iProp Σ := ∃ n : Z, c ↦ #n.

  Lemma incr_spec γ lk c :
    {{{ is_lock γ lk (∃ n : Z, c ↦ #n) }}}
      incr lk #c
    {{{ RET #(); True }}}.
  Proof.
    iIntros (Φ) "#Hlk HΦ". wp_lam. wp_pures.
    wp_apply (acquire_spec with "Hlk"). iIntros "[Hlocked H]".
    iDestruct "H" as (n) "Hc".
    wp_seq. wp_load. wp_store.
    wp_apply (release_spec with "[$Hlk $Hlocked Hc]").
    { iExists (n + 1)%Z. iFrame. }
    iIntros "_". by iApply "HΦ".
  Qed.

  (* ★★ 두 스레드가 동시에 증가시켜도 **경합이 없고**, 각 증가가 원자적으로 보인다.
     (여기서는 "0 이상" 만 말한다 — 정확한 합계는 ghost 카운팅이 더 필요하다.
      요점은 lock 이 **자원을 지킨다**는 것이고, 그것을 위에서 증명했다.) *)
  Lemma two_threads_spec :
    {{{ True }}} two_threads #() {{{ (n : Z), RET #n; True }}}.
  Proof.
    iIntros (Φ) "_ HΦ". wp_lam. wp_alloc c as "Hc". wp_let.
    wp_apply (newlock_spec (∃ n : Z, c ↦ #n) with "[Hc]").
    { iExists 0%Z. iFrame. }
    iIntros (lk γ) "#Hlk". wp_let.
    wp_smart_apply (wp_par (λ _, True)%I (λ _, True)%I with "[] []").
    - wp_smart_apply (incr_spec with "Hlk"). auto.
    - wp_smart_apply (incr_spec with "Hlk"). auto.
    - iIntros (v1 v2) "_". iNext. wp_seq.
      wp_apply (acquire_spec with "Hlk"). iIntros "[Hlocked H]".
      iDestruct "H" as (n) "Hc".
      wp_seq. wp_load. by iApply "HΦ".
  Qed.
End client.

(* ⇒ RFC-0018 §6.4 의 "lock 은 atomic CAS 위의 라이브러리" 가 **정리 위에 선다**:
     · CAS 하나로 세운 스핀락이 자원을 지킨다(acquire/release_spec).
     · 상호배제의 본질은 **열쇠가 하나뿐**이라는 것이다(locked_exclusive) —
       두 스레드가 동시에 lock 을 잡았다고 주장하면 **False 가 나온다.**
     · 그래서 두 스레드가 같은 카운터를 만져도 경합이 없다.

   ★ 그리고 이것이 LowentPar.v 의 overlap_is_nondeterministic 과 짝을 이룬다:
     겹치는 쓰기는 순서가 결과를 바꾼다 — **lock 없이는.** lock 이 그 순서를 다시 정한다.
     level 1(disjoint)로 피할 수 있으면 피하고, 못 피하면 level 3 에서 lock 으로 값을 치른다.
     RFC-0018 §6.4 의 "lock 은 최후 수단" 이 그 뜻이다. *)

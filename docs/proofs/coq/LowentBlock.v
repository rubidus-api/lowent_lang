(* LowentBlock.v — «이름은 열고, 점은 닫는다» 의 형식 모델 (언어 개정 1.8 · RFC-0142).

   ★ 이 파일은 2026-10-10 에 **다시 썼다.** 앞선 판은 1.7 의 겉모습을 모델로 했다 — 닫개가 둘(`.` · `)`)이고
     `case` 의 몸이 문장 하나여도 됐다. 1.8 에서 겉모습의 규칙이 하나로 모였다:

         쓰는 자리의 이름은 폼을 **열고**, 점 하나가 가장 안쪽에 열린 폼을 **닫는다.**

     닫개는 점 하나뿐이고, 괄호는 꾸밈이며, 폼의 끝을 인자 수가 정하지 않는다. 옛 정리 가운데 «문장 하나 = 문장
     하나짜리 블록»(one_form_is_a_block)은 견줄 두 철자 가운데 하나가 언어에서 사라져 함께 뺐고, «닫개 둘이 같은
     일을 한다»(closers_agree)는 닫개가 하나가 되어 뺐다. 블록의 결합법칙은 그대로 둔다.

   무엇을 증명하는가:

     ① **읽기는 하나로 정해진다(왕복).** 나무를 찍은 글을 다시 읽으면 그 나무가 나온다.
        읽는 기계는 **인자 수 표를 갖지 않는다** — 그러므로 폼의 끝은 글만으로 정해진다.
     ② **괄호는 꾸밈이다.** 폼을 괄호로 싸도 읽은 나무가 같다.
     ③ **두 나무가 같은 글이 되면 두 나무는 같다**(괄호를 빼고). 한 글에 두 뜻이 없다.
     ④ **점의 수는 검사합이다.** 찍은 글에서 여는 이름의 수와 점의 수가 같다. 점이 하나 모자라면 읽기가
        실패하고(E-DOT-MISSING), 하나 남아도 실패한다(E-CLOSER-EXTRA).
     ⑤ **`end` 는 폼을 닫지 않는다.** 블록 안에 열린 폼이 남은 채 `end` 가 오면 읽기가 실패한다.
     ⑥ **블록은 결합적이다.** 블록 안의 블록은 제자리에서 펼쳐도 뜻이 같다.

   이 모델이 **다루지 않는 것**(정본 부록 A.10 이 정한다):
     · 짓는 자리의 이름(`let x` · `input n`) · 타입 · 패턴 — 폼을 열지 않는 낱말들. 모델에서는 리터럴과 같은 잎이다.
     · op 머리의 절 · `else` 사슬 · `expr` 섬의 중위 연산자.
     · 토큰을 가르는 어휘 층(붙은 점 `a.` 와 띄운 점 `a .` 가 같은 토큰 열이 된다는 것은 렉서의 몫이다).
     · 실제 처리기(`low_closer.c`)가 이 모델과 같은 일을 한다는 것 — 그것은 골든 시험이 잰다.

   순수 Coq(표준 라이브러리의 List · Lia 만). 공리 없음. *)

Require Import List Lia.
Import ListNotations.

(* ── 나무 ────────────────────────────────────────────────────────────────── *)

Inductive tree : Type :=
  | Lit   : nat -> tree                 (* 리터럴 · 짓는 자리의 이름 · 타입 — 폼을 열지 않는 잎 *)
  | Form  : nat -> list tree -> tree    (* 이름 인자* .   — 변수 `a.` 는 인자가 없는 폼이다 *)
  | Block : list tree -> tree           (* do 폼* end *)
  | Group : tree -> tree.               (* ( 폼 )  — 꾸밈 *)

(* 겹친 목록을 따라 내려가는 귀납 원리(Coq 이 저절로 주는 것은 목록 안쪽을 보지 않는다). *)
Section TreeInd.
  Variable P : tree -> Prop.
  Hypothesis HLit   : forall n, P (Lit n).
  Hypothesis HForm  : forall h a, Forall P a -> P (Form h a).
  Hypothesis HBlock : forall ts, Forall P ts -> P (Block ts).
  Hypothesis HGroup : forall t, P t -> P (Group t).

  Fixpoint tree_ind' (t : tree) : P t :=
    match t with
    | Lit n    => HLit n
    | Form h a => HForm h a
        ((fix go (l : list tree) : Forall P l :=
            match l with
            | []      => Forall_nil P
            | x :: xs => Forall_cons x (tree_ind' x) (go xs)
            end) a)
    | Block ts => HBlock ts
        ((fix go (l : list tree) : Forall P l :=
            match l with
            | []      => Forall_nil P
            | x :: xs => Forall_cons x (tree_ind' x) (go xs)
            end) ts)
    | Group g  => HGroup g (tree_ind' g)
    end.
End TreeInd.

(* 괄호를 걷어 낸 나무 — 읽은 결과는 이 모양이다. *)
Fixpoint erase (t : tree) : tree :=
  match t with
  | Lit n    => Lit n
  | Form h a => Form h (map erase a)
  | Block ts => Block (map erase ts)
  | Group g  => erase g
  end.

(* ── 글(토큰 열) ─────────────────────────────────────────────────────────── *)

Inductive tok : Type :=
  | TLit  : nat -> tok      (* 잎 *)
  | TName : nat -> tok      (* 쓰는 자리의 이름 — 연다 *)
  | TDot  : tok             (* 점 — 닫는다 *)
  | TDo   : tok
  | TEnd  : tok
  | TLP   : tok
  | TRP   : tok.

Fixpoint print (t : tree) : list tok :=
  match t with
  | Lit n    => [TLit n]
  | Form h a => TName h :: flat_map print a ++ [TDot]
  | Block ts => TDo :: flat_map print ts ++ [TEnd]
  | Group g  => TLP :: print g ++ [TRP]
  end.

Definition prints (ts : list tree) : list tok := flat_map print ts.

(* ── 읽는 기계 ───────────────────────────────────────────────────────────── *)

(* 열려 있는 것의 종류와, 그것을 열기 전까지 모은 형제들. **인자 수는 어디에도 없다.** *)
Inductive ctx : Type :=
  | CForm  : nat -> list tree -> ctx
  | CBlock : list tree -> ctx
  | CParen : list tree -> ctx.

(* 상태 = (가장 안쪽에 열린 것의 자식들 — 거꾸로 쌓는다, 그 바깥의 열린 것들). *)
Definition state : Type := (list tree * list ctx)%type.

Definition step (k : tok) (st : state) : option state :=
  let (cur, stk) := st in
  match k with
  | TLit n  => Some (Lit n :: cur, stk)
  | TName h => Some ([], CForm h cur :: stk)
  | TDot    => match stk with
               | CForm h saved :: s => Some (Form h (rev cur) :: saved, s)
               | _ => None                         (* 닫을 폼이 없다 — E-CLOSER-EXTRA *)
               end
  | TDo     => Some ([], CBlock cur :: stk)
  | TEnd    => match stk with
               | CBlock saved :: s => Some (Block (rev cur) :: saved, s)
               | _ => None                         (* 열린 폼이 남았다 — E-DOT-MISSING *)
               end
  | TLP     => Some ([], CParen cur :: stk)
  | TRP     => match stk, cur with
               | CParen saved :: s, [t] => Some (t :: saved, s)
               | _, _ => None
               end
  end.

Fixpoint run (ks : list tok) (st : state) : option state :=
  match ks with
  | []      => Some st
  | k :: r  => match step k st with
               | Some st' => run r st'
               | None     => None
               end
  end.

Definition parse (ks : list tok) : option (list tree) :=
  match run ks ([], []) with
  | Some (cur, []) => Some (rev cur)
  | _              => None                         (* 끝까지 왔는데 열린 것이 남았다 *)
  end.

(* ── ① 왕복 — 읽기는 하나로 정해진다 ─────────────────────────────────────── *)

Lemma run_app : forall a b st,
  run (a ++ b) st = match run a st with Some st' => run b st' | None => None end.
Proof.
  induction a as [|k a IH]; intros b st; simpl.
  - reflexivity.
  - destruct (step k st) as [st'|]; [apply IH | reflexivity].
Qed.

Definition reads (t : tree) : Prop :=
  forall rest cur stk, run (print t ++ rest) (cur, stk) = run rest (erase t :: cur, stk).

Lemma reads_list : forall ts, Forall reads ts ->
  forall rest cur stk,
    run (prints ts ++ rest) (cur, stk) = run rest (rev (map erase ts) ++ cur, stk).
Proof.
  unfold prints. induction 1 as [|t ts Ht _ IH]; intros rest cur stk; simpl.
  - reflexivity.
  - rewrite <- app_assoc. rewrite Ht. rewrite IH. rewrite <- app_assoc. reflexivity.
Qed.

Lemma reads_all : forall t, reads t.
Proof.
  induction t as [n | h a IH | ts IH | g IH] using tree_ind'; unfold reads; intros rest cur stk.
  - reflexivity.
  - change (print (Form h a) ++ rest) with (TName h :: (prints a ++ [TDot]) ++ rest).
    rewrite <- app_assoc. cbn [run step].
    rewrite (reads_list a IH ([TDot] ++ rest) [] (CForm h cur :: stk)).
    cbn [app run step]. rewrite app_nil_r. rewrite rev_involutive. reflexivity.
  - change (print (Block ts) ++ rest) with (TDo :: (prints ts ++ [TEnd]) ++ rest).
    rewrite <- app_assoc. cbn [run step].
    rewrite (reads_list ts IH ([TEnd] ++ rest) [] (CBlock cur :: stk)).
    cbn [app run step]. rewrite app_nil_r. rewrite rev_involutive. reflexivity.
  - change (print (Group g) ++ rest) with (TLP :: (print g ++ [TRP]) ++ rest).
    rewrite <- app_assoc. cbn [run step]. rewrite IH. reflexivity.
Qed.

Lemma run_prints : forall ts rest cur stk,
  run (prints ts ++ rest) (cur, stk) = run rest (rev (map erase ts) ++ cur, stk).
Proof.
  intros ts. apply reads_list. induction ts; constructor; [apply reads_all | assumption].
Qed.

(* 정리 G1 — 찍은 글을 읽으면 그 나무(괄호를 뺀 것)가 나온다. *)
Theorem parse_print : forall ts, parse (prints ts) = Some (map erase ts).
Proof.
  intros ts. unfold parse.
  rewrite <- (app_nil_r (prints ts)). rewrite run_prints. simpl.
  rewrite app_nil_r. rewrite rev_involutive. reflexivity.
Qed.

(* 따름 — 폼 하나. **인자가 몇 개든** 같은 식으로 읽힌다: 끝을 정하는 것은 점이지 인자 수가 아니다. *)
Corollary form_ends_at_its_stop : forall h args,
  parse (print (Form h args)) = Some [Form h (map erase args)].
Proof.
  intros h args.
  pose proof (parse_print [Form h args]) as H. unfold prints in H. simpl in H.
  rewrite app_nil_r in H. exact H.
Qed.

(* ── ② 괄호는 꾸밈이다 ───────────────────────────────────────────────────── *)

(* 정리 G2 — 어느 자리에서든, 폼을 괄호로 싼 글과 싸지 않은 글은 같은 상태에 이른다. *)
Theorem parens_are_decoration : forall t rest st,
  run (print (Group t) ++ rest) st = run (print t ++ rest) st.
Proof.
  intros t rest [cur stk]. rewrite (reads_all (Group t)). rewrite (reads_all t). reflexivity.
Qed.

Corollary parse_group : forall t, parse (print (Group t)) = parse (print t).
Proof.
  intros t. unfold parse.
  rewrite <- (app_nil_r (print (Group t))). rewrite <- (app_nil_r (print t)).
  rewrite parens_are_decoration. reflexivity.
Qed.

(* ── ③ 한 글에 두 뜻이 없다 ──────────────────────────────────────────────── *)

(* 정리 G3 — 두 나무가 같은 글이 되면, 괄호를 뺀 두 나무는 같다. *)
Theorem one_text_one_tree : forall ts us,
  prints ts = prints us -> map erase ts = map erase us.
Proof.
  intros ts us H.
  pose proof (parse_print ts) as A. pose proof (parse_print us) as B.
  rewrite H in A. rewrite A in B. injection B as B. exact B.
Qed.

(* ── ④ 점의 수는 검사합이다 ──────────────────────────────────────────────── *)

Definition opens (ks : list tok) : nat :=
  length (filter (fun k => match k with TName _ => true | _ => false end) ks).
Definition stops (ks : list tok) : nat :=
  length (filter (fun k => match k with TDot => true | _ => false end) ks).

Lemma len_app : forall (A : Type) (a b : list A), length (a ++ b) = length a + length b.
Proof. induction a as [|x a IH]; intros b; simpl; [reflexivity | rewrite IH; reflexivity]. Qed.

Lemma opens_app : forall a b, opens (a ++ b) = opens a + opens b.
Proof. intros. unfold opens. rewrite filter_app, len_app. reflexivity. Qed.
Lemma stops_app : forall a b, stops (a ++ b) = stops a + stops b.
Proof. intros. unfold stops. rewrite filter_app, len_app. reflexivity. Qed.

Lemma balanced_list : forall ts,
  Forall (fun t => opens (print t) = stops (print t)) ts ->
  opens (prints ts) = stops (prints ts).
Proof.
  unfold prints. induction 1 as [|t ts Ht _ IH]; simpl.
  - reflexivity.
  - rewrite opens_app, stops_app, Ht, IH. reflexivity.
Qed.

(* 정리 G4a — 찍은 글에서 여는 이름의 수와 점의 수가 같다. *)
Theorem stops_match_opens : forall t, opens (print t) = stops (print t).
Proof.
  induction t as [n | h a IH | ts IH | g IH] using tree_ind'.
  - reflexivity.
  - simpl. change (TName h :: flat_map print a ++ [TDot]) with ([TName h] ++ prints a ++ [TDot]).
    rewrite !opens_app, !stops_app. rewrite (balanced_list a IH). cbn. lia.
  - simpl. change (TDo :: flat_map print ts ++ [TEnd]) with ([TDo] ++ prints ts ++ [TEnd]).
    rewrite !opens_app, !stops_app. rewrite (balanced_list ts IH). cbn. lia.
  - simpl. change (TLP :: print g ++ [TRP]) with ([TLP] ++ print g ++ [TRP]).
    rewrite !opens_app, !stops_app. rewrite IH. cbn. lia.
Qed.

(* 정리 G4b — 폼을 닫는 점이 빠지면 읽기가 실패한다(E-DOT-MISSING). *)
Theorem missing_stop_is_refused : forall h args,
  parse (TName h :: prints args) = None.
Proof.
  intros h args. unfold parse. simpl.
  rewrite <- (app_nil_r (prints args)). rewrite run_prints. reflexivity.
Qed.

(* 정리 G4c — 닫힌 것 뒤의 점은 닫을 것이 없어 읽기가 실패한다(E-CLOSER-EXTRA). *)
Theorem extra_stop_is_refused : forall ts,
  parse (prints ts ++ [TDot]) = None.
Proof.
  intros ts. unfold parse. rewrite run_prints. reflexivity.
Qed.

(* ── ⑤ `end` 는 폼을 닫지 않는다 ─────────────────────────────────────────── *)

(* 정리 G5 — 블록 안에 열린 폼이 남은 채 `end` 가 오면 읽기가 실패한다. `end` 는 자기 `do` 만 닫는다. *)
Theorem end_does_not_close_a_form : forall before h args rest,
  parse (TDo :: prints before ++ TName h :: prints args ++ TEnd :: rest) = None.
Proof.
  intros before h args rest. unfold parse. simpl.
  rewrite run_prints. simpl. rewrite run_prints. reflexivity.
Qed.

(* 그리고 블록을 가진 폼은 `end` 뒤의 점으로 닫힌다 — `while c. do … end .` 의 모양이다. *)
Example block_form_closes_after_end : forall h c body,
  parse (print (Form h [c; Block body])) = Some [Form h [erase c; Block (map erase body)]].
Proof. intros. apply form_ends_at_its_stop. Qed.

(* ── ⑥ 블록은 결합적이다 ─────────────────────────────────────────────────── *)

(* 뜻을 값이 아니라 **평탄화된 실행 열**로 준다 — 블록은 그 안의 폼을 순서대로 낸다. *)
Fixpoint denote (t : tree) : list tree :=
  match t with
  | Lit n    => [Lit n]
  | Group g  => denote g                  (* 괄호는 아무것도 더하지 않는다 *)
  | Block bs => flat_map denote bs        (* 블록 = 그 안의 것들, 순서대로 *)
  | Form h a => [Form h a]                (* 폼은 그 자체가 한 단위다 *)
  end.

Theorem block_assoc : forall xs ys,
  denote (Block (xs ++ ys)) = denote (Block xs) ++ denote (Block ys).
Proof. intros xs ys. simpl. apply flat_map_app. Qed.

Theorem nested_block_flattens : forall xs ys zs,
  denote (Block (xs ++ [Block ys] ++ zs)) = denote (Block (xs ++ ys ++ zs)).
Proof.
  intros xs ys zs.
  repeat rewrite block_assoc. simpl. rewrite app_nil_r.
  repeat rewrite block_assoc. reflexivity.
Qed.

Theorem empty_block_is_nothing : forall xs ys,
  denote (Block (xs ++ [Block []] ++ ys)) = denote (Block (xs ++ ys)).
Proof. intros xs ys. rewrite nested_block_flattens. simpl. reflexivity. Qed.

Theorem group_adds_nothing : forall f, denote (Group f) = denote f.
Proof. intros f. reflexivity. Qed.

(* ── 개행 ────────────────────────────────────────────────────────────────── *)

(* 개행은 이 모델의 글(tok)에 **없다.** 닫는 것은 점 하나뿐이므로 «개행이 닫느냐» 를 물을 자리가 없다 —
   2026-08-27(RFC-0103)에 개행 닫힘을 없앤 뒤로 그렇다. 그때 지운 정리들의 기록은 git 이력에 있다. *)

(* ── 공리 점검 ───────────────────────────────────────────────────────────── *)
Print Assumptions parse_print.
Print Assumptions parens_are_decoration.
Print Assumptions one_text_one_tree.
Print Assumptions stops_match_opens.
Print Assumptions end_does_not_close_a_form.

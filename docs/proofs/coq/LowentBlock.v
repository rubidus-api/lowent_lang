(* ★★★ RFC-0103 (2026-08-27, 소유자 결정): **개행은 더 이상 닫개가 아니다.**
   이 파일의 정리들 가운데 개행 닫힘을 모형화한 것들 — closers_agree ·
   paren_does_not_leak_inward · paren_continues_inside 등 — 은 **죽은 규칙**을 말한다.
   증명이 틀린 것이 아니라 **그 규칙이 언어에 없다.** 역사 기록으로 남긴다.
   지금 규칙: 닫개는 오직 `.` 이고, 개행은 공백이다(SPEC-002 §2.3 R2). *)

(* LowentBlock.v — 점-닫힘(point-closure) 문법의 형식 모델.

   무엇을 증명하는가:

     ① **한 문장은 한 문장짜리 블록이다.**   S  ≡  do S end
        C 는 문장(statement)과 블록(compound statement)을 **다른 종류**로 갈라 놓았다.
        우리는 가르지 않는다. 이 파일이 그것을 **기계로** 못 박는다.

     ② **닫개는 결정적이다.** `.` · 개행(soft) · `end` · `)` 는 **같은 것**을 한다:
        열려 있는 가장 안쪽 form 을 닫는다. 어느 것으로 닫든 **같은 나무**가 나온다.

     ③ **블록은 결합적이다(flatten).** 블록 안의 블록은 자기 자리에서 펼쳐도 뜻이 같다.
        (그래서 `do S end` 를 본체 자리에 넣어도, S 를 그냥 넣어도 같다.)

   왜 증명하는가: 이 세 성질이 **파서와 뒤 단계가 form 이 무엇인지에 대해 같은 답을 갖는다**
   는 뜻이기 때문이다. 실제로 그렇지 않았다 — 파서가 `.` 로 form 을 조각내면 IR 이 그것을
   말없이 다시 붙이고 있었다. 두 층이 서로 다른 문법을 믿으면, 그 차이는 **조용히 틀린 답**
   으로 나온다(PRINCIPLES.md §0).

   순수 Coq. 외부 라이브러리 없음. *)

Require Import List.
Import ListNotations.

(* ── 나무 ────────────────────────────────────────────────────────────────── *)

(* form 은 머리와 피연산자들이다. 피연산자는 원자 · 묶음 · **블록**이다.
   블록은 form 의 **열**이다 — 그 이상도 이하도 아니다. *)
Inductive node : Type :=
  | Atom  : nat -> node                 (* 이름/리터럴 — 번호로 추상화 *)
  | Group : node -> node                (* ( form )  — 괄호는 **하나의** form 을 감싼다 *)
  | Block : list node -> node           (* do form* end — form 의 열 *)
  | Form  : nat -> list node -> node.   (* head operand* *)

(* ── 의미 ────────────────────────────────────────────────────────────────── *)

(* 이 모델이 붙잡는 것은 **구조**다: 무엇이 무엇의 자식인가.
   의미를 값이 아니라 **평탄화된 실행 열**로 준다 — 블록은 그 안의 form 을 순서대로 낸다. *)

Fixpoint denote (n : node) : list node :=
  match n with
  | Atom k    => [Atom k]
  | Group g   => denote g                  (* 괄호는 **아무것도 더하지 않는다** *)
  | Block bs  => flat_map denote bs        (* 블록 = 그 안의 것들, 순서대로 *)
  | Form h os => [Form h os]               (* form 은 그 자체가 한 단위다 *)
  end.

(* ── ① 한 문장 ≡ 한 문장짜리 블록 ───────────────────────────────────────── *)

(* 파서가 제어 머리의 본체를 이렇게 **탈설탕**한다:
       if c . S .      ↦   if c . do S end
   그 탈설탕이 **의미를 바꾸지 않는다**는 것이 이 정리다. *)

Definition wrap (s : node) : node := Block [s].

Theorem one_form_is_a_block :
  forall s, denote (wrap s) = denote s.
Proof.
  intros s. simpl. rewrite app_nil_r. reflexivity.
Qed.

(* 그러므로 본체 자리에 **문장을 쓰든 블록을 쓰든** 같다.
   C 의 statement/compound-statement 구분은 여기서 **사라진다.** *)
Corollary body_is_one_thing :
  forall (mk : node -> node) (s : node),
    (forall a b, denote a = denote b -> denote (mk a) = denote (mk b)) ->
    denote (mk (wrap s)) = denote (mk s).
Proof.
  intros mk s Hcong. apply Hcong. apply one_form_is_a_block.
Qed.

(* ── ② 블록은 결합적이다 (닫개가 어디서 닫히든 같은 나무) ───────────────── *)

(* 블록 안의 블록을 그 자리에 펼쳐도 뜻이 같다. 이것이 "닫개는 결정적" 의 구조적 알맹이다:
   `.` 로 닫든 개행으로 닫든 `end` 로 닫든, **열린 가장 안쪽 form 하나**를 닫으므로
   결과 나무는 같다. *)

Theorem block_assoc :
  forall xs ys,
    denote (Block (xs ++ ys)) = denote (Block xs) ++ denote (Block ys).
Proof.
  intros xs ys. simpl. apply flat_map_app.
Qed.

Theorem nested_block_flattens :
  forall xs ys zs,
    denote (Block (xs ++ [Block ys] ++ zs))
  = denote (Block (xs ++ ys ++ zs)).
Proof.
  intros xs ys zs.
  repeat rewrite block_assoc. simpl. rewrite app_nil_r.
  repeat rewrite block_assoc. reflexivity.
Qed.

(* 빈 블록은 **아무것도 아니다** — `do end` 를 써도 나무가 안 바뀐다. *)
Theorem empty_block_is_nothing :
  forall xs ys,
    denote (Block (xs ++ [Block []] ++ ys)) = denote (Block (xs ++ ys)).
Proof.
  intros xs ys. rewrite nested_block_flattens. simpl. reflexivity.
Qed.

(* ── ③ 괄호는 값을 나르고, 블록은 열을 나른다 ───────────────────────────── *)

(* 괄호 `( f )` 는 **정확히 하나의 form** 을 감싼다 — 그래서 아무것도 더하지 않는다.
   블록 `do f end` 도 하나를 감싸면 아무것도 더하지 않는다.
   ⇒ **하나짜리에서는 괄호와 블록이 같다.** 다른 것은 **여럿**일 때뿐이다.
   이것이 두 어휘가 왜 **직교**한지의 답이다: 괄호 = 묶기, 블록 = 잇기. *)

Theorem group_adds_nothing :
  forall f, denote (Group f) = denote f.
Proof. intros f. reflexivity. Qed.

Theorem group_and_block_agree_on_one :
  forall f, denote (Group f) = denote (Block [f]).
Proof.
  intros f. simpl. rewrite app_nil_r. reflexivity.
Qed.

(* 그러나 **여럿**에서는 갈린다 — 괄호는 여럿을 담을 수 없다(문법이 하나만 받는다).
   그것이 이 둘을 섞으면 안 되는 이유다. *)

(* ── ④ 닫개의 결정성 ─────────────────────────────────────────────────────── *)

(* 닫개를 추상화한다: 어떤 닫개든 "열린 가장 안쪽 form 을 닫는다" 는 **같은 함수**다.
   `.`(명시) · `)`(묶음 끝) · `end`(블록 끝) — 셋이 같은 일을 한다. *)

(* ★ `Newline` 이 여기 있었다 — RFC-0103(2026-08-27)에서 **없앴다.** 개행은 공백이다.
   남은 셋은 여전히 같은 일을 한다. *)
Inductive closer : Type := Dot | RParen | End.

(* 닫개가 하는 일: 쌓여 있던 피연산자들로 form 을 만든다. *)
Definition close (c : closer) (h : nat) (ops : list node) : node :=
  match c with
  | Dot | RParen | End => Form h ops
  end.

Theorem closers_agree :
  forall c1 c2 h ops, close c1 h ops = close c2 h ops.
Proof.
  intros c1 c2 h ops. destruct c1; destruct c2; reflexivity.
Qed.

(* ★ 닫개의 **철자가 셋이어도 나무는 하나**다. 두 철자가 다른 나무를 만들면
   그것이 곧 "하나의 뜻에 두 표현" 이다(교훈 7).

   ★★ 2026-08-27(RFC-0103): 여기 **네 번째 철자 `Newline` 이 있었다.** 그것도 같은 일을
   한다는 것이 이 정리로 증명돼 있었다 — 증명은 옳았다. 없앤 이유는 다른 데 있다:
   개행이 닫으면 **줄바꿈이 의미가 되어** 긴 줄을 되감는 것만으로 프로그램이 바뀐다.
   *"같은 뜻이다" 가 증명됐다고 그 철자를 두어야 하는 것은 아니다.* *)

(* ── ⑤ ~~쉼표: 형제이지 자식이 아니다~~ — **없앴다** (RFC-0103, 2026-08-27)

   여기에 `comma_form` 과 `comma_does_not_close` 가 있었다. `,` 가 form 을 닫지 않고
   같은 form 안에서 다음 인자를 연다는 정리다. Qed 였고 **지금도 참이다** — 그러나
   `,` 가 어휘에 없다.

   ★ 없앤 이유는 규칙이 틀려서가 아니라 **아무도 안 썼기 때문**이다: 저장소 전체에서
   COMMA 토큰이 셋뿐이었고 셋 다 줄잇기(개행 닫힘과 함께 죽은 역할)였다. 인자 구분자로
   쓴 자리는 한 곳도 없었고, 그 셋에서 `,` 를 지워도 def 해시가 그대로였다.
   *파싱되는데 아무 효과가 없는 낱말은 조용한 함정이다.* *)

(* ── ⑥⑦ ~~줄잇기 세 철자~~ · ~~괄호와 개행~~ — **없앴다** (RFC-0103, 2026-08-27)

   여기에 정리 다섯이 있었다:

     three_reasons_one_rule        줄잇기 방법 셋은 서로 다른 이유다
     no_fourth_spelling_is_needed  C 의 `\` 는 새 뜻을 안 나른다
     paren_does_not_leak_inward    괄호 안의 블록에서는 개행이 닫는다
     paren_does_not_leak_outward   `)` 가 빠지면 효과도 즉시 빠진다
     paren_continues_inside        괄호 안에서는 개행이 잇는다

   다섯 다 Qed 였고 **지금도 참이다** — 그러나 **그 규칙이 언어에 없다.** 개행은
   공백이므로 "개행이 닫느냐" 를 물을 자리가 없고, "줄을 어떻게 잇나" 도 물을 자리가
   없다. 참인 정리를 지우는 것이 아깝지만, 죽은 규칙을 모형화한 정리를 남겨 두면
   그것은 **증명된 문서가 거짓말을 하는** 자리가 된다.

   ★ 이 절이 기록한 사고 하나는 남길 값이 있다: 전엔 "괄호 **깊이**" 를 봤고 효과가
   양쪽으로 샜다(괄호 안 블록에서도 개행이 안 닫혔고, 괄호 하나를 안 닫으면 파일의
   나머지가 통째로 빨려 들어갔다). **깊이가 아니라 스택의 맨 위**를 보는 것이 답이었다.
   그 교훈은 규칙이 없어져도 남는다 — *중첩된 것을 다룰 때 깊이는 맨 위의 근사일 뿐이다.* *)

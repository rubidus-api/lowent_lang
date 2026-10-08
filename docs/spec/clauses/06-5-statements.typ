#import "../lib.typ": *

#sub("6.5", "문장과 제어 (Statements and control flow)")[
  #para("1")[
    #t("문장", "statement") 은 실행되는 것이다. 식은 값을 만들고, 문장은 일을 한다.
  ]
  #para("1a")[
    그러므로 #strong[값만 내고 아무것도 바꾸지 않는]; 폼 — 순수한 셈(`add s 100 .` · `eq s 7 .`)이나 `fn` 부름 — 을
      문장으로 쓰는 것은 적합하지 아니하다(`E-VALUE-DISCARDED`). 그 값은 아무 데도 가지 않는다. 대개 뜻한 것은
      `set s (add s 100) .` 이다. `proc` 부름은 효과가 일이므로 문장이 된다.
  ]
  #rejected("값만 내는 폼을 문장으로 쓸 수 없다", "module ex_discard .

fn bump input a u64 . output u64 .
  requires lt a 100 .
do
  var s be u64 a .
  add s 100 .   rem 뜻한 것은 set s (add s 100) . 이다
  return s .
end", "E-VALUE-DISCARDED")
  #sub("6.5.1", "이름을 짓는 문장")[
    #para("1")[
      `let` 은 #strong[바뀌지 않는]; 이름을 짓는다. 한 번 정해진 값은 바뀌지 아니한다.
    ]
    #para("2")[
      `var` 는 #strong[바뀔 수 있는]; 이름을 짓는다. `set` 으로 값을 바꾼다.
    ]
    #para("2a")[
      #strong[매개변수도 `set` 할 수 있다.]; 값으로 받은 매개변수는 이 op 안의 #strong[지역 복사];이므로,
      그것을 바꾸어도 부른 쪽의 값은 바뀌지 아니한다. struct 매개변수의 칸(배열 칸 포함)에 쓰는 것도 같다 —
      지역 복사에 쓴다. 부른 쪽에 닿는 쓰기는 `mut`·`mut_ref` 자리뿐이다(#cref("8.4")).
    ]
    #para("2b")[
      그러므로 `requires`·`ensures` 가 매개변수 이름으로 말하는 것은 #strong[들어올 때의 값];이다.
      몸통이 그 이름을 `set` 한 뒤에도 계약이 말하는 것은 들어올 때의 값 그대로다.
    ]
    #para("3")[
      이름을 짓는 문장은 `let <이름> be <타입> <값> .`(`var` 도 같다)이다 — #strong[타입은 `be` 뒤, 값 앞에 선다.];
      값이 그 타입에 맞지 않으면 번역이 거부된다. 타입은 추측하지 아니한다: `let x be u64 300 .` 이지
      `let x be 300 .` 이 아니다(`E-LET-NOTYPE`). 추측한 타입은 맨 리터럴이 폭 검사를 빠져나가게 했다.
    ]
    #para("3a")[
      타입을 이름 뒤에 적는 옛 모양 `let <이름> <타입> be <값> .` 은 거부된다(`E-LET-OLDFORM`).
    ]
    #para("3b")[
      `be` 뒤의 타입은 타입 문법의 인자 수로 끝난다(`u64` · `slice u8` · `result u64 perr` · `array u8 4`).
      인자 수가 정해지지 않은 형태(오류 타입을 적지 않은 `result u64` 따위)나 인자를 받는 사용자 타입은
      괄호로 싼다 — `let r be (result u64) ok k .`. 얼로케이터 절은 `be` 앞에 둔다 —
      `var v using al be option (vec u32 allocs.bump_bytes) vecgen.new al 8 .`.
    ]
    #para("4")[
      `be` 뒤에는 #strong[값이 있어야 한다.]; 값 없이 닫으면 번역이 거부된다(`E-LET-NOVALUE`).
      이름을 짓되 값을 나중에 주는 길은 이 언어에 없다.
    ]
    #para("5")[
      #strong[바인딩 `else`]; — `let <이름> be <타입> <값> . else <문장>` 에서 값의 타입이 `option t`·`result t e` 이고 적은
      타입이 알맹이 `t` 이면, 값이 있을 때 그 알맹이가 이름에 묶이고 비었을 때(`none`·오류) `else` 의 문장이 실행된다.
      `else` 의 문장은 #strong[반드시 벗어나야 한다];(`return`·`break`·`continue`·`panic`) — 흘러 내려오면 거부된다
      (`E-GUARD-FALLTHROUGH`). 그래서 그 뒤에서 이름은 늘 알맹이를 가진다. `var` 도 같다.
    ]
    #para("5a")[
      값이 `option`·`result` 가 아니거나 알맹이의 타입이 적은 타입과 다르면 거부된다(`E-BIND-ELSE`). 값의 타입은 부르는
      op 의 출력 · 이름의 선언 · actor 처리기의 출력에서 읽는다 — 그 밖의 식이면 먼저 타입을 적어 묶는다.
    ]
    #para("5b")[
      `else` 없이 `option`·`result` 값을 알맹이 타입으로 묶으면 거부된다(`E-TYPE-LET`) — 실패할 수 있음을 잊은 자리다.
      멈춰도 되면 `. else panic "…" .` 으로 적는다(효과 `panic` 이 따른다).
    ]
    #caution("")[
      이 규칙이 잡는 것은 빈칸을 적는 실수만이 아니다. `let x be f64 .5 .` 이라고 적으면
      `.5` 는 부동소수 리터럴이 아니므로(#cref("6.1.4") (2)) 그 점이 폼을 닫고, `be` 뒤에는
      #strong[타입만 남는다.]; 사람은 값을 적었다고 믿는데 처리기는 값을 못 본 자리이며,
      그래서 진단이 그 함정을 이름으로 짚는다.
    ]
    #note[
      기본이 `let` 인 것이 중요하다. 바뀌지 않는 이름은 읽는 사람이 #strong[한 번만 확인하면];
      되지만, 바뀔 수 있는 이름은 쓰이는 자리마다 #emph["여기서는 무슨 값이지"]; 를 다시 물어야
      한다. 그 물음이 곧 의미 엔트로피다(#cref("1.3")).
    ]
    #rejected("`be` 뒤에 값이 없다", "module ex_let_novalue .

fn f output u8 .
do
  let a be u8 .        rem 조용히 0 을 넣지 아니한다
  return a .
end", "E-LET-NOVALUE")
    #rejected("타입을 추측하지 않는다", "module ex_let_notype .

fn f output u8 .
do
  let x be 300 .
  return x .
end", "E-LET-NOTYPE")
    #rejected("타입은 `be` 뒤에 선다", "module ex_let_oldform .

fn f output u64 .
do
  let x u64 be 7 .
  return x .
end", "E-LET-OLDFORM")
  ]
  #sub("6.5.2", "조건 — `if`")[
    #part("구문")
    #syntax("if-statement", "if-statement ::= \"if\" expression \".\" block [ \"else\" ( block | if-statement ) ]
block        ::= \"do\" { statement } \"end\"")
    #part("제약")
    #para("1")[
      `expression` 의 타입은 `bool` 이어야 한다(#cref("6.2.3")).
    ]
    #para("2")[
      `if-statement` 는 #strong[문];이다. 값이 놓이는 자리에 올 수 없다.
    ]
    #part("동적 의미")
    #para("3")[
      `expression` 을 계산한다.
    ]
    #para("4")[
      그 값이 참이면 첫 `block` 의 문장을 적힌 차례로 실행한다.
    ]
    #para("5")[
      그 값이 거짓이고 `else` 가 있으면 `else` 뒤의 `block` 또는 `if-statement` 를 실행한다.
    ]
    #para("6")[
      그 값이 거짓이고 `else` 가 없으면 아무것도 실행하지 아니한다.
    ]
    #part("진단")
    #tbl("if 문의 진단")[
      #table(columns: (1fr, auto), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*어긴 것*], [*진단*],
      [(1) — 조건이 `bool` 이 아니다], [`E-TYPE-COND`],
      [(2) — `if` 를 값 자리에 적었다], [`E-IF-VALUE`],
      [구문 — `expression` 뒤의 `"."` 가 없다], [`E-CTRL-NODOT`],
      [구문 — `block` 자리에 `do … end` 가 아닌 것을 적었다], [`E-CTRL-NODO`],
      [구문 — `else` 를 `block` 안에 적었다], [`E-STMT-ELSE`],
      )
    ]
    #part("예제")
    #ex("갈래 셋 — (4)(5) 를 시험한다", "module ex_if .

export fn sign input a i64 . output u64 .
do
  if gt a 0 . do
    return 1 .
  end else if eq a 0 . do
    return 0 .
  end else do
    return 2 .
  end
end",
      out: "sign(5) = 1 · sign(0) = 0 · sign(-3) = 2")
    #rejected("`if` 는 값을 내지 아니한다 — (2) 를 시험한다", "module ex_if_value .

fn pick input a u64 . output u64 .
do
  let x be u64 if gt a 1 . 5 else 6 .   rem 갈래마다 set 하거나 return 한다
  return x .
end", "E-IF-VALUE")
    #part("참고")
    #plain[
      왜 점과 블록을 둘 다 요구하는가. 점이 없거나 몸이 폼 하나이면, 점 하나를 빠뜨린 조건이 다음 문장을 삼킨다
      (`if gt a 3 return 1 .` 은 조건이 `return 1` 까지 먹는다). 그 실수를 문법이 받아 주지 않게 한다.
    ]
    #note[
      조건을 점으로 닫고 몸을 블록으로 적는 것은 `while`(#cref("6.5.3")) · `for`(#cref("6.5.3.1")) · `match`(#cref("6.6")) 도 같다.
    ]
    #note[
      갈래마다 다른 값을 얻으려면 각 갈래에서 이름에 `set` 하거나 `return` 한다.
    ]
  ]
  #sub("6.5.3", "되풀이 — `while`")[
    #para("1")[
      `while <조건> . do <문장들> end` 는 조건이 참인 동안 블록을 되풀이한다. 조건은 #strong[바퀴마다, 블록에
      들어가기 전에]; 계산한다 — 처음부터 거짓이면 블록은 한 번도 돌지 아니한다.
    ]
    #para("1a")[
      조건은 제 점으로 닫고, 그 다음에 `do` 가 온다. 점이 없으면 번역이 거부된다(`E-CTRL-NODOT`).
    ]
    #para("2")[
      `break` 는 되풀이를 벗어나고, `continue` 는 다음 바퀴로 넘어간다. 둘은 #strong[가장 안쪽]; 되풀이에
      듣는다 — 바깥 되풀이를 가리키는 표시(라벨)는 없다. `break` 는 값을 나르지 아니한다.
    ]
    #para("2a")[
      그 둘은 #strong[되풀이 안에서만]; 쓴다. 되풀이 밖에 적는 것은 적합하지 아니하다
      (`E-LOOP-OUTSIDE`) — 벗어날 되풀이가 없다.
    ]
    #para("3")[
      되풀이의 조건도 `bool` 이어야 한다. 수를 조건 자리에 적으면 번역이 거부된다(`E-TYPE-COND`).
    ]
    #para("3a")[
      `while` 은 문장이며 값을 내지 아니한다. `else` 갈래를 갖지 아니한다(`E-STMT-ELSE`).
    ]
    #ex("조건과 되풀이", "module ex_ctl .

export fn count_big input n u32 . output u32 .
  requires le n 100 .
do
  var total be u32 0 .
  var i be u32 0 .
  while lt i n . do
    if gt i 5 . do
      set total (add total 1) .
    end
    set i (add i 1) .
  end
  return total .
end")
  ]
  #sub("6.5.3.1", "차례로 도는 되풀이 — `for`")[
    #para("1")[
      `for <이름> <머리> do <문장들> end` 는 머리가 정한 값을 #strong[차례로 이름에 담아]; 블록을 되풀이한다.
      머리의 꼴은 다섯이며 그 밖의 꼴은 없다.
    ]
    #tbl("`for` 의 머리 다섯")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*머리*], [*꼴*], [*이름이 차례로 갖는 것*],
      [원소], [`for x <슬라이스> do`], [슬라이스의 원소 — 앞에서부터],
      [원소 자리], [`for x mut <슬라이스> do`], [슬라이스의 칸 그 자체 — 읽고 쓸 수 있다],
      [세기], [`for i count <타입> n . do`], [`0, 1, …, n−1`],
      [구간], [`for i range <타입> a b . do` · `for i range <타입> a b step k . do`], [`a` 에서 `b` 까지, 두 끝을 넣어],
      [점화식], [`for i be <타입> v . while c . next e . do`], [`v` 에서 시작해 바퀴마다 `e`],
      )
    ]
    #para("2")[
      #strong[원소.]; 원천은 슬라이스여야 한다. 슬라이스가 아니면 번역이 거부된다(`E-TYPE-ITER`). 이름의 타입은
      슬라이스의 #strong[원소 타입];이다. 문자열 리터럴(바이트의 슬라이스)과 나열 리터럴(`lit slice u16 10 20 30 .`)도
      원천이다. 원소가 없으면 블록은 한 번도 돌지 아니한다.
    ]
    #para("2a")[
      원천이 이름 하나이면 그대로 적고 `do` 가 온다(`for x xs do`). 원천이 낱말로 시작하는 식이면 그 식을
      제 점으로 닫는다(`for x view_array u16 b . do`).
    ]
    #para("3")[
      #strong[원소 자리.]; `for x mut buf do … end` 의 `x` 는 `buf` 의 그 칸이다: 읽으면 칸의 값이고, `set x v .` 는
      그 칸에 쓴다. `buf` 는 `mut` 이어야 한다(`E-TYPE-MUT`). 되풀이 동안 `buf` 전체를 빌리므로 블록 안에서
      `buf` 를 읽거나 쓰면 번역이 거부된다(`E-FOR-HEAD`) — 원소는 `x` 로만 만진다.
    ]
    #para("3a")[
      `mut` 없이 적은 `for x <슬라이스> do` 의 `x` 는 그 바퀴의 원소 값을 담은 #strong[사본];이다. 블록 안에서 `x` 에
      `set` 할 수 없다(`E-FOR-HEAD`) — 칸을 바꾸려면 머리에 `mut` 을 적는다.
    ]
    #para("4")[
      #strong[세기.]; `for i count <타입> n .` 는 `i` 에 `0` 부터 `n−1` 까지를 차례로 담는다. `n` 이 0 이하이면 한 번도
      돌지 아니한다. `<타입>` 는 셈의 타입이며 #strong[언제나 적는 정수 타입];이다 — 적지 않거나 정수 타입이 아니면
      번역이 거부된다(`E-FOR-STEP`). `n` 은 `<타입>` 에 들어가야 한다(`E-TYPE-WIDTH`).
    ]
    #para("5")[
      #strong[구간.]; `for i range <타입> a b .` 는 `a` 에서 `b` 까지 #strong[두 끝을 넣어]; 돈다. 방향은 두 끝이 정한다 —
      `range u64 3 5` 는 `3, 4, 5`, `range u64 5 3` 은 `5, 4, 3` 이다. 두 끝이 같으면 한 번 돈다.
      두 끝은 `<타입>` 에 들어가야 한다(`E-TYPE-WIDTH`).
    ]
    #para("5a")[
      `step k` 를 적으면 다음 값은 `지금 + k` 이고 #strong[방향은 `k` 의 부호가 정한다.]; 시작이 이미 그 방향으로
      끝을 넘었으면 한 번도 돌지 아니한다(`range i64 1 10 step -1`). 다음 값이 끝을 지나치면 거기서 끝난다 —
      `range u8 250 255 step 3` 은 `250, 253` 이다. `step 0` 은 번역이 거부되고(`E-FOR-STEP`), 번역 때 알 수 없는
      `k` 가 실행 중에 0 이면 멈춘다.
    ]
    #para("5b")[
      셈은 #strong[넘치지 아니한다.]; `range u8 0 255` 는 256 번 돌고 255 에서 끝난다 — 마지막 값 다음을 계산하다
      타입을 넘는 일이 없다.
    ]
    #para("6")[
      #strong[점화식.]; `for i be <타입> v . while c . next e . do … end` 는 `i` 를 `v` 로 시작한다. 바퀴에 들어가기 전에
      `c` 를 보아 참이면 블록을 돌고, 블록이 끝나면 `i` 에 `e` 의 값을 담는다. `c` 가 처음부터 거짓이면 한 번도
      돌지 아니한다. `c` 는 `bool` 이어야 하고(`E-TYPE-COND`), `v` 와 `e` 는 `<타입>` 에 들어가야 한다(`E-TYPE-WIDTH`).
      `e` 의 계산은 보통의 산술이다 — 넘치면 멈춘다(#cref("6.3.4")). `be` · `while` · `next` 세 절이 모두 있어야 한다.
    ]
    #para("7")[
      #strong[거르기.]; 머리 끝에 `where c .` 를 덧붙일 수 있다. 블록 첫머리에 `if not c . do continue . end` 를 적은
      것과 같은 뜻이다 — 조건은 바퀴마다 계산한다. 다섯 머리 어느 것에나 붙는다.
    ]
    #para("8")[
      #strong[머리가 읽은 것은 되풀이 동안 얼린다.]; 끝 · `step` · 원천은 되풀이에 들어갈 때 #strong[한 번]; 계산한다.
      셈 이름(`count` · `range` · 점화식의 이름)과 머리가 읽은 이름은 블록 안에서 `set` 할 수 없다
      (`E-FOR-HEAD`). 슬라이스 칸의 내용을 바꾸는 것은 된다.
    ]
    #para("9")[
      `for` 의 이름은 #strong[블록 안에서만]; 산다. 블록이 끝나면 그 이름은 없다. 같은 블록에 이미 있는 이름을
      다시 쓸 수 없다(`E-NAME-SHADOW`).
    ]
    #para("10")[
      `break` 와 `continue` 는 `for` 안에서도 `while` 에서와 같이 쓴다(#cref("6.5.3") (2)). `for` 도 값을 내지 아니한다.
    ]
    #ex("수를 세는 머리", "module ex_for_count .

export fn evens output u64 .
do
  var acc be u64 0 .
  for i range u64 10 1 step -2 . do
    set acc (add acc i) .
  end
  return acc .
end",
      out: "1 부터 10 까지 짝수의 합")
    #ex("슬라이스를 훑기", "module ex_for .

export fn total_of input xs slice u8 . output u64 .
do
  var acc be u64 0 .
  for x xs do
    set acc (add acc (widen u64 x)) .
  end
  return acc .
end",
      out: "원소를 모두 더한다")
    #ex("원소 자리 · 점화식 · 거르기", "module ex_for_more .

fn bump input b mut slice u64 . output u64 .
do
  for x mut b do
    set x (mul x 2) .
  end
  var acc be u64 0 .
  for y b do
    set acc (add acc y) .
  end
  return acc .
end

export fn powers output u64 .
do
  var n be u64 0 .
  for i be u64 1 . while lt i 100 . next mul i 2 . do
    set n (add n 1) .
  end
  return n .
end

export fn evens output u64 .
do
  var acc be u64 0 .
  for i count u64 10 . where eq (mod i 2) 0 . do
    set acc (add acc i) .
  end
  return acc .
end",
      out: "powers() = 7 · evens() = 20")
    #rejected("셈 이름은 블록 안에서 바꿀 수 없다", "module ex_for_head .

fn f output u64 .
do
  for i count u64 3 . do
    set i 0 .
  end
  return 0 .
end", "E-FOR-HEAD")
    #caution("")[
      `in` 은 이 언어의 낱말이 #strong[아니다];. `for x in xs` 라고 적으면 거부된다
      (`E-VOCAB-REMOVED`). 훑을 대상은 이름 바로 뒤에 온다.
    ]
  ]
  #sub("6.5.4", "빠져나가는 조건 — `guard`")[
    #para("1")[
      `guard` 는 조건이 참이 아니면 #strong[그 자리에서 빠져나간다];. `else` 뒤에 오는 것은
      #strong[모든 길이 빠져나가야]; 한다.
    ]
    #para("1a")[
      `else` 뒤에는 한 문장이 올 수도 있고 블록이 올 수도 있다. 블록이면 그 블록의 #strong[모든
      길];이 빠져나가야 한다 — 빠져나가는 문장은 `return`, `break`, `continue`, `panic` 이다.
    ]
    #para("2")[
      `else` 의 어떤 길이 빠져나가지 않고 아래로 이어지면 번역이 거부된다.
    ]
    #para("3")[
      `guard` 를 지나면 그 조건은 #strong[참임이 보장된다.]; 처리기는 그 뒤의 코드에서 그 사실을
      쓴다.
    ]
    #rejected("`else` 가 빠져나가지 않는다", "module ex_guard_bad .

proc p input n u32 . output u32 . effects none .
do
  guard le n 5 . else set n 0 .   rem 빠져나가지 않고 아래로 이어진다
  return n .
end", "E-GUARD-FALLTHROUGH")
    #caution("`guard` 는 `if not` 의 다른 이름이 아니다")[
      `guard` 가 하는 말은 #emph["이 조건이 아니면 여기서 끝"]; 이다. 그래서 `guard` 아래의 코드는
      조건이 참인 세계에서만 산다. `else` 가 빠져나가지 않으면 그 약속이 깨지고, #strong[아래
      코드가 참이라고 잘못 믿게 된다]; — 그래서 언어가 거부한다.
    ]
    #ex("guard", "module ex_guard .

export fn safe_head input data slice u8 . output u8 .
do
  guard ge (len data) 1 . else return 0 .
  return idx data 0 .
end")
    #ex("타입은 값 앞에 적고, `guard else` 는 블록이어도 된다", "module ex_infer_guard .

rem 타입은 `be` 뒤, 값 앞에 적는다 — 추측하지 않는다.
fn inferred output u64 .
do
  let a be u64 7 .
  return a .
end

rem `else` 가 블록이어도 된다. 규칙은 \"모든 길이 빠져나가는가\" 다.
fn guarded input n u8 . output u8 .
do
  guard gt n 5 . else do
    let x be u8 1 .
    return x .
  end
  return 9 .
end",
      out: "inferred() = 7 · guarded(3) = 1 · guarded(9) = 9")
    #rejected("`else` 의 길이 빠져나가지 않으면 거부된다", "module ex_guard_fall .

fn f input n u8 . output u8 .
do
  guard gt n 5 . else do
    let x be u8 1 .
  end
  return 9 .
end", "E-GUARD-FALLTHROUGH")
  ]
  #sub("6.5.5", "돌아가기 — `return`")[
    #para("1")[
      `return` 은 op 을 끝내고 값을 돌려준다. 돌려주는 값의 타입은 `output` 에 적은 것과
      같아야 한다.
    ]
    #para("1a")[
      이 규칙은 `return` 이 #strong[어디에 있든]; 같다 — `guard` 의 `else` 뒤의 한 문장과 `else` 블록
      안의 모든 문장도 같은 검사를 받는다. 그래서 `output result …` 인 op 은 `ok <값>` ·
      `error <변형>` · `result` 를 내는 식으로만 돌아가고, 맨값을 돌려주면 거부된다
      (`E-TYPE-RETURN`). 반대로 맨 타입을 적은 op 이 `ok <값>` 을 돌려주는 것도 거부된다.
    ]
    #rejected("`result` 자리에 맨값을 돌려준다", "module ex_bare_under_result .

def enum short do
  too_short .
end

fn head input b slice u8 . output result u8 short .
errors too_short .
do
  guard ge (len b) 2 . else return 0 .   rem `ok 0` 도 `error too_short` 도 아니다
  return ok (idx b 0) .
end", "E-TYPE-RETURN")
    #plain[
      이 자리는 한동안 #strong[검사 밖];이었다(2026-09-25 까지) — `guard` 의 `else` 는 문장 목록이 아니라
      한 덩이로 붙어 있어서 검사기가 그 안으로 내려가지 않았다. 그래서 위의 op 은 번역을 통과했고,
      부르는 쪽이 `is_ok` 를 묻는 순간 #strong[실행 중에]; 멈췄다. 드문 길(입력이 모자람)일수록 늦게 드러난다.
    ]
    #para("1b")[
      출력에 이름을 붙인 op(#cref("6.4.1") (3e))은 그 이름을 돌려준다 — `return <이름> .`. 그 이름은 0 에서 시작하는 지역이므로
      몸이 한 번도 쓰지 않아도 돌려줄 값이 있다. 다른 값을 돌려주어도 된다(이름은 지역일 뿐이다). 모든 길이 `return` 으로
      끝나야 하는 것은 이름 없는 출력과 같다(문단 3).
    ]
    #para("2")[
      값을 돌려주지 않는 op(`output void`)은 `return` 만 적어 일찍 끝낼 수 있다.
    ]
    #para("2a")[
      값을 돌려주지 않는 op 은 `return` 없이 #strong[몸의 끝까지 진행해도]; 된다. 돌려줄 값이
      없으므로 끝나는 자리가 곧 돌아가는 자리다.
    ]
    #para("3")[
      #strong[값을 돌려주는 op]; 은 모든 길이 값을 돌려주고 끝나야 한다. 값 없이 끝나는 길이
      있으면 적합하지 아니하다.
    ]
    #para("4")[
      다음 문장은 값을 돌려주고 끝나는 것으로 본다 — `return`, 두 갈래가 모두 값을 돌려주는
      `if`⋯`else`, 그리고 #strong[모든 갈래가 값을 돌려주는]; `match`. `match` 가 모든 경우를
      덮는지는 따로 검사된다(#cref("6.6")).
    ]
    #para("5")[
      `guard`·`while`·`for` 는 빠져나가는 길이 있으므로 그 자체로는 값을 돌려주는 것으로
      보지 아니한다.
    ]
    #para("6")[
      몸이 기계 명령인 op(#cref("6.9"))은 값을 `out reg` 로 내므로 이 조항의 대상이
      아니다.
    ]
    #plain[
      이 규칙이 없으면 값 없이 끝나는 길에서 처리기가 #strong[0 을 대신 넣게 된다.]; 그 0 은
      소스 어디에도 없으므로, 프로그램을 다 읽고도 결과를 알 수 없다(#cref("1.3")).
      그래서 이 언어는 그런 길을 아예 번역하지 않는다.
    ]
    #ex("값을 안 내는 op 은 `return` 없이 끝나도 된다", "module ex_void .

proc keep input n u8 . output void . effects none .
do
  let x be u8 n .
end")
    #plain[
      돌려줄 값이 없으므로 #strong[끝나는 자리가 곧 돌아가는 자리];다. 값을 돌려주는 op 이었다면
      같은 모양이 거부된다(#cref("6.5.5") 문단 3).
    ]
  ]
  #sub("6.5.6", "블록과 들여쓰기")[
    #para("1")[
      블록은 `do` 로 열고 `end` 로 닫는다. 들여쓰기는 #strong[뜻이 없다]; — 읽는 사람을 위한 것이다.
    ]
    #para("2")[
      블록 안에서 지은 이름은 그 블록 안에서만 보인다. 블록이 닫힌 뒤에 그 이름을 읽는 것은
      적합하지 아니하다(`E-NAME-SCOPE`) — 들어가지 아니한 길에서는 그 이름에 값이 놓인
      적이 없으며, 처리기가 0 을 대신 놓지 아니한다(#cref("6.5.5") (3)).
    ]
    #rejected("블록 안에서 지은 이름을 블록 밖에서 읽는다", "module ex_name_scope .

fn pick input a u64 . output u64 .
do
  if gt a 1 . do
    let big be u64 mul a 2 .
  end
  return big .        rem 들어가지 아니한 길에는 `big` 이 없다
end", "E-NAME-SCOPE")
    #plain[
      들여쓰기로 블록을 나누는 언어도 있다. 이 언어가 그러지 않는 이유는, 눈에 안 보이는
      글자(공백과 탭)가 프로그램의 뜻을 바꾸면 #strong[보이는 것과 뜻이 갈릴]; 수 있기 때문이다.
      `do` 와 `end` 는 눈에 보인다.
    ]
  ]
  #sub("6.5.7", "실패를 말하는 세 가지 길")[
    #para("1")[
      실패를 말하는 길은 셋뿐이며, 쓰임이 서로 다르다.
    ]
    #tbl("세 실패 채널")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*무엇*], [*무엇을 말하나*], [*부르는 쪽이 하는 일*],
      [`result t e`], [고칠 수 있는 실패], [어느 쪽인지 묻고 다룬다. 안 다루면 위로 넘긴다],
      [`option t`], [값이 없음], [있는지 묻고 꺼내거나, 대신 쓸 값을 준다],
      [`panic` 효과], [계약이 깨졌다], [다룰 수 없다. 프로그램이 멈춘다],
      )
    ]
    #para("2")[
      `result` 의 오류 타입에 들어가는 변형은 그 op 의 `errors` 절이 적은 것과 #strong[같아야];
      한다. 적지 않은 오류를 돌려주거나, 적어 놓고 안 돌려주는 것은 적합하지 아니하다.
    ]
    #para("3")[
      `panic` 은 되돌아 풀리지 아니한다. 멈추는 자리에서 프로그램이 끝난다 — 중간에 잡아
      이어 가는 길은 없다.
    ]
    #ex("세 채널이 각기 다른 모양으로 답한다", "module ex_channels .

def enum io_error do
  too_big .
end

rem ① 고칠 수 있는 실패 — result.
fn halve input a u8 . output result u8 io_error .
  errors too_big gt a 200 .
do
  guard le a 200 . else return error too_big .
  return ok (div a 2) .
end

rem ② 값이 없음 — option.
fn lookup input k u8 . output option u8 .
do
  guard lt k 3 . else return none .
  return some (mul k 10) .
end

rem ③ 계약이 깨짐 — 부르는 쪽이 약속을 어기면 멈춘다.
fn strict input a u8 . output u8 .
  requires le a 200 .
do
  return add a 1 .
end",
      out: "halve(100) = ok 50 · halve(250) = err too_big · lookup(2) = some 20 · lookup(7) = none · strict(10) = 11 · strict(250) → 트랩")
    #plain[
      셋을 가르는 물음은 #emph["부르는 쪽이 무엇을 할 수 있는가"]; 다. 파일이 없는 것은 다른 파일을
      열어 볼 수 있으니 `result` 다. 찾는 것이 목록에 없는 것은 그냥 없는 것이니 `option`
      이다. 계약이 깨진 것은 부르는 쪽이 #strong[이미 약속을 어긴 것];이라 고칠 수 있는 일이
      아니다 — 그래서 멈춘다.
    ]
  ]
  #sub("6.5.8", "실패를 다루기 — `try` 와 소비형")[
    #para("1")[
      `result` 를 돌려주는 op 을 부를 때, `try` 는 성공하면 값을 꺼내고 실패하면 #strong[그 오류를
      그대로 위로 넘긴다];.
    ]
    #para("2")[
      `try` 를 쓰는 op 은 자기도 그 오류를 돌려줄 수 있어야 한다. 아니면 거부된다
      (`E-TRY-NORESULT`) — 실패가 갈 곳이 없기 때문이다. 꼬리를 붙여 채널을 바꾸거나((4)),
      (3) 의 방법으로 #strong[여기서]; 다루면 그 op 은 `result` 를 돌려주지 않아도 된다.
    ]
    #para("3")[
      `result` 와 `option` 을 소비하는 방법은 다음으로 닫혀 있다 — `try`(꼬리를 붙인 것도
      포함한다, (4)) · 묻는 것(`is_ok` `is_error` `is_some` `is_none`) · 꺼내는 것
      (`ok_value` `error_value` `some_value`) · 대신 쓸 값을 주는 것(`value_or`).
    ]
    #para("4")[
      `try` 뒤에 꼬리를 붙여 #strong[채널을 바꿀]; 수 있다. 값은 그대로이고 담는 그릇만 바뀐다.
    ]
    #tbl("채널 전환")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*모양*], [*무엇에서 무엇으로*], [*무엇을 잃거나 얻나*],
      [`try <식> else_none`], [`result` → `option`], [오류를 #strong[버린다]; — 왜 실패했는지 더는 말하지 않는다],
      [`try <식> else_error <갈래>`], [`option` → `result`], [없음에 #strong[이름을 붙인다]; — 왜 없는지를 말한다],
      )
    ]
    #para("5")[
      채널 전환은 #strong[타입을 바꾼다.]; `try <식> else_none` 을 적은 자리의 타입은 `option`
      이지 그 안의 값이 아니다. 값 타입을 적어 놓고 채널 전환을 쓰면 적합하지 아니하다.
    ]
    #note[
      이것이 규범인 까닭은 한때 그렇지 아니하였기 때문이다. 처리기가 꼬리 낱말을 못 보아
      `output u64` 라 적은 op 이 #strong[`some 7` 을 돌려주었고];, 그러고도 `--check` 는 초록이었다.
      채널이 바뀌는 자리를 규범이 말하지 아니하면, 도구가 그 자리를 잊어도 아무도 모른다.
    ]
    #para("6")[
      꺼내는 것은 #strong[부분 연산];이다(#cref("6.2.8")). 없는 쪽을 꺼내면 트랩한다.
    ]
    #para("7")[
      `value_or` 의 기본값은 #strong[값이 없을 때만 평가된다];(지연평가). 그러므로 기본값 자리에는
      비싼 계산이나 실패할 수 있는 계산을 적어도 된다 — 값이 있으면 그것은 돌지 아니한다.
    ]
    #note[
      `value_or (some 7) (div 1 0)` 은 #strong[7]; 이다. 값이 있으므로 기본값 쪽은 돌지 않는다.
      값이 없을 때에만 그 자리가 평가되며, 그때는 트랩이 실제로 일어난다.
      ☞ 2026-08-29 까지는 그렇지 아니하였다 — 도구가 기본값을 #strong[먼저]; 평가하여, 값이 있는
      프로그램이 없어도 될 실패로 끝났다. 정본(#cref("6.2.8") · DECISION-0003 C1)이 지연평가로
      정해 둔 자리였고, 도구를 정본에 맞추어 고쳤다. 값은 달라지지 아니하며 달라지는 것은
      #strong[어떤 트랩과 효과가 일어날 수 있는가]; 이다.
    ]
    #plain[
      실패를 확인하는 코드를 매번 손으로 쓰면 길어지고, 길어지면 빼먹는다. `try` 는 그
      되풀이를 한 낱말로 줄이되 #strong[실패가 위로 간다는 사실은 소스에 남긴다]; — 조용히
      무시되는 실패가 없다.
    ]
  ]
  #sub("6.5.9", "멈추기 — `panic`")[
    #para("1")[
      `panic` 은 프로그램을 즉시 멈춘다(#cref("5.4")). 이것은 효과이므로, 쓰는 op 은
      자기 계약에 그 효과를 적어야 한다.
    ]
    #para("2")[
      순수한 op(`fn`)은 `panic` 을 쓸 수 없다. 다만 계약이 깨졌을 때 처리기가 일으키는
      트랩은 이와 별개이며, 그것은 op 이 한 일이 아니다.
    ]
  ]
  #sub("6.5.10", "나누어 도는 되풀이 — `parallel`")[
    #para("1")[
      되풀이에 `parallel <조각> split .` 절을 붙이면, 그 되풀이를 #strong[조각을 나누어]; 여럿이
      함께 돌 수 있다고 밝히는 것이다.
    ]
    #para("2")[
      처리기는 그 말을 믿지 아니하고 #strong[확인한다.]; 확인하는 것은 되풀이의 각 걸음이 서로에게
      기대지 않는다는 것이며, 조건은 셋이다.
    ]
    #tbl("나누어 돌 수 있는 되풀이의 조건")[
      #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*조건*], [*어기면*], [*무엇이 어긋났는가*],
      [자기 몫만 읽는다], [`E-PAR-READ`], [남의 자리를 읽으면 그 값이 아직 옛것인지 새것인지가 #strong[누가 먼저 도느냐];에 달린다],
      [자기 몫만 쓴다], [`E-PAR-WRITE`], [두 걸음이 같은 자리에 쓰면 남는 값이 차례에 달린다],
      [걸음을 넘어 사는 자리에 쓰지 아니한다], [`E-PAR-CARRY`], [그런 자리는 걸음들을 #strong[묶는다];. 모아야 한다면 `reduce <쌓는 자리> <연산> .` 로 밝힌다],
      )
    ]
    #para("3")[
      `parallel` 이 조각을 이름 부르는데 그 조각의 길이로 도는 되풀이가 없으면 거부된다
      (`E-PAR-NOLOOP`) — 나눌 것이 무엇인지 알 수 없기 때문이다.
    ]
    #para("4")[
      모으는 연산은 #strong[묶음의 차례를 바꿔도 답이 같아야]; 한다. 그렇지 않은 연산은 거부된다
      (`E-PAR-ASSOC`). 부동소수의 덧셈은 그렇지 않으므로 따로 막는다(`E-PAR-FLOAT`).
    ]
    #para("4a")[
      쌓는 자리의 #strong[시작값은 그 연산의 항등원];이어야 한다. 아니면 거부된다(`E-PAR-IDENTITY`).
      나누어 돌면 조각마다 그 시작값에서 다시 시작하므로, 항등원이 아닌 값은 #strong[조각 수만큼];
      셈에 들어간다 — 곧 나눈 답이 하나씩 돈 답과 달라진다(위 (2) 의 약속이 깨진다).
      항등원은 `add`·`bit_or`·`bit_xor` 가 0, `mul` 이 1, 부호 없는 폭의 `max` 가 0 이다.
    ]
    #plain[
      나누어 도는 것을 #strong[밝히는 말];로만 두면, 그 말이 틀렸을 때 프로그램은 대개 잘 돌다가
      어느 날 다르게 돈다. 그래서 이 언어는 밝힌 말을 확인한다 — 확인할 수 없으면 나누지
      않는 것이 아니라 #strong[번역을 거절한다.]; 「빠르지만 가끔 틀림」은 이 언어가 파는 물건이
      아니다.
    ]
    #caution("")[
      나누어 돌지 #strong[않아도 답은 같다.]; 위 조건은 나눌 수 있음의 조건이지 뜻을 바꾸는 절이
      아니다. 그러므로 나누어 돈 답과 하나씩 돈 답은 언제나 같다 — `E-PAR-FLOAT` 이 있는
      까닭이 바로 그것이다.
    ]
  ]
  #sub("6.5.11", "몸이 갖추어야 하는 것")[
    #para("1")[
      값을 내놓겠다고 적은 op 은 #strong[모든 길에서]; 값을 내놓아야 한다(`E-RETURN-PARTIAL`).
      어느 한 길이 값 없이 끝에 닿으면 적합하지 아니하다.
    ]
    #para("2")[
      선언의 머리(`fn`·`proc`·`on`·`test` …)에는 `do … end` 몸이 있어야 한다
      (`E-STMT-NODO`).
    ]
    #para("3")[
      맨 바깥 자리에 오는 것은 #strong[선언의 머리];로 시작해야 한다(`E-TOPLEVEL`).
    ]
    #para("4")[
      폼 안에 올 수 없는 것이 오면 거부된다(`E-FORM-UNEXPECTED`). 머리 자리에 op 이 아닌
      이름이 오는 것도 그렇다(`E-HEAD-NOT-AN-OP`).
    ]
    #plain[
      (1) 이 없으면 「값을 내놓는다」는 선언이 #strong[어떤 길에서만]; 참인 말이 된다. 부르는 쪽은
      그 말을 믿고 값을 쓰는데, 그 길로 가면 값이 없다. 선언은 #strong[모든 길에 대한 약속];이므로
      한 길이라도 어기면 그것은 약속이 아니다.
    ]
    #rejected("어떤 길에서 값이 없다", "module ex_partial .

fn f input a u8 . output u8 .
do
  if gt a 5 . do return 1 . end
end                      rem `a` 가 5 이하인 길에는 값이 없다", "E-RETURN-PARTIAL")
  ]
  #sub("6.5.12", "오류는 적은 것만 난다")[
    #para("1")[
      op 이 내는 오류는 `errors` 절이 적은 것 안에 있어야 한다(`E-ERR-UNDECLARED`).
    ]
    #para("2")[
      `errors` 절이 그 op 의 오류 타입에 #strong[없는 이름];을 적는 것도 거부된다
      (`E-ERR-UNDEF`).
    ]
    #para("3")[
      다른 op 의 실패를 넘겨 올리면 — `try <op>` 로, 또는 `return <op> …` 로 그 `result` 를
      그대로 돌려주면 — 그 op 의 실패가 이 op 의 실패가 된다. 그래서 부른 op 이 #strong[낼 수 있는
      오류];가 이 op 의 `errors` 절(절이 없으면 이 op 의 오류 타입) 안에 있어야 한다
      (`E-ERR-UNDECLARED`).
    ]
    #para("3a")[
      부른 op 이 낼 수 있는 오류는 그 op 의 `errors` 절이 적은 것이다. 절이 없으면 그 op 의
      #strong[오류 타입 전체];다 — 절을 적지 않는 것은 «이 타입의 오류는 무엇이든 날 수 있다» 는
      뜻이고, 부르는 쪽도 그 뜻 그대로 읽는다.
    ]
    #caution("")[
      두 규칙은 같은 문장의 양쪽이다 — #strong[적은 것과 내는 것이 같아야 한다.]; 한쪽이 넘치면
      부르는 쪽이 못 본 실패가 오고, 다른 쪽이 넘치면 오지 않을 실패를 다루게 된다
      (#cref("6.4.12") (4) 와 같은 까닭이다).
    ]
    #rejected("적지 않은 오류를 낸다", "module ex_err_undeclared .

def enum e do
  bad .
end

fn f output result u8 e .
do
  return error bad .   rem `errors bad …` 를 적지 않았다
end", "E-ERR-UNDECLARED")
    #rejected("남의 실패를 `return` 으로 넘긴다", "module ex_err_handed_on .

def enum parse_error do
  bad_digit .
end

def enum load_error do
  too_long .
end

fn read_digit input c u8 . output result u8 parse_error .
errors bad_digit .
do
  guard le c 9 . else return error bad_digit .
  return ok c .
end

fn load_byte input c u8 . output result u8 load_error .
errors too_long .
do
  guard le c 200 . else return error too_long .
  return read_digit c .     rem `bad_digit` 은 `load_byte` 의 약속에 없다
end", "E-ERR-UNDECLARED")
    #rejected("절 없는 op 이 넘겨받은 실패를 다시 넘긴다", "module ex_err_clauseless .

def enum parse_error do
  bad_digit .
end

def enum load_error do
  too_long .
end

fn read_digit input c u8 . output result u8 parse_error .
errors bad_digit .
do
  guard le c 9 . else return error bad_digit .
  return ok c .
end

fn digit_or_fail input c u8 . output result u8 parse_error .
do
  return read_digit c .     rem 절이 없다 — `parse_error` 의 무엇이든 날 수 있다
end

fn load_byte input c u8 . output result u8 load_error .
errors too_long .
do
  guard le c 200 . else return error too_long .
  let v be u8 try digit_or_fail c .     rem `parse_error` 는 `load_byte` 의 약속에 없다
  return ok v .
end", "E-ERR-UNDECLARED")
  ]
]

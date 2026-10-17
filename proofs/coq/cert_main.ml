(* cert_main.ml — **추출된 판정자의 껍데기.** 판정은 전부 `Lowent_cert.check_cert` 가 한다.
 *
 * ★ 이 파일이 하는 일은 셋뿐이다: **읽고 · 넘기고 · 세는 것.**
 *   판정 로직이 여기 한 줄도 없어야 한다 — 있으면 그것은 **증명되지 않은 판정**이고,
 *   추출의 목적(판정자를 증명에서 파생시키기)이 그만큼 깎인다.
 *   그래서 규칙 이름 → 생성자 대응만 두고, 수는 그대로 넘긴다.
 *
 * 입력: `lowentc --emit-proof` 가 낸 줄들(표준 입력).
 * 출력: 판정한 줄 수 · 관할 밖 수 · 그리고 **거절된 줄이 있으면 그것을 이름과 함께.**
 *)

let nat_of_int n =
  let rec go k acc = if k <= 0 then acc else go (k - 1) (Lowent_cert.S acc) in
  go n Lowent_cert.O

let big s = Big_int_Z.big_int_of_string s

(* 규칙 이름 → (생성자, 수의 개수, bits/signed 를 어디서 읽나)
   ★ RFC-0086 의 방출 순서를 그대로 따른다. 순서가 갈리면 그것이 곧 오판이다. *)
let judge rule op nums =
  let n = Array.of_list nums in
  let arith_op () =
    if String.length op >= 3 && String.sub op 0 3 = "add" then Lowent_cert.AAdd
    else if String.length op >= 3 && String.sub op 0 3 = "sub" then Lowent_cert.ASub
    else Lowent_cert.AMul in
  let bits_signed i = (nat_of_int (int_of_string n.(i)), n.(i+1) = "1") in
  match rule with
  | "R-ARITH-RANGE" when Array.length n >= 6 ->
      let (b, s) = bits_signed 4 in
      Some (Lowent_cert.check_cert (Lowent_cert.RArith (arith_op ()))
              [big n.(0); big n.(1); big n.(2); big n.(3)] b s)
  | "R-SUB-DIFF" when Array.length n >= 6 ->
      let (b, s) = bits_signed 4 in
      Some (Lowent_cert.check_cert Lowent_cert.RSubHi
              [big n.(0); big n.(1); big n.(2); big n.(3)] b s)
  | "R-NARROW-FITS" when Array.length n >= 4 ->
      let (b, s) = bits_signed 2 in
      Some (Lowent_cert.check_cert Lowent_cert.RNarrow [big n.(0); big n.(1)] b s)
  | "R-DIV-NZ" when Array.length n >= 6 ->
      let (b, s) = bits_signed 4 in
      Some (Lowent_cert.check_cert Lowent_cert.RDivNz
              [big n.(0); big n.(1); big n.(2); big n.(3)] b s)
  | "R-IDX-LENLT" | "R-IDX-ROWMAJOR" when Array.length n >= 2 ->
      Some (Lowent_cert.check_cert Lowent_cert.RIdxNonneg
              [big n.(0); big n.(1)] (nat_of_int 64) false)
  | "R-CALL-ARGRANGE" when Array.length n >= 4 ->
      Some (Lowent_cert.check_cert Lowent_cert.RArgRange
              [big n.(0); big n.(1); big n.(2); big n.(3)] (nat_of_int 64) false)
  | _ when String.length rule > 13 && String.sub rule 0 13 = "R-ASSERT-CMP-" ->
      let c = String.sub rule 13 (String.length rule - 13) in
      let cc = match c with
        | "LT" -> Some Lowent_cert.CLt | "LE" -> Some Lowent_cert.CLe
        | "GT" -> Some Lowent_cert.CGt | "GE" -> Some Lowent_cert.CGe
        | "EQ" -> Some Lowent_cert.CEq | "NE" -> Some Lowent_cert.CNe
        | _ -> None in
      (match cc with
       | Some cc when Array.length n >= 4 ->
           Some (Lowent_cert.check_cert (Lowent_cert.RPredCmp cc)
                   [big n.(0); big n.(1); big n.(2); big n.(3)] (nat_of_int 64) false)
       | _ -> None)
  (* ★ 관할 밖 — 근거가 **경로 사실**이라 이 판정자가 다룰 것이 없다.
     `RInventory` 로 true 를 내지 **않는다**: "검사했다" 와 "관할이 아니다" 를 섞지 않는다. *)
  | "R-ASSERT-FACT" | "R-TRY-NOFAIL" -> None
  | _ -> None

let () =
  let judged = ref 0 and outside = ref 0 and bad = ref [] in
  (try
     while true do
       let line = input_line stdin in
       if String.length line > 0 && line.[0] <> '#' && line <> "lowproof 1" then begin
         match String.split_on_char ' ' line |> List.filter (fun s -> s <> "") with
         | opname :: pc :: opcode :: rule :: nums ->
             (match judge rule opcode nums with
              | None -> incr outside
              | Some true -> incr judged
              | Some false ->
                  incr judged;
                  bad := Printf.sprintf "%s@%s %s %s" opname pc opcode rule :: !bad)
         | _ -> ()
       end
     done
   with End_of_file -> ());
  Printf.printf "cert-extract: **추출된 판정자**가 %d 줄을 판정했다 (관할 밖 %d)\n" !judged !outside;
  if !bad <> [] then begin
    List.iter (fun s -> Printf.printf "  ✘ %s — 추출된 판정자가 **거절했다**\n" s) (List.rev !bad);
    print_string "cert-extract: FAIL\n"; exit 1
  end;
  print_string "cert-extract: ok — 증명에서 **추출된** 함수가 전부 통과시켰다\n"

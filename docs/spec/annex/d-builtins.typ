#import "../lib.typ": *

#clause("D", "부록 D — 내장 연산 (Annex D: Builtin ops)")[

  #para("1")[
    이 부록은 언어가 뜻을 정해 둔 연산을 모은다 — **이름과 한 줄짜리 뜻**이다.
  ]

  #note[
    이 목록은 **생성된 것**이다. 사람이 손으로 고치지 아니한다 — 내장 연산은 계속
    늘고, 손으로 적은 목록은 곧 낡는다.
  ]

  #para("2")[모두 209 개다.]

  #tbl("내장 연산과 그 뜻")[
    #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 5pt,
      [*연산*], [*무엇을 하는가*],
        [`abs`], [절댓값. 가장 작은 음수에서는 넘치므로 트랩한다],
        [`add`], [두 수를 더한다. 넘치면 트랩한다],
        [`aes_ctr`], [AES-128-CTR — 키 16 · 카운터 16 을 제자리에서 올린다. 낸 값은 흐른 바이트 수],
        [`aes_gcm`], [AES-128-GCM 한 덩이 — 흐름(aes_ctr)과 누산(ghash)을 한 바퀴에. 뜻은 그 둘을 차례로 부른 것과 같다. 낸 값은 흐른 바이트 수],
        [`aes_round`], [AES 한 라운드 — 상태 16 바이트를 제자리에서(SubBytes·ShiftRows·MixColumns·AddRoundKey). 낸 값은 16],
        [`aes_round_last`], [마지막 AES 라운드 — MixColumns 없이. 낸 값은 16],
        [`all`], [모두 참인가 — 첫 거짓에서 멈춘다],
        [`alloc_bytes`], [영역에서 바이트 자리를 얻는다],
        [`and`], [논리 곱 — 앞이 거짓이면 뒤를 **안 본다**],
        [`any`], [참이 하나라도 있나 — 첫 참에서 멈춘다],
        [`arg`], [그 자리의 명령줄 인자],
        [`atomic_add`], [원자적으로 더한다],
        [`atomic_and`], [원자적 비트 곱],
        [`atomic_cas`], [기대한 값일 때만 바꾼다(compare-and-swap)],
        [`atomic_fence`], [메모리 순서의 울타리],
        [`atomic_load`], [원자적으로 읽는다],
        [`atomic_or`], [원자적 비트 합],
        [`atomic_store`], [원자적으로 쓴다],
        [`atomic_sub`], [원자적으로 뺀다],
        [`atomic_swap`], [원자적으로 바꿔 끼우고 옛 값을 낸다],
        [`atomic_xor`], [원자적 비트 배타합],
        [`bit_and`], [비트 곱],
        [`bit_cast`], [비트를 그대로 두고 타입만 바꾼다],
        [`bit_not`], [비트 뒤집기 — **폭 안에서** 뒤집는다(C 처럼 int 로 승격하지 아니한다)],
        [`bit_or`], [비트 합],
        [`bit_xor`], [비트 배타합],
        [`bitset_complement`], [여집합],
        [`bitset_contains`], [그 원소가 들어 있나],
        [`bitset_difference`], [차집합],
        [`bitset_insert`], [집합에 그 원소를 넣는다 — **제자리에서** 바꾼다],
        [`bitset_intersect`], [교집합],
        [`bitset_is_empty`], [비었는가],
        [`bitset_is_subset`], [앞이 뒤에 다 들어 있나],
        [`bitset_new`], [빈 비트 집합],
        [`bitset_remove`], [집합에서 그 원소를 지운다 — **제자리에서** 바꾼다],
        [`bitset_union`], [합집합],
        [`borrow`], [빌린 것의 수명을 스코프로 못 박는다],
        [`byte_swap`], [바이트 순서를 뒤집는다],
        [`call_builtin`], [계산 잎과 파일·그물 잎을 부르는 머리 — 다음 원자가 잎의 이름이다(닫힌 집합). 그 이름은 전역 어휘가 아니라 이 자리에만 산다. 낸 값은 그 잎이 내는 값],
        [`capacity`], [담을 수 있는 최대],
        [`cast`], [수치 변환(폭·부호·부동소수)],
        [`ceil`], [올림],
        [`chacha20`], [ChaCha20 (RFC 8439) — 키 32 · 카운터 블록 16(앞 4 = 블록 번호 리틀엔디언 · 뒤 12 = nonce)을 제자리에서 올린다. 낸 값은 흐른 바이트 수],
        [`chacha_poly`], [ChaCha20-Poly1305 한 덩이 — 흐름과 누산을 한 바퀴에. 뜻은 chacha20 을 돌리고 그 암호문을 poly1305 에 먹인 것과 같다. 낸 값은 흐른 바이트 수],
        [`chk_add`], [더하기 — 넘쳤는지 **답과 함께** 돌려준다],
        [`chk_mul`], [곱하기 — 넘쳤는지 답과 함께 돌려준다],
        [`chk_sub`], [빼기 — 넘쳤는지 답과 함께 돌려준다],
        [`clmul_hi`], [캐리 없는 곱셈의 **윗말** — 64×64 비트 곱의 위쪽 64 비트. 덧셈이 배타합인 셈이다],
        [`clmul_lo`], [캐리 없는 곱셈의 **아랫말** — 64×64 비트 곱의 아래쪽 64 비트. 기계에 명령이 있으면 그것으로, 없으면 같은 답을 내는 셈으로],
        [`collect`], [파이프라인의 결과를 자리에 담는다],
        [`config`], [번역 시점의 설정 값],
        [`cos`], [코사인],
        [`count`], [원소의 수를 센다],
        [`count_ones`], [선 비트의 수],
        [`crc32`], [CRC-32 검사값],
        [`cstr_of`], [C 문자열로 본다],
        [`deref`], [참조가 가리키는 값],
        [`dir_close`], [디렉터리를 닫는다],
        [`dir_make`], [디렉터리를 만든다],
        [`dir_open`], [디렉터리를 연다],
        [`dir_read`], [다음 항목을 읽는다],
        [`div`], [앞을 뒤로 나눈다. 0 으로 나누면 트랩한다],
        [`div_nz`], [0 이 아님이 증명된 나눗셈 — 검사 없이 나눈다],
        [`encode`], [주어진 부호로 바이트를 만든다],
        [`enumerate`], [원소에 차례 번호를 붙인다],
        [`env_get`], [환경 변수를 읽는다],
        [`eq`], [같은가],
        [`error`], [오류를 담는다],
        [`error_value`], [담긴 오류를 꺼낸다],
        [`exp`], [e 의 거듭제곱],
        [`expect`], [값을 꺼내되 없으면 멈춘다],
        [`field`], [값의 안을 읽는다],
        [`file_close`], [파일을 닫는다],
        [`file_open`], [파일을 연다],
        [`file_read`], [파일에서 읽는다],
        [`file_seek`], [읽고 쓸 자리를 옮긴다],
        [`file_type`], [그 경로가 무엇인가(파일·디렉터리…)],
        [`file_write`], [파일에 쓴다],
        [`filter`], [술어가 참인 원소만 지나보낸다],
        [`floor`], [내림],
        [`fmod`], [부동소수 나머지],
        [`fold`], [누산기에 차례로 접는다],
        [`ge`], [앞이 크거나 같은가],
        [`ghash`], [GHASH — GF(2^128) 누산. h 16 · z 16 을 제자리에서 고친다. 낸 값은 먹인 바이트 수],
        [`gt`], [앞이 큰가],
        [`hash_bytes`], [바이트열의 해시],
        [`idx`], [그 자리의 원소],
        [`into`], [같은 뜻의 다른 타입으로 옮긴다],
        [`is_error`], [오류인가],
        [`is_none`], [값이 없나],
        [`is_ok`], [성공인가],
        [`is_some`], [값이 있나],
        [`lane_all`], [가림막의 레인이 모두 켜졌나 (레인 판정 — pipe 종결자 `all` 과 다른 낱말)],
        [`lane_any`], [가림막의 레인이 하나라도 켜졌나 (레인 판정 — pipe 종결자 `any` 와 다른 낱말)],
        [`lane_avg`], [두 수의 평균 — 중간값을 넘침 없이 낸다],
        [`lane_reverse`], [차례를 뒤집는다],
        [`lane_rotate`], [주어진 방향으로 돌린다],
        [`lane_select`], [가림막에 따라 두 값 중 하나를 레인마다 고른다],
        [`le`], [앞이 작거나 같은가],
        [`leading_zeros`], [맨 앞의 0 비트 수],
        [`len`], [길이 — **원소의 수**다(바이트 수가 아니다)],
        [`link_type`], [그 경로 자신이 무엇인가 — 심링크를 따라가지 아니한다],
        [`load`], [그 자리에서 읽는다],
        [`load_masked`], [가려진 자리만 읽는다],
        [`log`], [자연로그],
        [`lt`], [앞이 작은가],
        [`map`], [원소마다 op 을 적용한다],
        [`max`], [둘 중 큰 것],
        [`min`], [둘 중 작은 것],
        [`mod`], [나눈 나머지],
        [`mul`], [두 수를 곱한다. 넘치면 트랩한다],
        [`mut_ref`], [쓰기 참조를 만든다],
        [`narrow`], [더 좁은 폭으로. 안 들어가면 트랩한다],
        [`narrow_sat`], [좁히되 안 들어가면 끝값에서 멈춘다],
        [`narrow_try`], [좁히기 — 안 들어가면 `none` 을 낸다],
        [`narrow_wrap`], [좁히되 안 들어가면 감는다],
        [`native_lanes`], [이 기계가 한 번에 다루는 레인 수],
        [`ne`], [다른가],
        [`neg`], [부호를 뒤집는다],
        [`net_accept`], [들어온 연결을 받는다],
        [`net_close`], [닫는다],
        [`net_connect`], [주소와 포트로 연결을 건다],
        [`net_listen`], [듣는 소켓을 연다],
        [`net_pair`], [맞물린 소켓 한 쌍],
        [`net_port`], [그 듣는 소켓의 포트],
        [`net_recv`], [받는다],
        [`net_resolve`], [이름을 IPv4 주소로 바꾼다 — DNS. 첫 A 레코드 하나],
        [`net_send`], [보낸다],
        [`nonzero_of`], [0 이 아님을 증명해 담는다],
        [`not`], [논리 부정],
        [`ok`], [성공한 답을 담는다],
        [`ok_value`], [담긴 성공값을 꺼낸다],
        [`or`], [논리 합 — 앞이 참이면 뒤를 안 본다],
        [`panic`], [계약이 깨졌음을 알리고 멈춘다],
        [`path_remove`], [지운다],
        [`path_rename`], [이름을 바꾼다],
        [`pipe`], [한 줄기로 흘린다 — **한 번의 훑기**다],
        [`poly1305`], [Poly1305 (RFC 8439) — 상태(h 다섯 · r 다섯)를 제자리에서 누산한다. 열여섯의 배수가 아니면 꼬리를 0 으로 채운다. 낸 값은 먹인 바이트 수],
        [`pop`], [뒤에서 하나 뺀다],
        [`pow`], [거듭제곱],
        [`prefetch`], [곧 쓸 자리를 미리 끌어 온다],
        [`proc_kill`], [띄운 프로그램을 강제로 끝낸다 — POSIX SIGKILL · Windows TerminateProcess. 손자는 끄지 않는다(RFC-0136)],
        [`proc_poll`], [안 막고 확인한다 — 끝났으면 종료 상태(낮은 32 비트 = 코드 · 비트 32 = 신호), 아직이면 UINT64_MAX],
        [`proc_read`], [띄운 프로그램의 출력(표준 출력 + 오류)을 안 막고 읽는다 — n 바이트 · 0 = 지금은 없다 · none = 끝],
        [`proc_spawn`], [NUL 로 이은 인자 목록으로 프로그램을 띄운다 — 표준 입력은 비고 출력은 파이프로 모인다(RFC-0136)],
        [`proc_wait`], [띄운 프로그램이 끝날 때까지 기다리고 핸들을 닫는다 — 종료 상태],
        [`push`], [뒤에 붙인다],
        [`r_read`], [reactor 로 읽는다],
        [`r_write`], [reactor 로 쓴다],
        [`range`], [값의 범위를 좁힌 매개변수 표시],
        [`reactor_new`], [reactor 를 만든다],
        [`read_in`], [표준 입력에서 읽는다],
        [`read_volatile`], [장치 레지스터를 읽는다 — 합치거나 재배치하지 아니한다],
        [`reduce_add`], [레인을 모두 더한다],
        [`reduce_max`], [레인 중 가장 큰 것],
        [`reduce_min`], [레인 중 가장 작은 것],
        [`reduce_mul`], [레인을 모두 곱한다],
        [`ref`], [읽기 참조를 만든다],
        [`region`], [영역을 연다 — 수명이 곧 스코프다],
        [`ret`], [오류를 위로 넘긴다],
        [`rng_next`], [난수를 낸다],
        [`rotl`], [왼쪽으로 돌린다],
        [`rotr`], [오른쪽으로 돌린다],
        [`round`], [반올림 — 0 에서 먼 쪽으로],
        [`same_slice`], [두 슬라이스가 같은 바이트인가(시작과 길이)],
        [`sat_add`], [더하되 넘치면 **끝값에서 멈춘다**(포화)],
        [`sat_mul`], [곱하되 넘치면 끝값에서 멈춘다],
        [`sat_sub`], [빼되 넘치면 끝값에서 멈춘다],
        [`scan`], [접으며 중간값을 낸다],
        [`seg`], [조각 사슬의 한 조각],
        [`segs`], [조각 사슬의 조각들],
        [`send`], [액터에 메시지를 보낸다],
        [`sha256`], [SHA-256],
        [`sha384`], [SHA-384 — SHA-512 의 다른 시작값이고 앞 48 바이트다],
        [`sha512`], [SHA-512],
        [`shl`], [왼쪽으로 민다. 시프트 양이 폭 이상이면 트랩한다],
        [`shr`], [오른쪽으로 민다. 부호 있는 값은 산술 이동이다],
        [`sin`], [사인],
        [`size_of`], [그 타입이 차지하는 바이트 수],
        [`skip`], [앞의 n 개를 건너뛴다],
        [`some_value`], [담긴 값을 꺼낸다],
        [`spawn`], [새 실행 흐름을 만들고 핸들을 낸다],
        [`splat`], [한 값을 모든 레인에 채운다],
        [`sqrt`], [제곱근],
        [`stack_new`], [스택 자료를 만든다],
        [`store`], [그 자리에 쓴다],
        [`store_masked`], [가려진 자리만 쓴다],
        [`str_from_cstr`], [C 문자열에서 읽는다],
        [`sub`], [앞에서 뒤를 뺀다. 넘치면 트랩한다],
        [`subslice`], [부분 슬라이스 — 복사하지 아니한다],
        [`sum_neumaier`], [부동 조각을 **보정하며** 더한다 — 오차가 항의 개수에 매이지 아니한다],
        [`sum_seq`], [부동 조각을 앞에서 뒤로 한 번 더한다 — 빠르고, 오차가 항의 개수에 비례한다],
        [`swap`], [두 자리를 맞바꾼다],
        [`take`], [앞에서 n 개],
        [`trailing_zeros`], [맨 뒤의 0 비트 수],
        [`try_view`], [`view` 와 같되 계약이 깨지면 `none` 을 낸다],
        [`value_or`], [값이 있으면 그것, 없으면 기본값 — 기본값은 **성공 시 평가되지 아니한다**],
        [`view`], [바이트를 구조체로 본다 — 복사 없음. 정렬·길이 계약을 확인한다],
        [`view_array`], [바이트를 그 타입의 배열로 본다 — 복사 없음],
        [`view_segments`], [조각 사슬을 하나의 배열처럼 본다],
        [`widen`], [더 넓은 폭으로 — 값이 보존된다],
        [`wrap_add`], [더하되 넘치면 **감는다**(모듈러)],
        [`wrap_mul`], [곱하되 넘치면 감는다],
        [`wrap_shl`], [왼쪽으로 밀되 시프트 양을 폭으로 감는다],
        [`wrap_shr`], [오른쪽으로 밀되 시프트 양을 폭으로 감는다],
        [`wrap_sub`], [빼되 넘치면 감는다],
        [`write_out`], [표준 출력에 쓴다],
        [`write_volatile`], [장치 레지스터에 쓴다 — 합치거나 재배치하지 아니한다],
        [`zip`], [두 줄기를 짝지어 흘린다],
    )
  ]

  #para("3")[
    이 이름들이 모두 같은 규칙을 갖지는 아니한다. 대부분은 지역 이름으로 쓸 수 없으나
    (#cref("6.1.3")), 일부는 **문맥 안에서만** 연산이므로 그 밖에서는 저자의 이름이 될 수
    있다. 어느 쪽인지는 처리기가 정하며, 막히는 것은 진단으로 말한다.
  ]

  #note[
    실측(2026-08-25): 209 개 가운데 지역 이름 선언이 막히는 것은 **191 개**,
    쓸 수 있는 것은 **18 개**다 — `all` · `any` · `borrow` · `call_builtin` · `capacity` · `collect` · `enumerate` · `into` · `is_none` · `pipe` · `pop` · `range` · `region` · `ret` · `scan` · `skip` · `take` · `zip`.
    ★ 이 수를 여기 적는 까닭은, 하나로 뭉뚱그리면 **이름 충돌 규칙을 틀리게 말하기**
    때문이다(RFC-0101 F-19). 뭉뚱그린 목록은 수가 맞아도 규칙이 틀린다.
  ]

  #shape("내장 연산 이름", "abs                   add                   aes_ctr               aes_gcm
aes_round             aes_round_last        all                   alloc_bytes
and                   any                   arg                   atomic_add
atomic_and            atomic_cas            atomic_fence          atomic_load
atomic_or             atomic_store          atomic_sub            atomic_swap
atomic_xor            bit_and               bit_cast              bit_not
bit_or                bit_xor               bitset_complement     bitset_contains
bitset_difference     bitset_insert         bitset_intersect      bitset_is_empty
bitset_is_subset      bitset_new            bitset_remove         bitset_union
borrow                byte_swap             call_builtin          capacity
cast                  ceil                  chacha20              chacha_poly
chk_add               chk_mul               chk_sub               clmul_hi
clmul_lo              collect               config                cos
count                 count_ones            crc32                 cstr_of
deref                 dir_close             dir_make              dir_open
dir_read              div                   div_nz                encode
enumerate             env_get               eq                    error
error_value           exp                   expect                field
file_close            file_open             file_read             file_seek
file_type             file_write            filter                floor
fmod                  fold                  ge                    ghash
gt                    hash_bytes            idx                   into
is_error              is_none               is_ok                 is_some
lane_all              lane_any              lane_avg              lane_reverse
lane_rotate           lane_select           le                    leading_zeros
len                   link_type             load                  load_masked
log                   lt                    map                   max
min                   mod                   mul                   mut_ref
narrow                narrow_sat            narrow_try            narrow_wrap
native_lanes          ne                    neg                   net_accept
net_close             net_connect           net_listen            net_pair
net_port              net_recv              net_resolve           net_send
nonzero_of            not                   ok                    ok_value
or                    panic                 path_remove           path_rename
pipe                  poly1305              pop                   pow
prefetch              proc_kill             proc_poll             proc_read
proc_spawn            proc_wait             push                  r_read
r_write               range                 reactor_new           read_in
read_volatile         reduce_add            reduce_max            reduce_min
reduce_mul            ref                   region                ret
rng_next              rotl                  rotr                  round
same_slice            sat_add               sat_mul               sat_sub
scan                  seg                   segs                  send
sha256                sha384                sha512                shl
shr                   sin                   size_of               skip
some_value            spawn                 splat                 sqrt
stack_new             store                 store_masked          str_from_cstr
sub                   subslice              sum_neumaier          sum_seq
swap                  take                  trailing_zeros        try_view
value_or              view                  view_array            view_segments
widen                 wrap_add              wrap_mul              wrap_shl
wrap_shr              wrap_sub              write_out             write_volatile
zip")
]

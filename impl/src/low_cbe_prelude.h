#ifndef LOW_CBE_PRELUDE_H
#define LOW_CBE_PRELUDE_H
/* low_cbe_prelude.h — 방출 런타임 문자열의 이름표 (WO-0166).
 * ★ 여기 있는 것은 전부 **데이터**다. 함수는 하나도 없다 — 그것이 이 파일이 갈라진 이유다. */
extern const char LW_HDR_HOSTED[];
extern const char LW_HDR_FREE[];
extern const char LW_HDR_WIN[];
extern const char LW_PRELUDE[];
extern const char LW_CONC[];
/* ★ 프리스탠딩(no_heap) 타깃의 동시성 — 힙·스레드·ucontext 없이 도는 협력 floor (RFC-0039 T0) */
extern const char LW_CONC_T0[];
extern const char LW_PRELUDE2[];
extern const char LW_RENDER[];
extern const char LW_DISPATCH[];

#endif

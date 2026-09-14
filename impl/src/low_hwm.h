// low_hwm.h — **고정 표의 최고수위(high-water mark)를 잰다.**
//
// ★★★ 왜 있나. 고정 표에 대한 물음은 늘 셋이다: *"넘치면 무슨 일이 나나 · 넘칠 수는
//   있나 · 얼마나 여유가 있나"*. 앞의 둘은 코드를 읽어 답하지만 **셋째는 재야 한다**.
//   여태 그 수를 아무도 안 쟀고, 그래서 *"넉넉하다"* 는 말은 **믿음**이었다.
//   ⇒ 도구가 재게 한다. `LOW_HWM=1` 이면 종료할 때 표별 최고수위를 stderr 로 찍는다.
//
// ★ 값이 0 일 때는 **꺼진 것과 구별되게** 찍는다 — *"안 쟀다"* 와 *"0 이었다"* 는 다르다.
// ★★ 켜지 않으면 비용은 **전역 하나 읽기**다(분기 예측이 먹는다). 그래서 상시 둔다 —
//    필요할 때만 켜지는 계측은 필요할 때 없다.
#ifndef LOW_HWM_H
#define LOW_HWM_H
#include <stddef.h>

void low_hwm_note(const char *name, size_t v, size_t cap);
void low_hwm_dump(void);      // LOW_HWM 이 켜져 있을 때만 찍는다
int  low_hwm_on(void);

#define LOW_HWM(name, v, cap) do { if (low_hwm_on()) low_hwm_note((name), (size_t)(v), (size_t)(cap)); } while (0)

#endif

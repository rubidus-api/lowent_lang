// host-for: exported.low
#include <stdio.h>
#include "lowent.h"

int main(void) {
    const unsigned char bytes[] = {1, 2, 3, 4};
    printf("clamp_add(100, 50) = %lld\n", lw_8exported_9clamp_add(100, 50));
    printf("sum_bytes = %lld\n", lw_8exported_9sum_bytes(bytes, 4));
    fflush(stdout);
    printf("clamp_add(5000, 1) = %lld\n", lw_8exported_9clamp_add(5000, 1));
    return 0;
}

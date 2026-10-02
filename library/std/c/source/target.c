#include "r_std_c.h"

#include <stdint.h>

RStdCTargetInfo r_std_c_target(void) {
    _Static_assert(sizeof(void *) == 8U, "arm64-apple-darwin requires 64-bit pointers");
    return (RStdCTargetInfo){UINT32_C(64), 1, 1, 1};
}

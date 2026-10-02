#include "r_std_rc.h"

_Bool r_std_rc_ptr_eq(const RRuntimeRc *left, const RRuntimeRc *right) {
    return r_runtime_rc_ptr_eq(left, right);
}

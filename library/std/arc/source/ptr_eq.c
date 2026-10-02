#include "r_std_arc.h"

_Bool r_std_arc_ptr_eq(const RRuntimeArc *left, const RRuntimeArc *right) {
    return r_runtime_arc_ptr_eq(left, right);
}

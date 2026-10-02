#include "r_std_arc.h"

void *r_std_arc_get_mut(RRuntimeArc *owner) {
    return r_runtime_arc_get_mut(owner);
}

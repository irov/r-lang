#include "r_std_rc.h"

void *r_std_rc_get_mut(RRuntimeRc *owner) {
    return r_runtime_rc_get_mut(owner);
}

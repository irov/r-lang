#include "r_std_rc.h"

size_t r_std_rc_weak_count(const RRuntimeRc *source) {
    return r_runtime_rc_weak_count(source);
}

#include "r_std_rc.h"

size_t r_std_rc_strong_count(const RRuntimeRc *source) {
    return r_runtime_rc_strong_count(source);
}

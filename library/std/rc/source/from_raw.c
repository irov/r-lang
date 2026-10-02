#include "r_std_rc.h"

#include "r_library_rc_internal.h"

RStdRcCallStatus r_std_rc_from_raw(const void *pointer, RRuntimeRc *result) {
    return r_library_internal_rc_status(r_runtime_rc_from_raw(pointer, result));
}

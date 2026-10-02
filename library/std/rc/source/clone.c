#include "r_std_rc.h"

#include "r_library_rc_internal.h"

RStdRcCallStatus r_std_rc_clone(const RRuntimeRc *source, RRuntimeRc *result) {
    return r_library_internal_rc_status(r_runtime_rc_clone(source, result));
}

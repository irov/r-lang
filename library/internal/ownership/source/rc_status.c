#include "r_library_rc_internal.h"

RStdRcCallStatus r_library_internal_rc_status(RRuntimeRcStatus status) {
    return status == R_RUNTIME_RC_COUNT_OVERFLOW ? R_STD_RC_CALL_COUNT_OVERFLOW
                                                 : R_STD_RC_CALL_SUCCESS;
}

#include "r_library_arc_internal.h"

RStdArcCallStatus r_library_internal_arc_status(RRuntimeArcStatus status) {
    return status == R_RUNTIME_ARC_COUNT_OVERFLOW ? R_STD_ARC_CALL_COUNT_OVERFLOW
                                                  : R_STD_ARC_CALL_SUCCESS;
}

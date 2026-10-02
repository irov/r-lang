#include "r_std_arc.h"

RStdArcTryUnwrapResult r_std_arc_try_unwrap(RRuntimeArc *owner, void *value_storage) {
    RStdArcTryUnwrapResult result = {0};
    RRuntimeArcStatus status = r_runtime_arc_try_unwrap(owner, value_storage);

    if (status == R_RUNTIME_ARC_NOT_UNIQUE) {
        result.status = R_STD_ARC_CALL_SUCCESS;
        result.kind = R_STD_ARC_TRY_UNWRAP_OWNER;
        result.owner = *owner;
        owner->control = NULL;
        return result;
    }
    result.status = R_STD_ARC_CALL_SUCCESS;
    result.kind = R_STD_ARC_TRY_UNWRAP_VALUE;
    return result;
}

#include "r_std_rc.h"

RStdRcTryUnwrapResult r_std_rc_try_unwrap(RRuntimeRc *owner, void *value_storage) {
    RStdRcTryUnwrapResult result = {0};
    RRuntimeRcStatus status = r_runtime_rc_try_unwrap(owner, value_storage);

    if (status == R_RUNTIME_RC_NOT_UNIQUE) {
        result.status = R_STD_RC_CALL_SUCCESS;
        result.kind = R_STD_RC_TRY_UNWRAP_OWNER;
        result.owner = *owner;
        owner->control = NULL;
        return result;
    }
    result.status = R_STD_RC_CALL_SUCCESS;
    result.kind = R_STD_RC_TRY_UNWRAP_VALUE;
    return result;
}

#include "r_std_rc.h"

#include "r_library_rc_internal.h"

RStdRcUpgradeResult r_std_rc_upgrade(const RRuntimeWeakRc *source) {
    RStdRcUpgradeResult result = {0};
    RRuntimeRcStatus status = r_runtime_weak_rc_upgrade(source, &result.value);

    if (status == R_RUNTIME_RC_EXPIRED) {
        result.status = R_STD_RC_CALL_SUCCESS;
        result.has_value = 0;
        return result;
    }
    result.status = r_library_internal_rc_status(status);
    result.has_value = result.status == R_STD_RC_CALL_SUCCESS;
    return result;
}

#include "r_std_arc.h"

#include "r_library_arc_internal.h"

RStdArcUpgradeResult r_std_arc_upgrade(const RRuntimeWeakArc *source) {
    RStdArcUpgradeResult result = {0};
    RRuntimeArcStatus status = r_runtime_weak_arc_upgrade(source, &result.value);

    if (status == R_RUNTIME_ARC_EXPIRED) {
        result.status = R_STD_ARC_CALL_SUCCESS;
        result.has_value = 0;
        return result;
    }
    result.status = r_library_internal_arc_status(status);
    result.has_value = result.status == R_STD_ARC_CALL_SUCCESS;
    return result;
}

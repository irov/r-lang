#include "r_std_alloc.h"

RStdAllocCallStatus r_std_alloc_into_value(RRuntimeOwn *object, void *result_storage) {
    RRuntimeOwnStatus status = r_runtime_own_into_value(object, result_storage);

    (void)status;
    return R_STD_ALLOC_CALL_SUCCESS;
}

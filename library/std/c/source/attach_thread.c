#include "r_std_c.h"

RStdCAttachThreadResult r_std_c_attach_thread(void) {
    RRuntimeThreadAttachResult attached = r_runtime_thread_attach();
    RStdCAttachThreadResult result = {0};

    switch (attached.status) {
    case R_RUNTIME_THREAD_ATTACH_OK:
        result.status = R_STD_C_CALL_SUCCESS;
        result.value = attached.attachment;
        break;
    case R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING:
        result.status = R_STD_C_CALL_ERROR;
        result.error = R_STD_C_RUNTIME_ERROR_RUNTIME_STOPPING;
        break;
    case R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED:
        result.status = R_STD_C_CALL_ERROR;
        result.error = R_STD_C_RUNTIME_ERROR_RESOURCE_EXHAUSTED;
        break;
    case R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE:
        result.status = R_STD_C_CALL_ERROR;
        result.error = R_STD_C_RUNTIME_ERROR_FLOATING_ENVIRONMENT_UNAVAILABLE;
        break;
    }
    return result;
}

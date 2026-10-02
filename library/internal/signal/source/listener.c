#include "r_library_signal_internal.h"

#include "r_library_process_internal.h"
#include "r_runtime_0_1.h"

#include <errno.h>
#include <stdint.h>

_Noreturn static void listener_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RRuntimeDarwinSignalKind listener_native_kind(RStdSignalKind kind) {
    switch (kind) {
    case R_STD_SIGNAL_KIND_TERMINATE:
        return R_RUNTIME_DARWIN_SIGNAL_TERMINATE;
    case R_STD_SIGNAL_KIND_INTERRUPT:
        return R_RUNTIME_DARWIN_SIGNAL_INTERRUPT;
    case R_STD_SIGNAL_KIND_HANGUP:
        return R_RUNTIME_DARWIN_SIGNAL_HANGUP;
    case R_STD_SIGNAL_KIND_USER1:
        return R_RUNTIME_DARWIN_SIGNAL_USER1;
    case R_STD_SIGNAL_KIND_USER2:
        return R_RUNTIME_DARWIN_SIGNAL_USER2;
    }
    listener_panic();
}

/* R-SLIB-SIGNAL-0001: exhausted resources, among them allocation, are resource_exhausted; every
   other native failure is other. */
static RStdProcessError listener_native_error(int native_error) {
    if (native_error == ENOMEM || native_error == EAGAIN || native_error == ENOBUFS) {
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED,
                                                (int64_t)native_error);
    }
    return r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, (int64_t)native_error);
}

RStdSignalListenerResult r_library_internal_signal_listen(RRuntimeAllocator *allocator,
                                                          RStdSignalKind kind) {
    RStdSignalListenerResult result = {0};
    RRuntimeDarwinSignalListenResult native;

    if (allocator == NULL) {
        listener_panic();
    }
    native = r_runtime_darwin_signal_listen(allocator, listener_native_kind(kind));
    switch (native.status) {
    case R_RUNTIME_DARWIN_SIGNAL_OK:
        result.status = R_STD_PROCESS_CALL_SUCCESS;
        result.value.storage = native.listener;
        return result;
    case R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED:
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error = r_library_internal_process_allocation_error();
        return result;
    case R_RUNTIME_DARWIN_SIGNAL_NATIVE_FAILED:
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error = listener_native_error(native.native_error);
        return result;
    case R_RUNTIME_DARWIN_SIGNAL_INVALID:
        listener_panic();
    }
    listener_panic();
}

RStdProcessVoidResult r_library_internal_signal_raise(RStdSignalKind kind) {
    RStdProcessVoidResult result = {0};
    const int native_error = r_runtime_darwin_signal_raise(listener_native_kind(kind));

    if (native_error == 0) {
        result.status = R_STD_PROCESS_CALL_SUCCESS;
        return result;
    }
    result.status = R_STD_PROCESS_CALL_ERROR;
    result.error = listener_native_error(native_error);
    return result;
}

void r_library_internal_signal_listener_move(RStdSignalListener *destination,
                                             RStdSignalListener *source) {
    if (destination == NULL || source == NULL) {
        listener_panic();
    }
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_signal_listener_destroy(RStdSignalListener *listener) {
    if (listener == NULL) {
        listener_panic();
    }
    if (listener->storage != NULL) {
        r_runtime_darwin_signal_listener_release(listener->storage);
        listener->storage = NULL;
    }
}

#include "r_runtime_freestanding.h"

#include <stddef.h>

static RRuntimeThreadLocalCleanupFn r_runtime_thread_local_cleanup;

void r_runtime_thread_local_cleanup_install(RRuntimeThreadLocalCleanupFn cleanup) {
    r_runtime_thread_local_cleanup = cleanup;
}

void r_runtime_freestanding_thread_exit(void) {
    if (r_runtime_thread_local_cleanup != NULL) {
        r_runtime_thread_local_cleanup();
    }
}

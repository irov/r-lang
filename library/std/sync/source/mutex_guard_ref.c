#include "r_std_sync.h"

#include "r_library_sync_internal.h"

const void *r_std_sync_mutex_guard_ref(const RStdSyncMutexGuard *guard) {
    return r_library_internal_sync_mutex_guard_ref(guard);
}

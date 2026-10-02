#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void *r_std_sync_mutex_guard_mut(RStdSyncMutexGuard *guard) {
    return r_library_internal_sync_mutex_guard_mut(guard);
}

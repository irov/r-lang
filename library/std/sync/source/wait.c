#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncLockResult r_std_sync_wait(const RStdSyncCondvar *condition, RStdSyncMutexGuard *guard) {
    const RLibrarySyncAcquireResult acquired =
        r_library_internal_sync_condvar_wait(condition, guard);
    RStdSyncLockResult result = {0};

    result.guard = acquired.guard;
    result.kind = acquired.kind == R_LIBRARY_SYNC_ACQUIRE_POISONED ? R_STD_SYNC_LOCK_RESULT_POISONED
                                                                   : R_STD_SYNC_LOCK_RESULT_LOCKED;
    return result;
}

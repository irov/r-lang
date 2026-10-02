#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncLockResult r_std_sync_lock(const RStdSyncMutex *mutex) {
    const RLibrarySyncAcquireResult acquired = r_library_internal_sync_mutex_acquire(mutex, 1);
    RStdSyncLockResult result = {0};

    result.guard = acquired.guard;
    switch (acquired.kind) {
    case R_LIBRARY_SYNC_ACQUIRE_LOCKED:
        result.kind = R_STD_SYNC_LOCK_RESULT_LOCKED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_POISONED:
        result.kind = R_STD_SYNC_LOCK_RESULT_POISONED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK:
        result.kind = R_STD_SYNC_LOCK_RESULT_WOULD_DEADLOCK;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK:
        break;
    }
    return result;
}

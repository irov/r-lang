#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_read(RStdSyncReadLockResult *result, const RStdSyncRwLock *lock) {
    RLibrarySyncAcquireKind acquired;

    acquired = r_library_internal_sync_rw_read_acquire(lock, 1, &result->guard);
    switch (acquired) {
    case R_LIBRARY_SYNC_ACQUIRE_LOCKED:
        result->kind = R_STD_SYNC_READ_LOCK_RESULT_LOCKED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_POISONED:
        result->kind = R_STD_SYNC_READ_LOCK_RESULT_POISONED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK:
        result->kind = R_STD_SYNC_READ_LOCK_RESULT_WOULD_DEADLOCK;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK:
        break;
    }
}

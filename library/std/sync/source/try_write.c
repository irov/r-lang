#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_try_write(RStdSyncTryWriteLockResult *result, const RStdSyncRwLock *lock) {
    RLibrarySyncAcquireKind acquired;

    acquired = r_library_internal_sync_rw_write_acquire(lock, 0, &result->guard);
    switch (acquired) {
    case R_LIBRARY_SYNC_ACQUIRE_LOCKED:
        result->kind = R_STD_SYNC_TRY_WRITE_LOCK_RESULT_LOCKED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_POISONED:
        result->kind = R_STD_SYNC_TRY_WRITE_LOCK_RESULT_POISONED;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK:
        result->kind = R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_DEADLOCK;
        break;
    case R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK:
        result->kind = R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_BLOCK;
        break;
    }
}

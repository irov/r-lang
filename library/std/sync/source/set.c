#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSetResult r_std_sync_set(const RStdSyncOnceLock *lock, void *staged_value) {
    return r_library_internal_sync_once_lock_set(lock, staged_value);
}

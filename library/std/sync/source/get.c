#include "r_std_sync.h"

#include "r_library_sync_internal.h"

const void *r_std_sync_get(const RStdSyncOnceLock *lock) {
    return r_library_internal_sync_once_lock_get(lock);
}

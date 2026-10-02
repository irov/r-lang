#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_rwlock_new(RStdSyncRwLock *result,
                           void *value_storage,
                           RRuntimeTypeInfo value_type,
                           void *staged_value) {
    r_library_internal_sync_rw_lock_initialize(result, value_storage, value_type, staged_value);
}

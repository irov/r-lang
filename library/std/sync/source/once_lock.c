#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_once_lock(RStdSyncOnceLock *result,
                          void *value_storage,
                          RRuntimeTypeInfo value_type) {
    r_library_internal_sync_once_lock_initialize(result, value_storage, value_type);
}

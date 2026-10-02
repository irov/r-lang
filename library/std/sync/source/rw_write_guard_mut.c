#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void *r_std_sync_rw_write_guard_mut(RStdSyncRwWriteGuard *guard) {
    return r_library_internal_sync_rw_write_guard_mut(guard);
}

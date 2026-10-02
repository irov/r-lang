#include "r_std_sync.h"

#include "r_library_sync_internal.h"

const void *r_std_sync_rw_write_guard_ref(const RStdSyncRwWriteGuard *guard) {
    return r_library_internal_sync_rw_write_guard_ref(guard);
}

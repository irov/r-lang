#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_unlock(void *guard_value) {
    RStdSyncGuardHeader *guard = guard_value;

    switch (guard->kind) {
    case R_STD_SYNC_GUARD_MUTEX:
        r_library_internal_sync_mutex_unlock(guard_value, 0);
        break;
    case R_STD_SYNC_GUARD_RW_READ:
        r_library_internal_sync_rw_read_unlock(guard_value);
        break;
    case R_STD_SYNC_GUARD_RW_WRITE:
        r_library_internal_sync_rw_write_unlock(guard_value, 0);
        break;
    case R_STD_SYNC_GUARD_NONE:
        break;
    }
}

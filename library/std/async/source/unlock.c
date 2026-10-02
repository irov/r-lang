#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

void r_std_async_unlock(void *guard, RStdAsyncGuardKind kind) {
    switch (kind) {
    case R_STD_ASYNC_GUARD_MUTEX:
        r_std_async_mutex_guard_destroy(guard);
        return;
    case R_STD_ASYNC_GUARD_RW_READ:
        r_std_async_rw_read_guard_destroy(guard);
        return;
    case R_STD_ASYNC_GUARD_RW_WRITE:
        r_std_async_rw_write_guard_destroy(guard);
        return;
    }
    r_library_internal_async_contract_violation();
}

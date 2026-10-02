#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

const void *r_std_async_rw_read_guard_ref(const RStdAsyncRwReadGuard *guard) {
    if (guard == NULL) {
        r_library_internal_async_contract_violation();
    }
    return r_library_internal_async_rwlock_value(guard->state);
}

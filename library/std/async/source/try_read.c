#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

_Bool r_std_async_try_read(const RStdAsyncRwLock *lock, RStdAsyncRwReadGuard *guard) {
    if (guard == NULL) {
        r_library_internal_async_contract_violation();
    }
    if (!r_library_internal_async_rwlock_try(lock, 0)) {
        return 0;
    }
    guard->state = lock->state;
    return 1;
}

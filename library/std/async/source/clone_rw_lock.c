#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncRwLock r_std_async_clone_rw_lock(const RStdAsyncRwLock *lock) {
    return r_library_internal_async_rwlock_clone(lock);
}

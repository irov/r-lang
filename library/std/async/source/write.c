#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncStartResult r_std_async_write(const RStdAsyncRwLock *lock, RRuntimeTypeInfo result) {
    return r_library_internal_async_rwlock_start(lock, 1, result);
}

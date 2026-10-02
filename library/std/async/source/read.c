#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncStartResult r_std_async_read(const RStdAsyncRwLock *lock, RRuntimeTypeInfo result) {
    return r_library_internal_async_rwlock_start(lock, 0, result);
}

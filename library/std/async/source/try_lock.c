#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

_Bool r_std_async_try_lock(const RStdAsyncMutex *mutex, RStdAsyncMutexGuard *guard) {
    return r_library_internal_async_mutex_try_lock(mutex, guard);
}

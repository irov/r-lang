#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncStartResult r_std_async_lock(const RStdAsyncMutex *mutex, RRuntimeTypeInfo result) {
    return r_library_internal_async_mutex_lock(mutex, result);
}

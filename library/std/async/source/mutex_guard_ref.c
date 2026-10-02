#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

const void *r_std_async_mutex_guard_ref(const RStdAsyncMutexGuard *guard) {
    return r_library_internal_async_mutex_value(guard);
}

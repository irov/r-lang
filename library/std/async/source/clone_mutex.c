#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncMutex r_std_async_clone_mutex(const RStdAsyncMutex *mutex) {
    return r_library_internal_async_mutex_clone(mutex);
}

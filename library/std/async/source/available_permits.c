#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

size_t r_std_async_available_permits(const RStdAsyncSemaphore *semaphore) {
    return r_library_internal_async_semaphore_available(semaphore);
}

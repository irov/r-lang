#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

void r_std_async_add_permits(const RStdAsyncSemaphore *semaphore, size_t count) {
    r_library_internal_async_semaphore_add(semaphore, count);
}

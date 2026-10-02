#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

_Bool r_std_async_try_acquire(const RStdAsyncSemaphore *semaphore,
                              RStdAsyncSemaphorePermit *permit) {
    return r_library_internal_async_semaphore_try_acquire(semaphore, permit);
}

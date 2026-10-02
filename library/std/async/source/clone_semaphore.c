#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncSemaphore r_std_async_clone_semaphore(const RStdAsyncSemaphore *semaphore) {
    return r_library_internal_async_semaphore_clone(semaphore);
}

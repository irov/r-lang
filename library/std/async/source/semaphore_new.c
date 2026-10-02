#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncSemaphoreNewResult r_std_async_semaphore_new(RRuntimeAllocator *allocator,
                                                      size_t permits) {
    return r_library_internal_async_semaphore_new(allocator, permits);
}

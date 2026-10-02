#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncNotifyNewResult r_std_async_notify_new(RRuntimeAllocator *allocator) {
    return r_library_internal_async_notify_new(allocator);
}

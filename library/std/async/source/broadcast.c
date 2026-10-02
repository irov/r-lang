#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncBroadcastNewResult r_std_async_broadcast(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo element,
                                                  RStdAsyncCloneFn clone,
                                                  size_t capacity) {
    return r_library_internal_async_broadcast_new(allocator, element, clone, capacity);
}

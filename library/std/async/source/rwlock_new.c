#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncRwLockNewResult r_std_async_rwlock_new(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo value_type,
                                                void *staged_value) {
    return r_library_internal_async_rwlock_new(allocator, value_type, staged_value);
}

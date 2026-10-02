#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSyncChannelCreateResult
r_std_sync_sync_channel(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity) {
    return r_library_internal_sync_sync_channel_create(allocator, element, capacity);
}

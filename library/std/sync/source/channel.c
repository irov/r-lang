#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncChannelCreateResult r_std_sync_channel(RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo element) {
    return r_library_internal_sync_channel_create(allocator, element);
}

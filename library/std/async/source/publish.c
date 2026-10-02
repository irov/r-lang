#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncPublishResult r_std_async_publish(const RStdAsyncBroadcast *sender, void *staged_value) {
    return r_library_internal_async_broadcast_publish(sender, staged_value);
}

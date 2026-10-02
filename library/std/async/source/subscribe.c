#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncSubscribeResult r_std_async_subscribe(const RStdAsyncBroadcast *sender) {
    return r_library_internal_async_broadcast_subscribe(sender);
}

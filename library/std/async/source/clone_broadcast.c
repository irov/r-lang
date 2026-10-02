#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncBroadcast r_std_async_clone_broadcast(const RStdAsyncBroadcast *sender) {
    return r_library_internal_async_broadcast_clone(sender);
}

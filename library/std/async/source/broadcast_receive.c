#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncStartResult r_std_async_broadcast_receive(const RStdAsyncBroadcastReceiver *receiver,
                                                   RStdAsyncBroadcastReceiveLayout layout) {
    return r_library_internal_async_broadcast_receive(receiver, layout);
}

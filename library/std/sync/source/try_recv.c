#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncTryRecvResult r_std_sync_try_recv(const RStdSyncReceiver *endpoint, void *result_storage) {
    return r_library_internal_sync_channel_try_recv(endpoint->state, result_storage);
}

#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncRecvResult r_std_sync_recv(const RStdSyncReceiver *endpoint, void *result_storage) {
    return r_library_internal_sync_channel_recv(endpoint->state, result_storage, 1);
}

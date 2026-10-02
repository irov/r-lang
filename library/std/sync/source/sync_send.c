#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSendResult r_std_sync_sync_send(const RStdSyncSyncSender *endpoint, void *staged_value) {
    return r_library_internal_sync_channel_send(endpoint->state, staged_value, 1);
}

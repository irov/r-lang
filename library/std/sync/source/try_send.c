#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncTrySendResult r_std_sync_try_send(const RStdSyncSyncSender *endpoint, void *staged_value) {
    return r_library_internal_sync_channel_try_send(endpoint->state, staged_value);
}

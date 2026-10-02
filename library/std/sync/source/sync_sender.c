#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSyncSender r_std_sync_sync_sender(const RStdSyncSyncChannel *factory) {
    return r_library_internal_sync_channel_sync_sender(factory->state);
}

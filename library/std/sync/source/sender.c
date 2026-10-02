#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSender r_std_sync_sender(const RStdSyncChannel *factory) {
    return r_library_internal_sync_channel_sender(factory->state);
}

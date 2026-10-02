#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSyncSender r_std_sync_clone_sync_sender(const RStdSyncSyncSender *source) {
    return r_library_internal_sync_channel_clone_sync_sender(source->state);
}

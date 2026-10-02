#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncSender r_std_sync_clone_sender(const RStdSyncSender *source) {
    return r_library_internal_sync_channel_clone_sender(source->state);
}

#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncReceiver r_std_sync_receiver(RStdSyncChannel *factory) {
    return r_library_internal_sync_channel_receiver(&factory->state, 0);
}

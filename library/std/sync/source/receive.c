#include "r_std_sync.h"

#include "r_library_sync_receive_internal.h"

RStdSyncReceiveStartResult r_std_sync_receive(const RStdSyncReceiver *endpoint,
                                              RStdSyncReceiveLayout layout) {
    return r_library_internal_sync_receive(endpoint, layout);
}

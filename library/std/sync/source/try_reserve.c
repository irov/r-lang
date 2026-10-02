#include "r_std_sync.h"

#include "r_library_sync_internal.h"

RStdSyncTryReserveResult r_std_sync_try_reserve(const RStdSyncSyncSender *endpoint) {
    if ((endpoint == NULL) || (endpoint->state == NULL)) {
        r_library_internal_sync_contract_violation();
    }
    return r_library_internal_sync_channel_try_reserve(endpoint->state);
}

#include "r_std_sync.h"

#include "r_library_sync_receive_internal.h"

RStdAsyncStartResult r_std_sync_reserve(const RStdSyncSyncSender *endpoint,
                                        RStdSyncReserveLayout layout) {
    return r_library_internal_sync_reserve(endpoint, layout);
}

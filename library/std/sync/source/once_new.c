#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_once_new(RStdSyncOnce *result) {
    r_library_internal_sync_once_initialize(result);
}

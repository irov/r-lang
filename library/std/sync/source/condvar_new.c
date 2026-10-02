#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_condvar_new(RStdSyncCondvar *result) {
    r_library_internal_sync_condvar_initialize(result);
}

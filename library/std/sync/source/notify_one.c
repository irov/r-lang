#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_notify_one(const RStdSyncCondvar *condition) {
    r_library_internal_sync_condvar_notify(condition, 0);
}

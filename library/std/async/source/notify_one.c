#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

void r_std_async_notify_one(const RStdAsyncNotify *notify) {
    r_library_internal_async_notify_wake(notify, 0);
}

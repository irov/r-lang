#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncStartResult r_std_async_notified(const RStdAsyncNotify *notify) {
    return r_library_internal_async_notify_wait(notify);
}

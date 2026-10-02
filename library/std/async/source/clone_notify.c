#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

RStdAsyncNotify r_std_async_clone_notify(const RStdAsyncNotify *notify) {
    return r_library_internal_async_notify_clone(notify);
}

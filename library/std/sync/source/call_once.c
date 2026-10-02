#include "r_std_sync.h"

#include "r_library_sync_internal.h"

_Bool r_std_sync_call_once(const RStdSyncOnce *once,
                           RStdSyncOnceInitializer initializer,
                           void *context) {
    return r_library_internal_sync_once_call(once, initializer, context, 0);
}

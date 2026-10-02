#include "r_std_sync.h"

#include "r_library_sync_internal.h"

const void *r_std_sync_get_or_init(const RStdSyncOnceLock *lock,
                                   RStdSyncOnceLockInitializer initializer,
                                   void *context) {
    return r_library_internal_sync_once_lock_get_or_init(lock, initializer, context);
}

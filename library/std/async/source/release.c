#include "r_std_async.h"

#include "r_library_async_sync_internal.h"

void r_std_async_release(RStdAsyncSemaphorePermit *permit) {
    if (permit == NULL) {
        r_library_internal_async_contract_violation();
    }
    r_std_async_semaphore_permit_destroy(permit);
}

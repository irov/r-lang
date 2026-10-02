#include "r_std_sync.h"

#include <stdatomic.h>

RStdSyncBarrierCreateResult r_std_sync_barrier_new(RStdSyncBarrier *result, size_t count) {
    RStdSyncBarrierCreateResult created = {0};

    if (count == 0U) {
        created.status = R_STD_SYNC_BARRIER_CREATE_ERROR;
        created.error = R_STD_SYNC_BARRIER_ERROR_ZERO_PARTICIPANTS;
        return created;
    }
    atomic_init(&result->arrived, 0U);
    atomic_init(&result->generation, 0U);
    result->participants = count;
    result->initialized = 1;
    created.status = R_STD_SYNC_BARRIER_CREATE_SUCCESS;
    return created;
}

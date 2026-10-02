#include "r_std_sync.h"

#include "r_library_sync_internal.h"

void r_std_sync_send_permit(RStdSyncPermit *permit, void *staged_value) {
    if (permit == NULL) {
        r_library_internal_sync_contract_violation();
    }
    r_library_internal_sync_permit_send(permit, staged_value);
}

#ifndef R_LIBRARY_SYNC_RECEIVE_INTERNAL_H
#define R_LIBRARY_SYNC_RECEIVE_INTERNAL_H

#include "r_std_sync.h"

RStdSyncReceiveStartResult r_library_internal_sync_receive(const RStdSyncReceiver *endpoint,
                                                           RStdSyncReceiveLayout layout);
RStdAsyncStartResult r_library_internal_sync_reserve(const RStdSyncSyncSender *endpoint,
                                                     RStdSyncReserveLayout layout);

#endif

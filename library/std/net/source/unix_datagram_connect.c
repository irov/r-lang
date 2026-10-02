#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_unix_datagram_connect(RStdStringView path,
                                                       RStdNetDeadline deadline) {
    return r_library_internal_net_unix_datagram_connect(path, deadline);
}

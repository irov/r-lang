#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult
r_std_net_unix_datagram_bind(RStdStringView path, _Bool replace, RStdNetDeadline deadline) {
    return r_library_internal_net_unix_datagram_bind(path, replace, deadline);
}

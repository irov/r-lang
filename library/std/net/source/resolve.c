#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_resolve(RStdStringView host,
                                         uint16_t port,
                                         RStdNetFamily family,
                                         RStdNetDeadline deadline) {
    return r_library_internal_net_resolve(host, port, family, deadline);
}

#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_udp_close(RStdNetUdpSocket *socket, RStdNetDeadline deadline) {
    return r_library_internal_net_udp_close(socket, deadline);
}

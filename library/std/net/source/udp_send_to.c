#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_udp_send_to(const RStdNetUdpSocket *socket,
                                             RStdNetSocketAddress peer,
                                             RRuntimeArray *buffer,
                                             RStdNetDeadline deadline) {
    return r_library_internal_net_udp_send_to(socket, peer, buffer, deadline);
}

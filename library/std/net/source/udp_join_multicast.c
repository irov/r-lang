#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetOptionResult r_std_net_udp_join_multicast(const RStdNetUdpSocket *socket,
                                                 RStdNetIpAddress group,
                                                 uint32_t interface_index) {
    return r_library_internal_net_udp_membership(
        &socket->storage->handle, group, interface_index, 1);
}

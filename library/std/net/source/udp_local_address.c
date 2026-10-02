#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetSocketAddressResult r_std_net_udp_local_address(const RStdNetUdpSocket *socket) {
    return r_library_internal_net_observe_address(
        &socket->storage->handle, R_LIBRARY_NET_HANDLE_UDP_SOCKET, 0);
}

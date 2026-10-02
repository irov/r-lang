#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetSocketAddressResult
r_std_net_tcp_listener_local_address(const RStdNetTcpListener *listener) {
    return r_library_internal_net_observe_address(
        &listener->storage->handle, R_LIBRARY_NET_HANDLE_TCP_LISTENER, 0);
}

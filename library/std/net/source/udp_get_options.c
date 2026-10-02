#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetUdpOptionsResult r_std_net_udp_get_options(const RStdNetUdpSocket *socket) {
    return r_library_internal_net_udp_get_options(&socket->storage->handle);
}

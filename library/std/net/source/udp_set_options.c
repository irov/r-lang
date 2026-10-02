#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetOptionResult r_std_net_udp_set_options(const RStdNetUdpSocket *socket,
                                              RStdNetUdpOptions options) {
    return r_library_internal_net_udp_set_options(&socket->storage->handle, options);
}

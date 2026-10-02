#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetSocketAddressResult r_std_net_tcp_local_address(const RStdNetTcpStream *stream) {
    return r_library_internal_net_observe_address(
        &stream->storage->handle, R_LIBRARY_NET_HANDLE_TCP_STREAM, 0);
}

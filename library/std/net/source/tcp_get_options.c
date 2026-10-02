#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTcpOptionsResult r_std_net_tcp_get_options(const RStdNetTcpStream *stream) {
    return r_library_internal_net_tcp_get_options(&stream->storage->handle);
}

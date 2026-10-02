#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetOptionResult r_std_net_tcp_set_options(const RStdNetTcpStream *stream,
                                              RStdNetTcpOptions options) {
    return r_library_internal_net_tcp_set_options(&stream->storage->handle, options);
}

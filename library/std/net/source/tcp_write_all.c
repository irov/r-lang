#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_tcp_write_all(const RStdNetTcpStream *stream,
                                               RRuntimeArray *buffer,
                                               RStdNetDeadline deadline) {
    return r_library_internal_net_tcp_write_all(stream, buffer, deadline);
}

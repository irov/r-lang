#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_tcp_read(const RStdNetTcpStream *stream,
                                          RRuntimeArray *buffer,
                                          RStdNetDeadline deadline) {
    return r_library_internal_net_tcp_read(stream, buffer, deadline);
}

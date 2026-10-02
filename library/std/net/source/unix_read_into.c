#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_unix_read_into(const RStdNetUnixStream *stream,
                                                RStdNetMutableBytes target,
                                                RStdNetDeadline deadline) {
    return r_library_internal_net_unix_read_into(stream, target, deadline);
}

#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_unix_shutdown(const RStdNetUnixStream *stream,
                                               RStdNetShutdownDirection direction,
                                               RStdNetDeadline deadline) {
    return r_library_internal_net_unix_shutdown(stream, direction, deadline);
}

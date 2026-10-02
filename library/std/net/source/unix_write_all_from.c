#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetTaskStartResult r_std_net_unix_write_all_from(const RStdNetUnixStream *stream,
                                                     RStdNetConstBytes source,
                                                     RStdNetDeadline deadline) {
    return r_library_internal_net_unix_write_all_from(stream, source, deadline);
}

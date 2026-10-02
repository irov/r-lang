#include "r_std_signal.h"

#include "r_library_signal_internal.h"

RStdProcessTaskStartResult r_std_signal_next(const RStdSignalListener *listener,
                                             RStdProcessDeadline deadline) {
    return r_library_internal_signal_next(listener, deadline);
}

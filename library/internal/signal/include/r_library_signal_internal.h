#ifndef R_LIBRARY_SIGNAL_INTERNAL_H
#define R_LIBRARY_SIGNAL_INTERNAL_H

#include "r_runtime_darwin_signal.h"
#include "r_std_signal.h"

RStdSignalListenerResult r_library_internal_signal_listen(RRuntimeAllocator *allocator,
                                                          RStdSignalKind kind);
RStdProcessTaskStartResult r_library_internal_signal_next(const RStdSignalListener *listener,
                                                          RStdProcessDeadline deadline);
RStdProcessVoidResult r_library_internal_signal_raise(RStdSignalKind kind);

#endif

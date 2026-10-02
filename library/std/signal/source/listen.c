#include "r_std_signal.h"

#include "r_library_signal_internal.h"

RStdSignalListenerResult r_std_signal_listen(RRuntimeAllocator *allocator, RStdSignalKind kind) {
    return r_library_internal_signal_listen(allocator, kind);
}

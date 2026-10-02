#include "r_std_signal.h"

#include "r_library_signal_internal.h"

RStdProcessVoidResult r_std_signal_raise(RStdSignalKind kind) {
    return r_library_internal_signal_raise(kind);
}

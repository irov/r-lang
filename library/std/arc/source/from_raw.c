#include "r_std_arc.h"

#include "r_library_arc_internal.h"

RStdArcCallStatus r_std_arc_from_raw(const void *pointer, RRuntimeArc *result) {
    return r_library_internal_arc_status(r_runtime_arc_from_raw(pointer, result));
}

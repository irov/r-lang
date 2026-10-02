#include "r_std_arc.h"

#include "r_library_arc_internal.h"

RStdArcCallStatus r_std_arc_clone(const RRuntimeArc *source, RRuntimeArc *result) {
    return r_library_internal_arc_status(r_runtime_arc_clone(source, result));
}

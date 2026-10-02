#include "r_std_arc.h"

#include "r_library_arc_internal.h"

RStdArcCallStatus r_std_arc_downgrade(const RRuntimeArc *source, RRuntimeWeakArc *result) {
    return r_library_internal_arc_status(r_runtime_arc_downgrade(source, result));
}

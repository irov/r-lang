#include "r_std_format.h"

#include "r_library_float_format_internal.h"

RStdFormatAppendResult r_std_format_append_f64(RStdFormatBuilder *target, double value) {
    RLibraryFloatFormatValue erased = {.f64 = value};

    return r_library_internal_float_append(target, erased, R_LIBRARY_FLOAT_FORMAT_F64);
}

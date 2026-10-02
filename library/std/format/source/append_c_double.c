#include "r_std_format.h"

#include "r_library_float_format_internal.h"

RStdFormatAppendResult r_std_format_append_c_double(RStdFormatBuilder *target, double value) {
    RLibraryFloatFormatValue erased = {.c_double = value};

    return r_library_internal_float_append(target, erased, R_LIBRARY_FLOAT_FORMAT_C_DOUBLE);
}

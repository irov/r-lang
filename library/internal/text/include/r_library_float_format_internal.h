#ifndef R_LIBRARY_FLOAT_FORMAT_INTERNAL_H
#define R_LIBRARY_FLOAT_FORMAT_INTERNAL_H

#include "r_std_format.h"

typedef enum RLibraryFloatFormatDestination {
    R_LIBRARY_FLOAT_FORMAT_F32 = 0,
    R_LIBRARY_FLOAT_FORMAT_F64 = 1,
    R_LIBRARY_FLOAT_FORMAT_C_FLOAT = 2,
    R_LIBRARY_FLOAT_FORMAT_C_DOUBLE = 3,
    R_LIBRARY_FLOAT_FORMAT_C_LONG_DOUBLE = 4
} RLibraryFloatFormatDestination;

typedef union RLibraryFloatFormatValue {
    float f32;
    double f64;
    float c_float;
    double c_double;
    long double c_long_double;
} RLibraryFloatFormatValue;

RStdFormatAppendResult r_library_internal_float_append(RStdFormatBuilder *target,
                                                       RLibraryFloatFormatValue value,
                                                       RLibraryFloatFormatDestination destination);

#endif

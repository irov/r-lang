#ifndef R_LIBRARY_FLOAT_PARSE_INTERNAL_H
#define R_LIBRARY_FLOAT_PARSE_INTERNAL_H

#include "r_std_convert.h"

typedef enum RLibraryFloatDestination {
    R_LIBRARY_FLOAT_DESTINATION_F32 = 0,
    R_LIBRARY_FLOAT_DESTINATION_F64 = 1,
    R_LIBRARY_FLOAT_DESTINATION_C_FLOAT = 2,
    R_LIBRARY_FLOAT_DESTINATION_C_DOUBLE = 3,
    R_LIBRARY_FLOAT_DESTINATION_C_LONG_DOUBLE = 4
} RLibraryFloatDestination;

typedef union RLibraryFloatValue {
    float f32;
    double f64;
    float c_float;
    double c_double;
    long double c_long_double;
} RLibraryFloatValue;

typedef struct RLibraryFloatParseResult {
    RStdConvertCallStatus status;
    RLibraryFloatValue value;
    RStdConvertParseError error;
} RLibraryFloatParseResult;

RLibraryFloatParseResult r_library_internal_float_parse(RStdStringView source,
                                                        RLibraryFloatDestination destination);

#endif

#include "r_std_convert.h"

#include "r_library_float_parse_internal.h"

RStdConvertParseCFloatResult r_std_convert_parse_c_float(RStdStringView source) {
    RLibraryFloatParseResult parsed =
        r_library_internal_float_parse(source, R_LIBRARY_FLOAT_DESTINATION_C_FLOAT);
    RStdConvertParseCFloatResult result = {0};

    result.status = parsed.status;
    result.error = parsed.error;
    if (parsed.status == R_STD_CONVERT_CALL_SUCCESS) {
        result.value = parsed.value.c_float;
    }
    return result;
}

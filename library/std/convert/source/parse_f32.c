#include "r_std_convert.h"

#include "r_library_float_parse_internal.h"

RStdConvertParseF32Result r_std_convert_parse_f32(RStdStringView source) {
    RLibraryFloatParseResult parsed =
        r_library_internal_float_parse(source, R_LIBRARY_FLOAT_DESTINATION_F32);
    RStdConvertParseF32Result result = {0};

    result.status = parsed.status;
    result.error = parsed.error;
    if (parsed.status == R_STD_CONVERT_CALL_SUCCESS) {
        result.value = parsed.value.f32;
    }
    return result;
}

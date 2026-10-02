#include "r_std_array.h"

#include "r_library_array_internal.h"

RStdArrayConstSliceResult r_std_array_as_slice(const RStdArray *source) {
    RStdArrayConstSliceResult result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.data = source->data;
    result.length = source->length;
    return result;
}

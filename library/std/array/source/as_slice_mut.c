#include "r_std_array.h"

#include "r_library_array_internal.h"

RStdArrayMutSliceResult r_std_array_as_slice_mut(RStdArray *source) {
    RStdArrayMutSliceResult result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.data = source->data;
    result.length = source->length;
    return result;
}

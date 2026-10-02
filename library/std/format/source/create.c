#include "r_std_format.h"

RStdFormatBuilderResult r_std_format_create(RRuntimeAllocator *allocator) {
    RStdStringCreateResult created = r_std_string_create(allocator);
    RStdFormatBuilderResult result = {0};

    result.status = R_STD_FORMAT_CALL_SUCCESS;
    result.value.output = created.value;
    return result;
}

#include "r_library_json_internal.h"

#include <string.h>

RStdJsonResult r_json_allocation_result(RRuntimeAllocationStatus status) {
    RStdJsonResult result = {0};
    if (status == R_RUNTIME_ALLOCATION_OK)
        return result;
    result.status = R_STD_JSON_CALL_ALLOCATION_ERROR;
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW)
        result.allocation_error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
    else if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT)
        result.allocation_error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    else if (status == R_RUNTIME_ALLOCATION_INVALID)
        result.status = R_STD_JSON_CALL_CONTRACT_VIOLATION;
    return result;
}
RStdJsonResult r_json_array_result(RRuntimeArrayStatus status) {
    switch (status) {
    case R_RUNTIME_ARRAY_OK:
        return (RStdJsonResult){0};
    case R_RUNTIME_ARRAY_SIZE_OVERFLOW:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
    case R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT);
    case R_RUNTIME_ARRAY_INVALID:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_INVALID);
    default:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_EXHAUSTED);
    }
}
RStdJsonResult r_json_string_result(RRuntimeStringStatus status) {
    switch (status) {
    case R_RUNTIME_STRING_OK:
        return (RStdJsonResult){0};
    case R_RUNTIME_STRING_SIZE_OVERFLOW:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
    case R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT);
    case R_RUNTIME_STRING_INVALID_UTF8:
        return r_json_failure(R_STD_JSON_ERROR_INVALID_UTF8, 0U);
    case R_RUNTIME_STRING_INVALID:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_INVALID);
    default:
        return r_json_allocation_result(R_RUNTIME_ALLOCATION_EXHAUSTED);
    }
}
RStdJsonResult r_json_failure(RStdJsonErrorCode code, size_t offset) {
    RStdJsonResult result = {0};
    result.status = R_STD_JSON_CALL_JSON_ERROR;
    result.error.code = code;
    result.error.offset = offset;
    return result;
}
RStdJsonByteView r_json_string_view(const RStdString *string) {
    return (RStdJsonByteView){r_runtime_string_bytes(string), r_runtime_string_length(string)};
}
bool r_json_view_equal(RStdJsonByteView a, RStdJsonByteView b) {
    return a.length == b.length && (a.length == 0U || memcmp(a.data, b.data, a.length) == 0);
}
RStdJsonResult
r_json_copy_text(RStdString *target, RRuntimeAllocator *allocator, RStdJsonByteView source) {
    size_t invalid = 0U;
    RRuntimeStringStatus status =
        r_runtime_string_from_utf8(target, allocator, source.data, source.length, &invalid);
    RStdJsonResult result = r_json_string_result(status);
    if (result.status == R_STD_JSON_CALL_JSON_ERROR)
        result.error.offset = invalid;
    return result;
}
void r_json_error_destroy(RStdJsonError *error) {
    r_runtime_string_destroy(&error->pointer);
    *error = (RStdJsonError){0};
}
void r_json_number_destroy(RStdJsonNumber *number) {
    r_runtime_string_destroy(&number->text);
}

RStdJsonValueResult r_json_string_value(RRuntimeAllocator *allocator, RStdJsonByteView value) {
    RStdJsonValueResult result = r_json_new_node(allocator, R_STD_JSON_STRING);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    result.outcome = r_json_copy_text(&result.value.node->as.text, allocator, value);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        r_json_value_destroy(&result.value);
    return result;
}

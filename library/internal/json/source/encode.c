#include "r_library_float_format_internal.h"
#include "r_library_json_internal.h"

#include <math.h>

RStdJsonValueResult r_json_encode_integer(RRuntimeAllocator *allocator,
                                          bool negative,
                                          uint64_t magnitude,
                                          bool quoted) {
    uint8_t buffer[21];
    size_t at = sizeof(buffer);
    RStdJsonValueResult result;
    do {
        buffer[--at] = (uint8_t)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    if (negative)
        buffer[--at] = '-';
    result = r_json_new_node(allocator, quoted ? R_STD_JSON_STRING : R_STD_JSON_NUMBER);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    result.outcome = r_json_copy_text(&result.value.node->as.text,
                                      allocator,
                                      (RStdJsonByteView){buffer + at, sizeof(buffer) - at});
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        r_json_value_destroy(&result.value);
    return result;
}
RStdJsonValueResult r_json_encode_float(RRuntimeAllocator *allocator,
                                        long double value,
                                        uint32_t representation,
                                        bool quoted) {
    RStdJsonValueResult result = {0};
    RLibraryFloatFormatValue payload = {0};
    RStdFormatBuilder builder = {0};
    RStdFormatAppendResult appended;
    if (!isfinite(value)) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_RANGE, 0U);
        return result;
    }
    switch (representation) {
    case 0U:
        payload.f32 = (float)value;
        break;
    case 1U:
        payload.f64 = (double)value;
        break;
    case 2U:
        payload.c_float = (float)value;
        break;
    case 3U:
        payload.c_double = (double)value;
        break;
    case 4U:
        payload.c_long_double = value;
        break;
    default:
        result.outcome.status = R_STD_JSON_CALL_CONTRACT_VIOLATION;
        return result;
    }
    result.outcome = r_json_string_result(r_runtime_string_initialize(&builder.output, allocator));
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    appended = r_library_internal_float_append(
        &builder, payload, (RLibraryFloatFormatDestination)representation);
    if (appended.status != R_STD_FORMAT_CALL_SUCCESS) {
        result.outcome.status = R_STD_JSON_CALL_ALLOCATION_ERROR;
        result.outcome.allocation_error = appended.error.allocation_error;
    } else {
        result = r_json_new_node(allocator, quoted ? R_STD_JSON_STRING : R_STD_JSON_NUMBER);
        if (result.outcome.status == R_STD_JSON_CALL_SUCCESS) {
            result.value.node->as.text = builder.output;
            builder.output = (RStdString){0};
        }
    }
    r_runtime_string_destroy(&builder.output);
    return result;
}
RStdJsonValueResult r_json_encode_char(RRuntimeAllocator *allocator, uint32_t scalar) {
    RStdJsonValueResult result = {0};
    uint8_t bytes[4];
    size_t count;
    if (scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU)) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
        return result;
    }
    if (scalar < 0x80U) {
        bytes[0] = (uint8_t)scalar;
        count = 1U;
    } else if (scalar < 0x800U) {
        bytes[0] = (uint8_t)(0xc0U | scalar >> 6U);
        bytes[1] = (uint8_t)(0x80U | (scalar & 0x3fU));
        count = 2U;
    } else if (scalar < 0x10000U) {
        bytes[0] = (uint8_t)(0xe0U | scalar >> 12U);
        bytes[1] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | (scalar & 0x3fU));
        count = 3U;
    } else {
        bytes[0] = (uint8_t)(0xf0U | scalar >> 18U);
        bytes[1] = (uint8_t)(0x80U | ((scalar >> 12U) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3fU));
        bytes[3] = (uint8_t)(0x80U | (scalar & 0x3fU));
        count = 4U;
    }
    return r_json_string_value(allocator, (RStdJsonByteView){bytes, count});
}
bool r_json_value_empty(const RStdJsonValue *value) {
    const struct RJsonNode *node = value->node;
    if (node == NULL || node->kind == R_STD_JSON_NULL)
        return true;
    if (node->kind == R_STD_JSON_ARRAY)
        return node->as.elements.length == 0U;
    if (node->kind == R_STD_JSON_OBJECT)
        return node->as.members.length == 0U;
    if (node->kind == R_STD_JSON_STRING)
        return r_runtime_string_length(&node->as.text) == 0U;
    return false;
}

RStdJsonResult r_json_quote_number(RStdJsonValue *value) {
    if (value == NULL || value->node == NULL || value->node->kind != R_STD_JSON_NUMBER)
        return r_json_failure(R_STD_JSON_ERROR_TYPE, 0U);
    value->node->kind = R_STD_JSON_STRING;
    return (RStdJsonResult){0};
}

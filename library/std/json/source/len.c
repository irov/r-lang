#include "r_library_json_internal.h"

size_t r_std_json_len(const RStdJsonValue *value) {
    if (value->node == NULL)
        return 0U;
    if (value->node->kind == R_STD_JSON_ARRAY)
        return value->node->as.elements.length;
    if (value->node->kind == R_STD_JSON_OBJECT)
        return value->node->as.members.length;
    if (value->node->kind == R_STD_JSON_STRING || value->node->kind == R_STD_JSON_NUMBER)
        return r_runtime_string_length(&value->node->as.text);
    return 0U;
}

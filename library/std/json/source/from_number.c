#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_from_number(RRuntimeAllocator *allocator,
                                           const RStdJsonNumber *value) {
    RStdJsonValueResult result = r_json_new_node(allocator, R_STD_JSON_NUMBER);
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    result.outcome =
        r_json_copy_text(&result.value.node->as.text, allocator, r_json_string_view(&value->text));
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
        r_json_value_destroy(&result.value);
    return result;
}

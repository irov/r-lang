#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_from_bool(RRuntimeAllocator *allocator, bool value) {
    RStdJsonValueResult result = r_json_new_node(allocator, R_STD_JSON_BOOLEAN);
    if (result.outcome.status == R_STD_JSON_CALL_SUCCESS)
        result.value.node->as.boolean = value;
    return result;
}

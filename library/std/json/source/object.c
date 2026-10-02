#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_object(RRuntimeAllocator *allocator) {
    return r_json_new_node(allocator, R_STD_JSON_OBJECT);
}

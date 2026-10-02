#include "r_library_json_internal.h"

RStdJsonStringResult r_std_json_stringify_with_options(RRuntimeAllocator *allocator,
                                                       const RStdJsonValue *value,
                                                       RStdJsonOptions options) {
    return r_json_stringify(allocator, value, options);
}

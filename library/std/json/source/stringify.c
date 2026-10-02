#include "r_library_json_internal.h"

RStdJsonStringResult r_std_json_stringify(RRuntimeAllocator *allocator,
                                          const RStdJsonValue *value) {
    return r_json_stringify(allocator, value, R_STD_JSON_DEFAULT_OPTIONS);
}

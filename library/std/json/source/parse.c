#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_parse(RRuntimeAllocator *allocator, RStdJsonByteView source) {
    return r_json_parse(allocator, source, R_STD_JSON_DEFAULT_OPTIONS);
}

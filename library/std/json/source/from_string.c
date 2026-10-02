#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_from_string(RRuntimeAllocator *allocator, RStdJsonByteView value) {
    return r_json_string_value(allocator, value);
}

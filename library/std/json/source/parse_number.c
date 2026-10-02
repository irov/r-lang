#include "r_library_json_internal.h"

RStdJsonNumberResult r_std_json_parse_number(RRuntimeAllocator *allocator,
                                             RStdJsonByteView source) {
    return r_json_number(allocator, source);
}

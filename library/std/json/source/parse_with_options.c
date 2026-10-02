#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_parse_with_options(RRuntimeAllocator *allocator,
                                                  RStdJsonByteView source,
                                                  RStdJsonOptions options) {
    return r_json_parse(allocator, source, options);
}

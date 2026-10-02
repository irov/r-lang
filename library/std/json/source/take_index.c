#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_take_index(RStdJsonValue *target, size_t index) {
    return r_json_take_index(target, index);
}

#include "r_library_json_internal.h"

RStdJsonValueResult r_std_json_take_field(RStdJsonValue *target, RStdJsonByteView key) {
    return r_json_take_field(target, key);
}

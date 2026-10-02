#include "r_library_json_internal.h"

RStdJsonResult
r_std_json_insert(RStdJsonValue *target, RStdJsonByteView key, RStdJsonValue *value) {
    return r_json_insert(target, key, value);
}

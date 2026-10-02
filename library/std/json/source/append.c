#include "r_library_json_internal.h"

RStdJsonResult r_std_json_append(RStdJsonValue *target, RStdJsonValue *value) {
    return r_json_append(target, value);
}

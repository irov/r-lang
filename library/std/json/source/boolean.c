#include "r_library_json_internal.h"

bool r_std_json_boolean(const RStdJsonValue *value) {
    return value->node != NULL && value->node->kind == R_STD_JSON_BOOLEAN &&
           value->node->as.boolean;
}

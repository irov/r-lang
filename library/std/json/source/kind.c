#include "r_library_json_internal.h"

RStdJsonKind r_std_json_kind(const RStdJsonValue *value) {
    return value->node == NULL ? R_STD_JSON_NULL : value->node->kind;
}

#include "r_library_json_internal.h"

const RStdJsonValue *r_std_json_get(const RStdJsonValue *value, size_t index) {
    if (value->node == NULL)
        return NULL;
    if (value->node->kind == R_STD_JSON_ARRAY)
        return r_runtime_array_get(&value->node->as.elements, index);
    if (value->node->kind == R_STD_JSON_OBJECT) {
        const RJsonMember *member = r_runtime_array_get(&value->node->as.members, index);
        return member == NULL ? NULL : &member->value;
    }
    return NULL;
}

#include "r_library_json_internal.h"

const RStdJsonValue *r_std_json_find(const RStdJsonValue *value, RStdJsonByteView key) {
    if (value->node == NULL || value->node->kind != R_STD_JSON_OBJECT)
        return NULL;
    for (size_t i = 0U; i < value->node->as.members.length; ++i) {
        RJsonMember *member = &((RJsonMember *)value->node->as.members.data)[i];
        if (r_json_view_equal(r_json_string_view(&member->key), key))
            return &member->value;
    }
    return NULL;
}

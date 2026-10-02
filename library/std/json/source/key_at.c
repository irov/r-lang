#include "r_library_json_internal.h"

RStdJsonByteView r_std_json_key_at(const RStdJsonValue *value, size_t index) {
    if (value->node != NULL && value->node->kind == R_STD_JSON_OBJECT) {
        const RJsonMember *member = r_runtime_array_get(&value->node->as.members, index);
        if (member != NULL)
            return r_json_string_view(&member->key);
    }
    return (RStdJsonByteView){0};
}

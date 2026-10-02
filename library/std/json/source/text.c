#include "r_library_json_internal.h"

RStdJsonByteView r_std_json_text(const RStdJsonValue *value) {
    if (value->node != NULL &&
        (value->node->kind == R_STD_JSON_STRING || value->node->kind == R_STD_JSON_NUMBER))
        return r_json_string_view(&value->node->as.text);
    return (RStdJsonByteView){0};
}

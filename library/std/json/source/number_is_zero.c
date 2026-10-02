#include "r_library_json_internal.h"

bool r_std_json_number_is_zero(const RStdJsonNumber *value) {
    return r_json_number_is_zero(r_json_string_view(&value->text));
}

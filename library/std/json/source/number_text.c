#include "r_library_json_internal.h"

RStdJsonByteView r_std_json_number_text(const RStdJsonNumber *value) {
    return r_json_string_view(&value->text);
}

#include "r_library_json_internal.h"

bool r_std_json_name_equal(RStdJsonByteView left, RStdJsonByteView right, bool ignore_case) {
    return r_json_name_equal(left, right, ignore_case);
}

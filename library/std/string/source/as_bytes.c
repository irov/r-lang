#include "r_std_string.h"

RStdStringView r_std_string_as_bytes(const RStdString *source) {
    RStdStringView result = {
        r_runtime_string_bytes(source),
        r_runtime_string_length(source),
    };

    return result;
}

#include "r_std_string.h"

size_t r_std_string_len(const RStdString *source) {
    return r_runtime_string_length(source);
}

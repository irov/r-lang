#include "r_std_string.h"

size_t r_std_string_capacity(const RStdString *source) {
    return r_runtime_string_capacity(source);
}

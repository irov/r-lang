#include "r_std_c.h"

const char *r_std_c_string_as_ptr(const RStdCString *source) {
    return (const char *)source->data;
}

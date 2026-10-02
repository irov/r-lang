#include "r_std_c.h"

RStdCCharSlice r_std_c_string_as_slice(const RStdCString *source) {
    return (RStdCCharSlice){(const char *)source->data, source->length};
}

#include "r_std_c.h"

void *r_std_c_release_handle(RStdCHandle *handle) {
    void *pointer;

    pointer = handle->pointer;
    handle->pointer = NULL;
    handle->destructor = NULL;
    return pointer;
}

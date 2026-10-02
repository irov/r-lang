#include "allocation.h"

#include <stdlib.h>

void *r_cli_allocate(size_t size) {
    return malloc(size);
}

void *r_cli_reallocate(void *allocation, size_t size) {
    return realloc(allocation, size);
}

void r_cli_deallocate(void *allocation) {
    free(allocation);
}

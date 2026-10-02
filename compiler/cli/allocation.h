#ifndef R_COMPILER_CLI_ALLOCATION_H
#define R_COMPILER_CLI_ALLOCATION_H

#include <stddef.h>

void *r_cli_allocate(size_t size);
void *r_cli_reallocate(void *allocation, size_t size);
void r_cli_deallocate(void *allocation);

#endif

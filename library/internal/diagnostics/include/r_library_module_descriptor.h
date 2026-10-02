#ifndef R_LIBRARY_INTERNAL_MODULE_DESCRIPTOR_H
#define R_LIBRARY_INTERNAL_MODULE_DESCRIPTOR_H

#include <stdint.h>

typedef struct RLibraryModuleDescriptor {
    const char *r_module_name;
    const char *cmake_target;
    uint32_t abi_revision;
    uint32_t public_operation_count;
} RLibraryModuleDescriptor;

#endif

#include "r_library_module_descriptor.h"

#ifndef R_LIBRARY_MODULE_TOKEN
#error "R_LIBRARY_MODULE_TOKEN must name the module descriptor symbol"
#endif

#ifndef R_LIBRARY_MODULE_R_NAME
#error "R_LIBRARY_MODULE_R_NAME must contain the R module name"
#endif

#ifndef R_LIBRARY_MODULE_TARGET
#error "R_LIBRARY_MODULE_TARGET must contain the CMake target name"
#endif

#ifndef R_LIBRARY_MODULE_ABI_REVISION
#error "R_LIBRARY_MODULE_ABI_REVISION must contain the internal ABI revision"
#endif

#ifndef R_LIBRARY_MODULE_PUBLIC_OPERATION_COUNT
#error "R_LIBRARY_MODULE_PUBLIC_OPERATION_COUNT must contain the source operation count"
#endif

#define R_LIBRARY_INTERNAL_JOIN_RAW(left, right) left##right
#define R_LIBRARY_INTERNAL_JOIN(left, right) R_LIBRARY_INTERNAL_JOIN_RAW(left, right)
#define R_LIBRARY_INTERNAL_SUFFIX_RAW(token) token##_descriptor
#define R_LIBRARY_INTERNAL_SUFFIX(token) R_LIBRARY_INTERNAL_SUFFIX_RAW(token)
#define R_LIBRARY_INTERNAL_STRINGIFY_RAW(value) #value
#define R_LIBRARY_INTERNAL_STRINGIFY(value) R_LIBRARY_INTERNAL_STRINGIFY_RAW(value)
#define R_LIBRARY_INTERNAL_DESCRIPTOR(token)                                                       \
    R_LIBRARY_INTERNAL_JOIN(r_library_internal_module_, R_LIBRARY_INTERNAL_SUFFIX(token))

const RLibraryModuleDescriptor *R_LIBRARY_INTERNAL_DESCRIPTOR(R_LIBRARY_MODULE_TOKEN)(void);

const RLibraryModuleDescriptor *R_LIBRARY_INTERNAL_DESCRIPTOR(R_LIBRARY_MODULE_TOKEN)(void) {
    static const RLibraryModuleDescriptor descriptor = {
        R_LIBRARY_INTERNAL_STRINGIFY(R_LIBRARY_MODULE_R_NAME),
        R_LIBRARY_INTERNAL_STRINGIFY(R_LIBRARY_MODULE_TARGET),
        R_LIBRARY_MODULE_ABI_REVISION,
        R_LIBRARY_MODULE_PUBLIC_OPERATION_COUNT,
    };

    return &descriptor;
}

#include "r_std_env.h"

#include "r_library_environment_internal.h"

RStdEnvVoidResult
r_std_env_set(RRuntimeAllocator *allocator, RStdStringView name, RStdStringView value) {
    return r_library_internal_environment_set(allocator, name, value);
}

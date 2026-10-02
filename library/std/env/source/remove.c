#include "r_std_env.h"

#include "r_library_environment_internal.h"

RStdEnvVoidResult r_std_env_remove(RRuntimeAllocator *allocator, RStdStringView name) {
    return r_library_internal_environment_remove(allocator, name);
}

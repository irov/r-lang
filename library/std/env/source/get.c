#include "r_std_env.h"

#include "r_library_environment_internal.h"

RStdEnvGetResult r_std_env_get(RRuntimeAllocator *allocator, RStdStringView name) {
    return r_library_internal_environment_get(allocator, name);
}

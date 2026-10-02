#include "r_std_env.h"

#include "r_library_environment_internal.h"

RStdEnvVariablesResult r_std_env_variables(RRuntimeAllocator *allocator, uint64_t seed) {
    return r_library_internal_environment_variables(allocator, seed);
}

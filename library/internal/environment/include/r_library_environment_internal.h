#ifndef R_LIBRARY_ENVIRONMENT_INTERNAL_H
#define R_LIBRARY_ENVIRONMENT_INTERNAL_H

#include "r_std_env.h"

_Bool r_library_internal_environment_acquire(void);
_Bool r_library_internal_environment_release(void);
RRuntimeTypeInfo r_library_internal_environment_string_type(void);
RStdEnvVariablesResult r_library_internal_environment_variables(RRuntimeAllocator *allocator,
                                                                uint64_t seed);
RStdEnvGetResult r_library_internal_environment_get(RRuntimeAllocator *allocator,
                                                    RStdStringView name);
RStdEnvVoidResult r_library_internal_environment_set(RRuntimeAllocator *allocator,
                                                     RStdStringView name,
                                                     RStdStringView value);
RStdEnvVoidResult r_library_internal_environment_remove(RRuntimeAllocator *allocator,
                                                        RStdStringView name);

#endif

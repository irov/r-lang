#include "r_std_thread.h"

#include "r_library_thread_internal.h"

RStdThreadScopedSpawnResult r_std_thread_spawn_scoped(RRuntimeAllocator *allocator,
                                                      RRuntimeTypeInfo payload_type,
                                                      RRuntimeTypeInfo result_type,
                                                      RStdThreadEntryFn entry,
                                                      void *staged_payload) {
    RStdThreadScopedSpawnResult result = {0};
    result.error = r_library_internal_thread_spawn(allocator,
                                                   payload_type,
                                                   result_type,
                                                   entry,
                                                   staged_payload,
                                                   1,
                                                   &result.value,
                                                   &result.is_ok);
    return result;
}

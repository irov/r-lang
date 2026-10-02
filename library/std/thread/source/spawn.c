#include "r_std_thread.h"

#include "r_library_thread_internal.h"

RStdThreadSpawnResult r_std_thread_spawn(RRuntimeAllocator *allocator,
                                         RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RStdThreadEntryFn entry,
                                         void *staged_payload) {
    RStdThreadSpawnResult result = {0};
    result.error = r_library_internal_thread_spawn(allocator,
                                                   payload_type,
                                                   result_type,
                                                   entry,
                                                   staged_payload,
                                                   0,
                                                   &result.value,
                                                   &result.is_ok);
    return result;
}

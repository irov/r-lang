#ifndef R_LIBRARY_THREAD_INTERNAL_H
#define R_LIBRARY_THREAD_INTERNAL_H

#include "r_std_thread.h"

RStdThreadError r_library_internal_thread_spawn(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RStdThreadEntryFn entry,
                                                void *staged_payload,
                                                _Bool scoped,
                                                RStdThreadJoinHandle *result,
                                                _Bool *started);

RStdThreadError
r_library_internal_thread_spawn_with_completion(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo payload_type,
                                                RStdThreadCompletionTypeInfo completion_type,
                                                RStdThreadEntryFn entry,
                                                void *staged_payload,
                                                _Bool scoped,
                                                RStdThreadJoinHandle *result,
                                                _Bool *started);

RStdThread r_library_internal_thread_current(void);
RStdThread r_library_internal_thread_clone(const RStdThread *source);
void r_library_internal_thread_unpark(const RStdThread *target);
void r_library_internal_thread_park(void);
RStdThreadJoinResult r_library_internal_thread_join(RStdThreadJoinHandle *handle);
void r_library_internal_thread_detach(RStdThreadJoinHandle *handle);
_Noreturn void r_library_internal_thread_contract_violation(void);

#if defined(R_STD_THREAD_TESTING)
void r_library_internal_thread_testing_fail_create(int native_error);
#endif

#endif

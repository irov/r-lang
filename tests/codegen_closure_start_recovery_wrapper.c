#include "r_runtime_0_1.h"
#include "r_runtime_own.h"
#include "r_runtime_task.h"
#include "r_std_thread.h"

#include <stdbool.h>
#include <stdint.h>

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
RStdThreadSpawnResult r_test_spawn(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload);
void r_test_release(RRuntimeOwn *owner);
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_library_internal_thread_spawn_checked r_test_spawn
#define r_runtime_own_release r_test_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release
#undef r_library_internal_thread_spawn_checked
#undef r_runtime_task_resumable_start_prepare

static unsigned task_starts;
static unsigned thread_starts;
static unsigned releases;
static bool task_rejected;
static bool thread_rejected;
static bool valid = true;

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    const bool reject = ++task_starts == 2U;
    if (reject)
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    const RRuntimeTaskPrepareResult result =
        r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    if (reject) {
        task_rejected = result.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED &&
                        r_runtime_allocator_attempt_count(allocator) == UINT64_C(1);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    }
    return result;
}

RStdThreadSpawnResult r_test_spawn(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload) {
    const bool reject = ++thread_starts == 1U;
    if (reject)
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    const RStdThreadSpawnResult result = r_library_internal_thread_spawn_checked(
        allocator, payload_type, completion_type, entry, staged_payload);
    if (reject) {
        thread_rejected =
            !result.is_ok && r_runtime_allocator_attempt_count(allocator) == UINT64_C(1);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    }
    return result;
}

void r_test_release(RRuntimeOwn *owner) {
    if (owner->allocation == NULL || *(int32_t *)owner->allocation != INT32_C(9))
        valid = false;
    ++releases;
    r_runtime_own_release(owner);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    return status == 0 && valid && task_starts == 3U && thread_starts == 2U && task_rejected &&
                   thread_rejected && releases == 2U
               ? 0
               : 1;
}

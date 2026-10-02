#include "r_runtime_0_1.h"
#include "r_runtime_task.h"

#include <stdbool.h>
#include <stdint.h>

/* R-TYPE-0054 (L28): the first start through an async function value after main's own start is
   rejected; the program recovers and calls through the same values again. */
static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RRuntimeTaskStepFn step);
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_resumable_start_prepare

static unsigned task_starts;
static bool task_rejected;

static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
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

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    /* main, the rejected start and seven accepted starts through function values. */
    return status == 0 && task_rejected && task_starts == 9U ? 0 : 1;
}

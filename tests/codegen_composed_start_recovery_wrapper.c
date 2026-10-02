#include "r_runtime_0_1.h"
#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stdatomic.h>
#include <stdint.h>

static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RRuntimeTaskStepFn step);
static void r_test_release(RRuntimeOwn *owner);

#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_own_release r_test_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_own_release
#undef r_runtime_task_resumable_start_prepare

static _Atomic unsigned starts;
static _Atomic unsigned rejections;
static _Atomic unsigned releases;
static _Atomic _Bool valid = 1;

static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RRuntimeTaskStepFn step) {
    const unsigned attempt = atomic_fetch_add(&starts, 1U) + 1U;
    const _Bool reject = attempt == 2U || attempt == 4U;
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    if (reject)
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    const RRuntimeTaskPrepareResult result =
        r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    if (reject) {
        if (result.status != R_RUNTIME_TASK_START_ALLOCATION_FAILED ||
            r_runtime_allocator_attempt_count(allocator) != UINT64_C(1))
            atomic_store(&valid, 0);
        atomic_fetch_add(&rejections, 1U);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    }
    return result;
}

static void r_test_release(RRuntimeOwn *owner) {
    if (owner->allocation == NULL || *(const int32_t *)owner->allocation != INT32_C(9))
        atomic_store(&valid, 0);
    atomic_fetch_add(&releases, 1U);
    r_runtime_own_release(owner);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    return status == 0 && atomic_load(&valid) && atomic_load(&starts) == 5U &&
                   atomic_load(&rejections) == 2U && atomic_load(&releases) == 4U
               ? 0
               : 1;
}

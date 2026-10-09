#include "r_runtime_task.h"
#include <stdatomic.h>
static _Atomic unsigned starts;
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
/* P4.4: an awaited call that runs directly takes the place of a start. */
_Bool r_test_direct_begin(size_t stack_bytes);
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_task_direct_begin r_test_direct_begin
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_task_direct_begin
#undef r_runtime_task_resumable_start_prepare
_Bool r_test_direct_begin(size_t stack_bytes) {
    const _Bool direct = r_runtime_task_direct_begin(stack_bytes);
    if (direct) {
        atomic_fetch_add(&starts, 1U);
    }
    return direct;
}
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    atomic_fetch_add(&starts, 1U);
    return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
}
int main(int argc, char *argv[]) {
    int result = r_generated_main(argc, argv);
    return result == 0 && atomic_load(&starts) == 6U ? 0 : 1;
}

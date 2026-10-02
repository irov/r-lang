#include "r_runtime_task.h"
#include <stdatomic.h>
static _Atomic unsigned starts;
static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RRuntimeTaskStepFn step);
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_resumable_start_prepare
static RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RRuntimeTaskStepFn step) {
    atomic_fetch_add(&starts, 1U);
    return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
}
int main(int argc, char *argv[]) {
    int result = r_generated_main(argc, argv);
    return result == 0 && atomic_load(&starts) == 6U ? 0 : 1;
}

#include "r_runtime_0_1.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_std_thread.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic unsigned diagnostic_count;
static _Atomic unsigned acknowledged;
static _Atomic unsigned payload_drops;
static _Atomic unsigned thread_runs;
static unsigned static_drops;

static void require(_Bool condition) {
    if (!condition)
        abort();
}

static void pending_drop(void *value) {
    require(*(const int *)value == 42);
    require(atomic_load(&diagnostic_count) == 1U);
    (void)atomic_fetch_add(&payload_drops, 1U);
}

static void pending_start(RRuntimeTaskExternalExecution *execution, void *payload, void *result) {
    (void)payload;
    (void)result;
    r_runtime_task_external_start_ready(execution);
}

static void pending_cancel(RRuntimeTaskExternalExecution *execution, void *payload) {
    (void)payload;
    require(atomic_load(&diagnostic_count) == 1U);
    require(r_runtime_task_external_cancel_requested(execution));
    (void)atomic_fetch_add(&acknowledged, 1U);
    r_runtime_task_external_acknowledge(execution);
}

static void start_pending(void) {
    const RRuntimeTypeInfo payload = {sizeof(int), _Alignof(int), NULL, pending_drop};
    const RRuntimeTypeInfo result = {0U, 1U, NULL, NULL};
    int value = 42;
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_external_start_prepare(payload, result, pending_start, pending_cancel);
    require(prepared.status == R_RUNTIME_TASK_START_OK);
    RRuntimeTaskStartResult started = r_runtime_task_start_commit(&prepared.transaction, &value);
    require(started.status == R_RUNTIME_TASK_START_OK);
    r_runtime_task_detach(&started.task);
}

static RRuntimeStartResult test_start(int argc, char *argv[]) {
    RRuntimeStartResult result = r_runtime_hosted_start(argc, argv);
    if (result.started)
        start_pending();
    return result;
}

static void test_report(const char *domain,
                        size_t domain_length,
                        const uint8_t *name,
                        size_t name_length,
                        uint32_t code,
                        int64_t native_code) {
    require(domain_length == 2U && memcmp(domain, "io", 2U) == 0);
    require(name_length == 17U && memcmp(name, "permission_denied", 17U) == 0);
    require(code == 2U && native_code == INT64_MIN && static_drops == 0U);
    require(atomic_load(&acknowledged) == 0U);
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_runtime_report_main_error(domain, domain_length, name, name_length, code, native_code);
    require(r_runtime_allocator_attempt_count(allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    atomic_store(&diagnostic_count, 1U);
}

static void cleanup_thread(void *payload, void *result) {
    (void)payload;
    (void)result;
    require(atomic_load(&diagnostic_count) == 1U);
    (void)atomic_fetch_add(&thread_runs, 1U);
}

static void test_destroy(RRuntimeArray *array) {
    require(atomic_load(&diagnostic_count) == 1U);
    require(atomic_load(&acknowledged) == static_drops + 1U);
    require(atomic_load(&payload_drops) == static_drops + 1U);
    require(array->length == 1U);
    require(*(const uint8_t *)array->data == (static_drops == 0U ? 2U : 1U));
    r_runtime_array_destroy(array);
    static_drops += 1U;
    /* Work created by one object must drain before the next object is destroyed. */
    start_pending();
    const RRuntimeTypeInfo empty = {0U, 1U, NULL, NULL};
    RStdThreadSpawnResult thread =
        r_std_thread_spawn(r_runtime_hosted_allocator(), empty, empty, cleanup_thread, NULL);
    require(thread.is_ok);
    r_std_thread_detach(&thread.value);
}

#define r_runtime_hosted_start test_start
#define r_runtime_report_main_error test_report
#define r_runtime_array_destroy test_destroy
#define main r_generated_main
int r_generated_main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_array_destroy
#undef r_runtime_report_main_error
#undef r_runtime_hosted_start

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    require(status == 113 && static_drops == 2U);
    require(atomic_load(&diagnostic_count) == 1U);
    require(atomic_load(&acknowledged) == 3U && atomic_load(&payload_drops) == 3U);
    require(atomic_load(&thread_runs) == 2U);
    (void)puts("cleanup complete");
    return status;
}

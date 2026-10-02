#include "r_std_time.h"

#include "r_runtime_allocator.h"
#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
#include "r_runtime_darwin_timer.h"
#endif
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "library time async test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static RStdTimeTaskResult await_sleep(RStdTimeSleepStartResult started, const char *message) {
    RStdTimeTaskResult result = {0};

    require(started.is_ok && started.task != NULL, message);
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK, message);
    require(started.task == NULL, message);
    return result;
}

static RStdTimeDuration elapsed_since(RStdTimeInstant start, const char *message) {
    RStdTimeInstantResult finish = r_std_time_monotonic_now();
    RStdTimeDurationTimeResult elapsed;

    require(finish.is_ok, message);
    elapsed = r_std_time_instant_duration(finish.value, start);
    require(elapsed.is_ok, message);
    return elapsed.value;
}

static void quick_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    *(int *)result = 73;
}

static RRuntimeTask *start_quick_task(void) {
    const RRuntimeTypeInfo void_type = {0U, 1U, NULL, NULL};
    const RRuntimeTypeInfo result_type = {sizeof(int), _Alignof(int), NULL, NULL};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_start_prepare(void_type, result_type, quick_body);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, NULL);
    return started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
}

static void test_completion_and_deadlines(void) {
    RStdTimeInstantResult before;
    RStdTimeInstantResult deadline;
    RStdTimeTaskResult result;
    RStdTimeDuration elapsed;

    result = await_sleep(r_std_time_sleep_for((RStdTimeDuration){0, 0U}), "zero sleep");
    require(result.r_tag == UINT32_C(0), "zero sleep result");
    result = await_sleep(r_std_time_sleep_for((RStdTimeDuration){-INT64_C(1), UINT32_C(999999999)}),
                         "negative sleep");
    require(result.r_tag == UINT32_C(0), "negative sleep result");

    before = r_std_time_monotonic_now();
    require(before.is_ok, "read before positive sleep");
    result = await_sleep(r_std_time_sleep_for((RStdTimeDuration){0, UINT32_C(20000000)}),
                         "positive sleep");
    require(result.r_tag == UINT32_C(0), "positive sleep result");
    elapsed = elapsed_since(before.value, "measure positive sleep");
    require(r_std_time_duration_compare(elapsed, (RStdTimeDuration){0, UINT32_C(20000000)}) >= 0,
            "positive sleep completed before its deadline");

    before = r_std_time_monotonic_now();
    require(before.is_ok, "read before sleep until");
    deadline = r_std_time_instant_add(before.value, (RStdTimeDuration){0, UINT32_C(15000000)});
    require(deadline.is_ok, "construct sleep-until deadline");
    result = await_sleep(r_std_time_sleep_until(deadline.value), "future sleep until");
    require(result.r_tag == UINT32_C(0), "future sleep-until result");
    elapsed = elapsed_since(before.value, "measure sleep until");
    require(r_std_time_duration_compare(elapsed, (RStdTimeDuration){0, UINT32_C(15000000)}) >= 0,
            "sleep_until completed before its deadline");

    result = await_sleep(r_std_time_sleep_until((RStdTimeInstant){0, 0U}), "past sleep until");
    require(result.r_tag == UINT32_C(0), "past sleep-until result");
}

static void test_error_outcomes(void) {
    RStdTimeTaskResult result;

    result =
        await_sleep(r_std_time_sleep_for((RStdTimeDuration){0, R_STD_TIME_NANOSECONDS_PER_SECOND}),
                    "invalid duration task");
    require(result.r_tag == UINT32_C(1) &&
                result.r_payload.r_error_00000001.code == R_STD_TIME_ERROR_INVALID_VALUE &&
                result.r_payload.r_error_00000001.native_code == 0,
            "invalid duration classification");

    result = await_sleep(r_std_time_sleep_for((RStdTimeDuration){INT64_MAX, UINT32_C(999999999)}),
                         "overflow duration task");
    require(result.r_tag == UINT32_C(1) &&
                result.r_payload.r_error_00000001.code == R_STD_TIME_ERROR_OVERFLOW &&
                result.r_payload.r_error_00000001.native_code == 0,
            "instant horizon overflow classification");

    result = await_sleep(r_std_time_sleep_for((RStdTimeDuration){INT64_C(10000000000), 0U}),
                         "native horizon task");
    require(result.r_tag == UINT32_C(1) &&
                result.r_payload.r_error_00000001.code == R_STD_TIME_ERROR_UNAVAILABLE &&
                result.r_payload.r_error_00000001.native_code == 0,
            "Dispatch timer horizon classification");

    result =
        await_sleep(r_std_time_sleep_until((RStdTimeInstant){0, R_STD_TIME_NANOSECONDS_PER_SECOND}),
                    "invalid instant task");
    require(result.r_tag == UINT32_C(1) &&
                result.r_payload.r_error_00000001.code == R_STD_TIME_ERROR_INVALID_VALUE,
            "invalid instant classification");
}

static void test_timer_does_not_block_executor(void) {
    RStdTimeSleepStartResult sleeping = r_std_time_sleep_for((RStdTimeDuration){INT64_C(5), 0U});
    RRuntimeTask *quick;
    int value = 0;

    require(sleeping.is_ok, "long timer start");
    quick = start_quick_task();
    require(quick != NULL, "quick computation start while timer pending");
    require(r_runtime_task_await(&quick, &value) == R_RUNTIME_TASK_AWAIT_OK,
            "timer occupied executor worker");
    require(value == 73, "quick computation result");
    r_runtime_task_cancel(&sleeping.task);
}

static void test_cancel_completion_races(void) {
    size_t index;

    for (index = 0U; index < 200U; ++index) {
        RStdTimeSleepStartResult started =
            r_std_time_sleep_for((RStdTimeDuration){0, (uint32_t)(index & 1U)});

        require(started.is_ok, "race timer start");
        r_runtime_task_cancel(&started.task);
        require(started.task == NULL, "race timer cancel consumption");
    }
}

#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
static void test_timer_cancel_acknowledges_once(void) {
    RStdTimeSleepStartResult started;
    RStdTimeTaskResult result = {0};

    r_runtime_darwin_timer_testing_pause_next_cancel_handler();
    started = r_std_time_sleep_for((RStdTimeDuration){0, 0U});
    require(started.is_ok && started.task != NULL, "deterministic timer race start");
    r_runtime_darwin_timer_testing_wait_for_cancel_handler();
    r_runtime_darwin_timer_testing_cancel_paused_timer();
    r_runtime_darwin_timer_testing_release_cancel_handler();
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "deterministic timer race await");
    require(result.r_tag == UINT32_C(0) && started.task == NULL, "deterministic timer race result");
}
#endif

static void test_start_allocation_failure(RRuntimeAllocator *allocator) {
    RStdTimeDuration original = {INT64_C(1), UINT32_C(2)};
    RStdTimeSleepStartResult started;

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_time_sleep_for(original);
    require(!started.is_ok && started.task == NULL &&
                started.error == R_STD_ASYNC_START_ALLOCATION_FAILED,
            "timer frame allocation failure");
    require(original.seconds == INT64_C(1) && original.nanoseconds == UINT32_C(2),
            "start failure changed the named operand");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
}

static void test_root_drain_is_native_acknowledged(void) {
    RStdTimeInstantResult before = r_std_time_monotonic_now();
    RStdTimeSleepStartResult detached = r_std_time_sleep_for((RStdTimeDuration){INT64_C(30), 0U});
    RStdTimeDuration elapsed;

    require(before.is_ok && detached.is_ok, "detached timer setup");
    r_runtime_task_detach(&detached.task);
    require(r_runtime_executor_lifecycle_stop(), "timer root drain");
    elapsed = elapsed_since(before.value, "measure timer root drain");
    require(r_std_time_duration_compare(elapsed, (RStdTimeDuration){INT64_C(1), 0U}) < 0,
            "root drain waited for the timer deadline instead of cancellation acknowledgement");
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    require(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "executor start");
    test_completion_and_deadlines();
    test_error_outcomes();
#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
    test_timer_cancel_acknowledges_once();
#endif
    test_timer_does_not_block_executor();
    test_cancel_completion_races();
    test_start_allocation_failure(&allocator);
    test_root_drain_is_native_acknowledged();
    (void)fprintf(stdout, "library_time_async_tests: ok\n");
    return EXIT_SUCCESS;
}

#include "r_std_async.h"

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* R-SLIB-ASYNC-0017 (L31): the blocking call pool runs every call exactly once in submission
 * order on at most four threads; a call that no thread has taken is removed by cancellation
 * together with its arguments, a taken call runs to its return and its result is then destroyed,
 * and a full queue or a stopped pool fails the start without consuming the staged arguments. */

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

enum {
    R_TEST_GATES = 8
};

static _Atomic unsigned argument_drops;
static _Atomic unsigned result_drops;
static _Atomic unsigned entered;
static _Atomic unsigned order_cursor;
static _Atomic unsigned order[R_TEST_GATES];
static atomic_bool gates[R_TEST_GATES];

/* The argument payload of one call: the gate it waits on and a value it returns. */
typedef struct RTestArguments {
    uint32_t gate;
    int32_t value;
    _Bool initialized;
} RTestArguments;

typedef struct RTestResult {
    int32_t value;
} RTestResult;

static void arguments_move(void *destination, void *source) {
    RTestArguments *target = destination;
    RTestArguments *staged = source;

    *target = *staged;
    staged->initialized = 0;
}

static void arguments_drop(void *value) {
    RTestArguments *arguments = value;

    if (arguments->initialized) {
        arguments->initialized = 0;
        atomic_fetch_add(&argument_drops, 1U);
    }
}

static void result_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTestResult));
}

static void result_drop(void *value) {
    (void)value;
    atomic_fetch_add(&result_drops, 1U);
}

static const RRuntimeTypeInfo arguments_type = {
    sizeof(RTestArguments), _Alignof(RTestArguments), arguments_move, arguments_drop};
static const RRuntimeTypeInfo result_type = {
    sizeof(RTestResult), _Alignof(RTestResult), result_move, result_drop};

/* Moves its arguments out, as a generated entry does, then blocks on its gate. */
static void entry(void *payload, void *result) {
    RTestArguments *arguments = payload;
    RTestArguments taken = *arguments;
    const unsigned position = atomic_fetch_add(&order_cursor, 1U);

    arguments->initialized = 0;
    if (position < R_TEST_GATES) {
        atomic_store(&order[position], taken.gate);
    }
    atomic_fetch_add(&entered, 1U);
    while (!atomic_load(&gates[taken.gate])) {
        (void)sched_yield();
    }
    ((RTestResult *)result)->value = taken.value;
    atomic_fetch_add(&argument_drops, 1U);
}

static _Bool eventually(_Atomic unsigned *counter, unsigned expected) {
    const clock_t start = clock();

    while (atomic_load(counter) != expected) {
        if ((clock() - start) > 2 * CLOCKS_PER_SEC) {
            return 0;
        }
        (void)sched_yield();
    }
    return 1;
}

static _Bool stays(_Atomic unsigned *counter, unsigned expected) {
    for (unsigned attempt = 0U; attempt < 20000U; ++attempt) {
        if (atomic_load(counter) != expected) {
            return 0;
        }
        (void)sched_yield();
    }
    return 1;
}

static void reset(void) {
    atomic_store(&argument_drops, 0U);
    atomic_store(&result_drops, 0U);
    atomic_store(&entered, 0U);
    atomic_store(&order_cursor, 0U);
    for (unsigned index = 0U; index < R_TEST_GATES; ++index) {
        atomic_store(&gates[index], 0);
        atomic_store(&order[index], UINT32_MAX);
    }
}

static RStdAsyncStartResult start(uint32_t gate, int32_t value) {
    RTestArguments staged = {gate, value, 1};

    return r_std_async_blocking(arguments_type, result_type, entry, &staged);
}

/* Starts one call per pool thread, each after the previous one entered, so the queue stays
 * empty. */
static int occupy(RStdAsyncStartResult *calls) {
    for (uint32_t index = 0U; index < 4U; ++index) {
        calls[index] = start(index, (int32_t)index);
        R_TEST_CHECK(calls[index].is_ok);
        R_TEST_CHECK(eventually(&entered, index + 1U));
    }
    return 0;
}

/* Four calls occupy every pool thread; the fifth waits in the queue and starts only when one of
 * them returns; queued calls start in submission order. */
static int test_exhaustion_and_order(void) {
    RStdAsyncStartResult calls[7];
    RTestResult result;

    reset();
    R_TEST_CHECK(occupy(calls) == 0);
    for (uint32_t index = 4U; index < 7U; ++index) {
        calls[index] = start(index, (int32_t)index);
        R_TEST_CHECK(calls[index].is_ok);
    }
    R_TEST_CHECK(stays(&entered, 4U));
    for (uint32_t index = 0U; index < 3U; ++index) {
        atomic_store(&gates[index], 1);
        R_TEST_CHECK(r_runtime_task_await(&calls[index].task, &result) == R_RUNTIME_TASK_AWAIT_OK);
        R_TEST_CHECK(result.value == (int32_t)index);
        R_TEST_CHECK(eventually(&entered, 5U + index));
        R_TEST_CHECK(atomic_load(&order[4U + index]) == 4U + index);
    }
    for (uint32_t index = 3U; index < 7U; ++index) {
        atomic_store(&gates[index], 1);
        R_TEST_CHECK(r_runtime_task_await(&calls[index].task, &result) == R_RUNTIME_TASK_AWAIT_OK);
        R_TEST_CHECK(result.value == (int32_t)index);
    }
    R_TEST_CHECK(atomic_load(&argument_drops) == 7U);
    R_TEST_CHECK(atomic_load(&result_drops) == 0U);
    return 0;
}

/* A queued call is removed with its arguments and never enters; a taken call returns first and
 * its result is then destroyed. */
static int test_cancellation(void) {
    RStdAsyncStartResult calls[6];
    RTestResult result;

    reset();
    R_TEST_CHECK(occupy(calls) == 0);
    calls[4] = start(4U, 4);
    calls[5] = start(5U, 5);
    R_TEST_CHECK(calls[4].is_ok && calls[5].is_ok);
    r_std_async_cancel(&calls[4].task);
    R_TEST_CHECK(calls[4].task == NULL);
    R_TEST_CHECK(eventually(&argument_drops, 1U));
    r_std_async_cancel(&calls[0].task);
    R_TEST_CHECK(stays(&result_drops, 0U));
    atomic_store(&gates[0], 1);
    R_TEST_CHECK(eventually(&result_drops, 1U));
    R_TEST_CHECK(eventually(&entered, 5U));
    R_TEST_CHECK(atomic_load(&order[4]) == 5U);
    for (uint32_t index = 1U; index < 4U; ++index) {
        atomic_store(&gates[index], 1);
        R_TEST_CHECK(r_runtime_task_await(&calls[index].task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    }
    atomic_store(&gates[5], 1);
    R_TEST_CHECK(r_runtime_task_await(&calls[5].task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(result.value == 5);
    R_TEST_CHECK(atomic_load(&entered) == 5U);
    R_TEST_CHECK(atomic_load(&argument_drops) == 6U);
    R_TEST_CHECK(atomic_load(&result_drops) == 1U);
    return 0;
}

/* With every thread busy and the queue at its capacity of two, a start fails with
 * allocation_failed and leaves the staged arguments with the caller. */
static int test_queue_full(void) {
    RStdAsyncStartResult calls[6];
    RTestArguments staged = {7U, 7, 1};
    RStdAsyncStartResult refused;
    RTestResult result;

    reset();
    R_TEST_CHECK(occupy(calls) == 0);
    calls[4] = start(4U, 4);
    calls[5] = start(5U, 5);
    R_TEST_CHECK(calls[4].is_ok && calls[5].is_ok);
    refused = r_std_async_blocking(arguments_type, result_type, entry, &staged);
    R_TEST_CHECK(!refused.is_ok && refused.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(staged.initialized);
    for (uint32_t index = 0U; index < 6U; ++index) {
        atomic_store(&gates[index], 1);
        R_TEST_CHECK(r_runtime_task_await(&calls[index].task, &result) == R_RUNTIME_TASK_AWAIT_OK);
        R_TEST_CHECK(result.value == (int32_t)index);
    }
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RTestArguments staged = {0U, 0, 1};
    RStdAsyncStartResult stopped;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(r_runtime_blocking_start(2U));
    R_TEST_CHECK(!r_runtime_blocking_start(2U));
    R_TEST_CHECK(test_queue_full() == 0);
    r_runtime_blocking_stop();
    R_TEST_CHECK(r_runtime_blocking_start(64U));
    R_TEST_CHECK(test_exhaustion_and_order() == 0);
    R_TEST_CHECK(test_cancellation() == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_blocking_stop();
    stopped = r_std_async_blocking(arguments_type, result_type, entry, &staged);
    R_TEST_CHECK(!stopped.is_ok && stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    R_TEST_CHECK(staged.initialized);
    return 0;
}

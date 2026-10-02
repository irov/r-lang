#include "r_std_async.h"

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

typedef struct RLibraryAsyncTestContext {
    _Atomic _Bool entered;
    _Atomic _Bool release;
    _Atomic _Bool saw_cancel;
    _Atomic _Bool body_returned;
} RLibraryAsyncTestContext;

typedef struct RLibraryAsyncTestPayload {
    RLibraryAsyncTestContext *context;
} RLibraryAsyncTestPayload;

static void payload_move(void *destination, void *source) {
    RLibraryAsyncTestPayload *destination_value = destination;
    RLibraryAsyncTestPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
}

static void test_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result) {
    RLibraryAsyncTestPayload *payload = payload_pointer;

    (void)result;
    atomic_store_explicit(&payload->context->entered, 1, memory_order_release);
    while (!atomic_load_explicit(&payload->context->release, memory_order_acquire) &&
           !r_runtime_task_execution_cancel_requested(execution)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&payload->context->saw_cancel,
                          r_runtime_task_execution_cancel_requested(execution),
                          memory_order_release);
    atomic_store_explicit(&payload->context->body_returned, 1, memory_order_release);
}

static RRuntimeTask *start_test_task(RLibraryAsyncTestContext *context) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryAsyncTestPayload),
        _Alignof(RLibraryAsyncTestPayload),
        payload_move,
        NULL,
    };
    const RRuntimeTypeInfo result_type = {0U, 1U, NULL, NULL};
    RLibraryAsyncTestPayload payload = {context};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_start_prepare(payload_type, result_type, test_body);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    return started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
}

static void context_initialize(RLibraryAsyncTestContext *context) {
    atomic_init(&context->entered, 0);
    atomic_init(&context->release, 0);
    atomic_init(&context->saw_cancel, 0);
    atomic_init(&context->body_returned, 0);
}

int main(void) {
    RRuntimeAllocator allocator;
    RLibraryAsyncTestContext context;
    RRuntimeTask *task;

    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_executor_lifecycle_start(&allocator) != R_RUNTIME_EXECUTOR_START_OK) {
        (void)fputs("async library test: executor start failed\n", stderr);
        return 1;
    }
    context_initialize(&context);
    task = start_test_task(&context);
    if (task == NULL) {
        (void)fputs("async library test: cancel task start failed\n", stderr);
        return 1;
    }
    while (!atomic_load_explicit(&context.entered, memory_order_acquire)) {
        (void)sched_yield();
    }
    r_std_async_cancel(&task);
    if (task != NULL) {
        (void)fputs("async library test: cancel did not consume task\n", stderr);
        return 1;
    }
    if (!r_runtime_executor_lifecycle_stop()) {
        (void)fputs("async library test: cancel drain did not stop executor\n", stderr);
        return 1;
    }
    if (!atomic_load_explicit(&context.saw_cancel, memory_order_acquire)) {
        (void)fputs("async library test: cancel request was not observed\n", stderr);
        return 1;
    }

    if (r_runtime_executor_lifecycle_start(&allocator) != R_RUNTIME_EXECUTOR_START_OK) {
        (void)fputs("async library test: executor restart failed\n", stderr);
        return 1;
    }
    context_initialize(&context);
    task = start_test_task(&context);
    if (task == NULL) {
        (void)fputs("async library test: detach task start failed\n", stderr);
        return 1;
    }
    while (!atomic_load_explicit(&context.entered, memory_order_acquire)) {
        (void)sched_yield();
    }
    r_std_async_detach(&task);
    atomic_store_explicit(&context.release, 1, memory_order_release);
    while (!atomic_load_explicit(&context.body_returned, memory_order_acquire)) {
        (void)sched_yield();
    }
    if (task != NULL) {
        (void)fputs("async library test: detach did not consume task\n", stderr);
        return 1;
    }
    if (atomic_load_explicit(&context.saw_cancel, memory_order_acquire)) {
        (void)fputs("async library test: detach requested cancellation\n", stderr);
        return 1;
    }
    if (!r_runtime_executor_lifecycle_stop()) {
        (void)fputs("async library test: detach drain did not stop executor\n", stderr);
        return 1;
    }
    (void)puts("library_async_ok");
    return 0;
}

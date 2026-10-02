#include "r_std_thread.h"

#include "r_library_thread_internal.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(condition)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "thread test failed at line %d: %s\n", __LINE__, #condition);    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestOwnedPayload {
    int value;
    _Bool active;
} RTestOwnedPayload;

typedef struct RTestOwnedResult {
    int value;
    _Bool active;
} RTestOwnedResult;

typedef struct RTestCheckedThreadCarrier {
    uint32_t tag;
    union {
        RTestOwnedResult value;
        RTestOwnedResult error;
    } payload;
} RTestCheckedThreadCarrier;

typedef struct RTestParkContext {
    _Atomic _Bool ready;
    _Atomic _Bool enter_park;
    _Atomic _Bool first_return;
    _Atomic _Bool second_return;
    RStdThread identity;
} RTestParkContext;

typedef struct RTestCloneContext {
    _Atomic _Bool ready;
    _Atomic _Bool finish;
    RStdThread identity;
} RTestCloneContext;

typedef struct RTestCloneWorker {
    const RStdThread *identity;
    _Bool passed;
} RTestCloneWorker;

static _Atomic unsigned int r_test_payload_moves;
static _Atomic unsigned int r_test_payload_drops;
static _Atomic unsigned int r_test_result_drops;

static RRuntimeTypeInfo r_test_void_type(void) {
    RRuntimeTypeInfo type = {0U, 1U, NULL, NULL};
    return type;
}

static void r_test_payload_move(void *destination, void *source) {
    RTestOwnedPayload *target = destination;
    RTestOwnedPayload *origin = source;
    *target = *origin;
    origin->active = 0;
    (void)atomic_fetch_add_explicit(&r_test_payload_moves, 1U, memory_order_relaxed);
}

static void r_test_payload_drop(void *value) {
    RTestOwnedPayload *payload = value;
    if (payload->active) {
        payload->active = 0;
        (void)atomic_fetch_add_explicit(&r_test_payload_drops, 1U, memory_order_relaxed);
    }
}

static void r_test_result_drop(void *value) {
    RTestOwnedResult *result = value;
    if (result->active) {
        result->active = 0;
        (void)atomic_fetch_add_explicit(&r_test_result_drops, 1U, memory_order_relaxed);
    }
}

static void r_test_checked_carrier_move(void *destination, void *source) {
    RTestCheckedThreadCarrier *target = destination;
    RTestCheckedThreadCarrier *origin = source;
    const uint32_t tag = origin->tag;

    if (tag == UINT32_C(0)) {
        target->payload.value = origin->payload.value;
        origin->payload.value.active = 0;
    } else {
        target->payload.error = origin->payload.error;
        origin->payload.error.active = 0;
    }
    target->tag = tag;
}

static void r_test_checked_carrier_drop(void *value) {
    RTestCheckedThreadCarrier *carrier = value;

    if (carrier->tag == UINT32_C(0)) {
        r_test_result_drop(&carrier->payload.value);
    } else {
        r_test_result_drop(&carrier->payload.error);
    }
}

static RRuntimeTypeInfo r_test_payload_type(void) {
    RRuntimeTypeInfo type = {sizeof(RTestOwnedPayload),
                             _Alignof(RTestOwnedPayload),
                             r_test_payload_move,
                             r_test_payload_drop};
    return type;
}

static RRuntimeTypeInfo r_test_int_type(void) {
    RRuntimeTypeInfo type = {sizeof(int), _Alignof(int), NULL, NULL};
    return type;
}

static RRuntimeTypeInfo r_test_owned_result_type(void) {
    RRuntimeTypeInfo type = {
        sizeof(RTestOwnedResult), _Alignof(RTestOwnedResult), NULL, r_test_result_drop};
    return type;
}

static RRuntimeTypeInfo r_test_checked_carrier_type(void) {
    RRuntimeTypeInfo type = {sizeof(RTestCheckedThreadCarrier),
                             _Alignof(RTestCheckedThreadCarrier),
                             r_test_checked_carrier_move,
                             r_test_checked_carrier_drop};
    return type;
}

static RStdThreadCompletionTypeInfo r_test_checked_completion_type(void) {
    const RStdThreadCompletionTypeInfo type = {
        .storage_type = r_test_checked_carrier_type(),
        .tag_offset = offsetof(RTestCheckedThreadCarrier, tag),
        .payload_offset = offsetof(RTestCheckedThreadCarrier, payload),
        .error_count = UINT32_C(1),
    };
    return type;
}

static RRuntimeTypeInfo r_test_pointer_type(void) {
    RRuntimeTypeInfo type = {sizeof(void *), _Alignof(void *), NULL, NULL};
    return type;
}

static void r_test_return_int(void *payload, void *result) {
    const RTestOwnedPayload *value = payload;
    *(int *)result = value->value + 1;
}

static void r_test_return_owned(void *payload, void *result) {
    RTestOwnedResult *returned = result;
    (void)payload;
    returned->value = 73;
    returned->active = 1;
}

static void r_test_return_checked_error(void *payload, void *result) {
    RTestCheckedThreadCarrier *carrier = result;

    (void)payload;
    carrier->payload.error.value = 91;
    carrier->payload.error.active = 1;
    carrier->tag = UINT32_C(1);
}

static void r_test_return_checked_value(void *payload, void *result) {
    RTestCheckedThreadCarrier *carrier = result;

    (void)payload;
    carrier->payload.value.value = 82;
    carrier->payload.value.active = 1;
    carrier->tag = UINT32_C(0);
}

static void r_test_park_entry(void *payload, void *result) {
    RTestParkContext *context = *(RTestParkContext **)payload;
    (void)result;
    context->identity = r_std_thread_current();
    atomic_store_explicit(&context->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&context->enter_park, memory_order_acquire)) {
        r_std_thread_yield_now();
    }
    r_std_thread_park();
    atomic_store_explicit(&context->first_return, 1, memory_order_release);
    r_std_thread_park();
    atomic_store_explicit(&context->second_return, 1, memory_order_release);
}

static void r_test_clone_entry(void *payload, void *result) {
    RTestCloneContext *context = *(RTestCloneContext **)payload;
    (void)result;
    context->identity = r_std_thread_current();
    atomic_store_explicit(&context->ready, 1, memory_order_release);
    while (!atomic_load_explicit(&context->finish, memory_order_acquire)) {
        r_std_thread_yield_now();
    }
}

static void *r_test_clone_worker(void *context_value) {
    RTestCloneWorker *context = context_value;
    size_t iteration;
    context->passed = 1;
    for (iteration = 0U; iteration < 1000U; ++iteration) {
        RStdThread clone = r_std_thread_clone_thread(context->identity);
        if (clone.descriptor == NULL) {
            context->passed = 0;
            return NULL;
        }
        r_std_thread_unpark(&clone);
        r_std_thread_thread_destroy(&clone);
    }
    return NULL;
}

static _Bool r_test_wait_bool(const _Atomic _Bool *value) {
    size_t iteration;
    for (iteration = 0U; iteration < 5000U; ++iteration) {
        if (atomic_load_explicit(value, memory_order_acquire)) {
            return 1;
        }
        r_std_thread_sleep_nanoseconds(UINT64_C(1000000));
    }
    return 0;
}

static int r_test_start_failures(void) {
    uint64_t attempt;

    for (attempt = UINT64_C(1); attempt <= UINT64_C(3); ++attempt) {
        RRuntimeAllocator allocator;
        RTestOwnedPayload payload = {41, 1};
        RStdThreadSpawnResult result;
        atomic_store_explicit(&r_test_payload_moves, 0U, memory_order_relaxed);
        atomic_store_explicit(&r_test_payload_drops, 0U, memory_order_relaxed);
        r_runtime_allocator_initialize(&allocator);
        r_runtime_allocator_set_failure(&allocator, attempt);
        result = r_std_thread_spawn(
            &allocator, r_test_payload_type(), r_test_int_type(), r_test_return_int, &payload);
        R_TEST_CHECK(!result.is_ok);
        R_TEST_CHECK(result.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED);
        R_TEST_CHECK(result.value.descriptor == NULL);
        R_TEST_CHECK(payload.active && (payload.value == 41));
        R_TEST_CHECK(atomic_load_explicit(&r_test_payload_moves, memory_order_relaxed) == 0U);
        R_TEST_CHECK(atomic_load_explicit(&r_test_payload_drops, memory_order_relaxed) == 0U);
    }

    {
        RRuntimeAllocator allocator;
        RTestOwnedPayload payload = {41, 1};
        RStdThreadSpawnResult result;
        r_runtime_allocator_initialize(&allocator);
        r_library_internal_thread_testing_fail_create(EAGAIN);
        result = r_std_thread_spawn(
            &allocator, r_test_payload_type(), r_test_int_type(), r_test_return_int, &payload);
        R_TEST_CHECK(!result.is_ok && (result.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED));
        r_library_internal_thread_testing_fail_create(EPERM);
        result = r_std_thread_spawn(
            &allocator, r_test_payload_type(), r_test_int_type(), r_test_return_int, &payload);
        R_TEST_CHECK(!result.is_ok && (result.error == R_STD_THREAD_ERROR_PERMISSION_DENIED));
        r_library_internal_thread_testing_fail_create(EINVAL);
        result = r_std_thread_spawn(
            &allocator, r_test_payload_type(), r_test_int_type(), r_test_return_int, &payload);
        R_TEST_CHECK(!result.is_ok && (result.error == R_STD_THREAD_ERROR_UNAVAILABLE));
        R_TEST_CHECK(payload.active);
    }
    {
        RRuntimeAllocator allocator;
        RTestOwnedPayload payload = {41, 1};
        RStdThreadSpawnResult result;

        atomic_store_explicit(&r_test_payload_moves, 0U, memory_order_relaxed);
        r_runtime_allocator_initialize(&allocator);
        r_runtime_allocator_set_failure(&allocator, UINT64_C(3));
        result = r_library_internal_thread_spawn_checked(&allocator,
                                                         r_test_payload_type(),
                                                         r_test_checked_completion_type(),
                                                         r_test_return_checked_error,
                                                         &payload);
        R_TEST_CHECK(!result.is_ok);
        R_TEST_CHECK(result.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED);
        R_TEST_CHECK(result.value.descriptor == NULL);
        R_TEST_CHECK(payload.active && (payload.value == 41));
        R_TEST_CHECK(atomic_load_explicit(&r_test_payload_moves, memory_order_relaxed) == 0U);
    }
    return 0;
}

static int r_test_join_and_cleanup(void) {
    RRuntimeAllocator allocator;
    RTestOwnedPayload payload = {41, 1};
    RStdThreadSpawnResult spawned;
    RStdThreadJoinResult joined;
    RTestCheckedThreadCarrier checked = {0};
    int value = 0;

    atomic_store_explicit(&r_test_payload_moves, 0U, memory_order_relaxed);
    atomic_store_explicit(&r_test_payload_drops, 0U, memory_order_relaxed);
    r_runtime_allocator_initialize(&allocator);
    spawned = r_std_thread_spawn(
        &allocator, r_test_payload_type(), r_test_int_type(), r_test_return_int, &payload);
    R_TEST_CHECK(spawned.is_ok && !payload.active);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
    R_TEST_CHECK(joined.completion_type.error_count == UINT32_C(0));
    R_TEST_CHECK(joined.completion_type.storage_type.size == sizeof(int));
    R_TEST_CHECK(atomic_load_explicit(&r_test_payload_moves, memory_order_relaxed) == 1U);
    R_TEST_CHECK(atomic_load_explicit(&r_test_payload_drops, memory_order_relaxed) == 1U);
    r_library_internal_thread_join_result_move(&joined, &value);
    R_TEST_CHECK(value == 42);
    r_std_thread_join_result_destroy(&joined);

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    spawned = r_std_thread_spawn(
        &allocator, r_test_void_type(), r_test_owned_result_type(), r_test_return_owned, NULL);
    R_TEST_CHECK(spawned.is_ok);
    r_std_thread_detach(&spawned.value);
    while (atomic_load_explicit(&r_test_result_drops, memory_order_acquire) < 1U) {
        r_std_thread_sleep_nanoseconds(UINT64_C(1000000));
    }
    {
        RStdThreadScopedSpawnResult scoped = r_std_thread_spawn_scoped(
            &allocator, r_test_void_type(), r_test_owned_result_type(), r_test_return_owned, NULL);
        R_TEST_CHECK(scoped.is_ok);
        r_std_thread_scoped_join_handle_destroy(&scoped.value);
        R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_acquire) == 2U);
    }

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    spawned = r_library_internal_thread_spawn_checked(&allocator,
                                                      r_test_void_type(),
                                                      r_test_checked_completion_type(),
                                                      r_test_return_checked_error,
                                                      NULL);
    R_TEST_CHECK(spawned.is_ok);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
    R_TEST_CHECK(joined.completion_type.storage_type.size == sizeof(checked));
    R_TEST_CHECK(joined.completion_type.tag_offset == offsetof(RTestCheckedThreadCarrier, tag));
    R_TEST_CHECK(joined.completion_type.payload_offset ==
                 offsetof(RTestCheckedThreadCarrier, payload));
    R_TEST_CHECK(joined.completion_type.error_count == UINT32_C(1));
    r_library_internal_thread_join_result_move(&joined, &checked);
    R_TEST_CHECK((checked.tag == UINT32_C(1)) && (checked.payload.error.value == 91) &&
                 checked.payload.error.active);
    r_std_thread_join_result_destroy(&joined);
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 0U);
    r_test_checked_carrier_drop(&checked);
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 1U);

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    {
        RStdThreadScopedSpawnResult scoped =
            r_library_internal_thread_spawn_scoped_checked(&allocator,
                                                           r_test_void_type(),
                                                           r_test_checked_completion_type(),
                                                           r_test_return_checked_error,
                                                           NULL);
        R_TEST_CHECK(scoped.is_ok);
        joined = r_std_thread_join(&scoped.value);
        R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
        r_library_internal_thread_join_result_move(&joined, &checked);
        R_TEST_CHECK((checked.tag == UINT32_C(1)) && checked.payload.error.active);
        r_std_thread_join_result_destroy(&joined);
        r_test_checked_carrier_drop(&checked);
        R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 1U);
    }

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    spawned = r_library_internal_thread_spawn_checked(&allocator,
                                                      r_test_void_type(),
                                                      r_test_checked_completion_type(),
                                                      r_test_return_checked_error,
                                                      NULL);
    R_TEST_CHECK(spawned.is_ok);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
    r_std_thread_join_result_destroy(&joined);
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 1U);

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    spawned = r_library_internal_thread_spawn_checked(&allocator,
                                                      r_test_void_type(),
                                                      r_test_checked_completion_type(),
                                                      r_test_return_checked_value,
                                                      NULL);
    R_TEST_CHECK(spawned.is_ok);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
    r_library_internal_thread_join_result_move(&joined, &checked);
    R_TEST_CHECK((checked.tag == UINT32_C(0)) && (checked.payload.value.value == 82) &&
                 checked.payload.value.active);
    r_std_thread_join_result_destroy(&joined);
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 0U);
    r_test_checked_carrier_drop(&checked);
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 1U);

    atomic_store_explicit(&r_test_result_drops, 0U, memory_order_relaxed);
    spawned = r_library_internal_thread_spawn_checked(&allocator,
                                                      r_test_void_type(),
                                                      r_test_checked_completion_type(),
                                                      r_test_return_checked_error,
                                                      NULL);
    R_TEST_CHECK(spawned.is_ok);
    r_std_thread_detach(&spawned.value);
    while (atomic_load_explicit(&r_test_result_drops, memory_order_acquire) < 1U) {
        r_std_thread_sleep_nanoseconds(UINT64_C(1000000));
    }
    R_TEST_CHECK(atomic_load_explicit(&r_test_result_drops, memory_order_relaxed) == 1U);
    return 0;
}

static int r_test_park_and_clones(void) {
    enum {
        R_TEST_CLONE_WORKER_COUNT = 4
    };
    RRuntimeAllocator allocator;
    RTestParkContext park_context = {0};
    RTestParkContext *park_payload = &park_context;
    RStdThreadSpawnResult spawned;
    RStdThreadJoinResult joined;
    RTestCloneContext clone_context = {0};
    RTestCloneContext *clone_payload = &clone_context;
    RTestCloneWorker workers[R_TEST_CLONE_WORKER_COUNT];
    pthread_t threads[R_TEST_CLONE_WORKER_COUNT];
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    spawned = r_std_thread_spawn(
        &allocator, r_test_pointer_type(), r_test_void_type(), r_test_park_entry, &park_payload);
    R_TEST_CHECK(spawned.is_ok && r_test_wait_bool(&park_context.ready));
    r_std_thread_unpark(&park_context.identity);
    r_std_thread_unpark(&park_context.identity);
    atomic_store_explicit(&park_context.enter_park, 1, memory_order_release);
    R_TEST_CHECK(r_test_wait_bool(&park_context.first_return));
    r_std_thread_sleep_nanoseconds(UINT64_C(10000000));
    R_TEST_CHECK(!atomic_load_explicit(&park_context.second_return, memory_order_acquire));
    r_std_thread_unpark(&park_context.identity);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_COMPLETED);
    r_std_thread_join_result_destroy(&joined);
    r_std_thread_unpark(&park_context.identity);
    r_std_thread_thread_destroy(&park_context.identity);

    spawned = r_std_thread_spawn(
        &allocator, r_test_pointer_type(), r_test_void_type(), r_test_clone_entry, &clone_payload);
    R_TEST_CHECK(spawned.is_ok && r_test_wait_bool(&clone_context.ready));
    for (index = 0U; index < R_TEST_CLONE_WORKER_COUNT; ++index) {
        workers[index].identity = &clone_context.identity;
        workers[index].passed = 0;
        R_TEST_CHECK(pthread_create(&threads[index], NULL, r_test_clone_worker, &workers[index]) ==
                     0);
    }
    for (index = 0U; index < R_TEST_CLONE_WORKER_COUNT; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
        R_TEST_CHECK(workers[index].passed);
    }
    atomic_store_explicit(&clone_context.finish, 1, memory_order_release);
    joined = r_std_thread_join(&spawned.value);
    R_TEST_CHECK(joined.kind == R_STD_THREAD_JOIN_COMPLETED);
    r_std_thread_join_result_destroy(&joined);
    r_std_thread_thread_destroy(&clone_context.identity);
    return 0;
}

static int r_test_observers_and_panic(void) {
    static const uint8_t text[] = "worker diagnostic";
    RRuntimeAllocator allocator;
    RStdThread current = r_std_thread_current();
    RStdThread clone = r_std_thread_clone_thread(&current);
    RStdThreadPanicReport report = {0};
    RStdStringView category;
    RStdStringView diagnostic;

    R_TEST_CHECK(clone.descriptor == current.descriptor);
    r_std_thread_thread_destroy(&clone);
    r_std_thread_thread_destroy(&current);
    r_runtime_allocator_initialize(&allocator);
    report.category = R_RUNTIME_PANIC_EXPLICIT;
    R_TEST_CHECK(
        r_runtime_string_from_utf8(&report.text, &allocator, text, sizeof(text) - 1U, NULL) ==
        R_RUNTIME_STRING_OK);
    category = r_std_thread_panic_category(&report);
    diagnostic = r_std_thread_panic_text(&report);
    R_TEST_CHECK((category.length == strlen("explicit")) &&
                 (memcmp(category.data, "explicit", category.length) == 0));
    R_TEST_CHECK((diagnostic.length == sizeof(text) - 1U) &&
                 (memcmp(diagnostic.data, text, diagnostic.length) == 0));
    r_std_thread_panic_report_destroy(&report);

    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_start_failures() == 0);
    R_TEST_CHECK(r_test_join_and_cleanup() == 0);
    R_TEST_CHECK(r_test_park_and_clones() == 0);
    R_TEST_CHECK(r_test_observers_and_panic() == 0);
    r_std_thread_yield_now();
    r_std_thread_sleep_nanoseconds(UINT64_C(0));
    (void)puts("library_thread_ok");
    return 0;
}

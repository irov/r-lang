#include "r_library_fs_internal.h"

#include "r_runtime_allocator.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    R_TEST_DEADLINE_WAIT_SECONDS = 5,
    R_TEST_DEADLINE_STRESS_ITERATIONS = 256
};

typedef struct RTestDeadlineLedger {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    _Atomic size_t retain_count;
    _Atomic size_t release_count;
    _Atomic size_t expired_count;
    _Atomic size_t destroyed_count;
    _Bool block_callback;
    _Bool callback_entered;
    _Bool callback_may_return;
    _Bool callback_finished;
} RTestDeadlineLedger;

typedef struct RTestDeadlineContext {
    _Atomic size_t references;
    RTestDeadlineLedger *ledger;
    RLibraryFsPositionDeadline *deadline;
    _Bool destroy_deadline_on_expiry;
} RTestDeadlineContext;

_Noreturn static void fail(const char *message) {
    (void)fprintf(stderr, "library fs deadline test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static void ledger_initialize(RTestDeadlineLedger *ledger, _Bool block_callback) {
    (void)memset(ledger, 0, sizeof(*ledger));
    require(pthread_mutex_init(&ledger->mutex, NULL) == 0, "initialize ledger mutex");
    require(pthread_cond_init(&ledger->condition, NULL) == 0, "initialize ledger condition");
    atomic_init(&ledger->retain_count, 0U);
    atomic_init(&ledger->release_count, 0U);
    atomic_init(&ledger->expired_count, 0U);
    atomic_init(&ledger->destroyed_count, 0U);
    ledger->block_callback = block_callback;
}

static void ledger_destroy(RTestDeadlineLedger *ledger) {
    require(pthread_cond_destroy(&ledger->condition) == 0, "destroy ledger condition");
    require(pthread_mutex_destroy(&ledger->mutex) == 0, "destroy ledger mutex");
}

static struct timespec realtime_timeout(void) {
    struct timespec timeout;

    require(clock_gettime(CLOCK_REALTIME, &timeout) == 0, "read realtime clock");
    timeout.tv_sec += R_TEST_DEADLINE_WAIT_SECONDS;
    return timeout;
}

static RStdTimeInstant future_instant(uint64_t nanoseconds) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeInstantResult result;
    RStdTimeDuration duration;

    require(now.is_ok, "read continuous clock for deadline");
    duration.seconds = (int64_t)(nanoseconds / nanoseconds_per_second);
    duration.nanoseconds = (uint32_t)(nanoseconds % nanoseconds_per_second);
    result = r_std_time_instant_add(now.value, duration);
    require(result.is_ok, "construct future continuous deadline");
    return result.value;
}

static void
wait_for_callback_state(RTestDeadlineLedger *ledger, _Bool wait_for_finished, const char *message) {
    struct timespec timeout = realtime_timeout();
    int status = 0;

    require(pthread_mutex_lock(&ledger->mutex) == 0, "lock callback ledger");
    while (!(wait_for_finished ? ledger->callback_finished : ledger->callback_entered)) {
        status = pthread_cond_timedwait(&ledger->condition, &ledger->mutex, &timeout);
        if (status == ETIMEDOUT) {
            (void)pthread_mutex_unlock(&ledger->mutex);
            fail(message);
        }
        if (status != 0) {
            (void)pthread_mutex_unlock(&ledger->mutex);
            fail("wait for callback state");
        }
    }
    require(pthread_mutex_unlock(&ledger->mutex) == 0, "unlock callback ledger");
}

static void allow_callback_return(RTestDeadlineLedger *ledger) {
    require(pthread_mutex_lock(&ledger->mutex) == 0, "lock blocked callback ledger");
    ledger->callback_may_return = 1;
    require(pthread_cond_broadcast(&ledger->condition) == 0, "release blocked callback");
    require(pthread_mutex_unlock(&ledger->mutex) == 0, "unlock blocked callback ledger");
}

static void wait_for_context_destruction(RTestDeadlineLedger *ledger, const char *message) {
    struct timespec start;
    struct timespec current;

    require(clock_gettime(CLOCK_MONOTONIC, &start) == 0, "read monotonic clock");
    while (atomic_load_explicit(&ledger->destroyed_count, memory_order_acquire) == 0U) {
        (void)sched_yield();
        require(clock_gettime(CLOCK_MONOTONIC, &current) == 0, "reread monotonic clock");
        if (current.tv_sec - start.tv_sec >= R_TEST_DEADLINE_WAIT_SECONDS) {
            fail(message);
        }
    }
}

static RTestDeadlineContext *context_create(RRuntimeAllocator *allocator,
                                            RTestDeadlineLedger *ledger,
                                            RLibraryFsPositionDeadline *deadline,
                                            _Bool destroy_deadline_on_expiry) {
    RTestDeadlineContext *context = NULL;

    require(r_runtime_allocator_allocate(
                allocator, sizeof(*context), _Alignof(RTestDeadlineContext), (void **)&context) ==
                R_RUNTIME_ALLOCATION_OK,
            "allocate deadline context");
    (void)memset(context, 0, sizeof(*context));
    atomic_init(&context->references, 1U);
    context->ledger = ledger;
    context->deadline = deadline;
    context->destroy_deadline_on_expiry = destroy_deadline_on_expiry;
    return context;
}

static void context_retain(void *context_pointer) {
    RTestDeadlineContext *context = context_pointer;
    size_t current = atomic_load_explicit(&context->references, memory_order_relaxed);

    for (;;) {
        if (current == 0U || current == SIZE_MAX) {
            abort();
        }
        if (atomic_compare_exchange_weak_explicit(&context->references,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            break;
        }
    }
    (void)atomic_fetch_add_explicit(&context->ledger->retain_count, 1U, memory_order_relaxed);
}

static void context_release_reference(RTestDeadlineContext *context) {
    RTestDeadlineLedger *ledger = context->ledger;
    size_t previous = atomic_fetch_sub_explicit(&context->references, 1U, memory_order_acq_rel);

    if (previous == 0U) {
        abort();
    }
    if (previous == 1U) {
        r_runtime_allocator_deallocate(context, _Alignof(RTestDeadlineContext));
        (void)atomic_fetch_add_explicit(&ledger->destroyed_count, 1U, memory_order_release);
    }
}

static void context_release(void *context_pointer) {
    RTestDeadlineContext *context = context_pointer;

    (void)atomic_fetch_add_explicit(&context->ledger->release_count, 1U, memory_order_relaxed);
    context_release_reference(context);
}

static void context_owner_release(RTestDeadlineContext *context) {
    context_release_reference(context);
}

static void deadline_expired(void *context_pointer) {
    RTestDeadlineContext *context = context_pointer;
    RTestDeadlineLedger *ledger = context->ledger;
    size_t previous = atomic_fetch_add_explicit(&ledger->expired_count, 1U, memory_order_acq_rel);

    if (previous != 0U) {
        abort();
    }
    if (context->destroy_deadline_on_expiry) {
        r_library_internal_fs_position_deadline_destroy(context->deadline);
    }
    if (pthread_mutex_lock(&ledger->mutex) != 0) {
        abort();
    }
    ledger->callback_entered = 1;
    if (pthread_cond_broadcast(&ledger->condition) != 0) {
        abort();
    }
    while (ledger->block_callback && !ledger->callback_may_return) {
        if (pthread_cond_wait(&ledger->condition, &ledger->mutex) != 0) {
            abort();
        }
    }
    ledger->callback_finished = 1;
    if (pthread_cond_broadcast(&ledger->condition) != 0) {
        abort();
    }
    if (pthread_mutex_unlock(&ledger->mutex) != 0) {
        abort();
    }
}

static void require_source_lifetime(const RTestDeadlineLedger *ledger,
                                    size_t expected_expired,
                                    size_t expected_retained,
                                    const char *message) {
    size_t retained = atomic_load_explicit(&ledger->retain_count, memory_order_acquire);
    size_t released = atomic_load_explicit(&ledger->release_count, memory_order_acquire);

    require(retained == expected_retained && released == expected_retained, message);
    require(atomic_load_explicit(&ledger->expired_count, memory_order_acquire) == expected_expired,
            message);
    require(atomic_load_explicit(&ledger->destroyed_count, memory_order_acquire) == 1U, message);
}

static void test_suspended_initialize_destroy(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;

    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 0);
    context = context_create(&allocator, &ledger, &deadline, 0);
    require(
        r_library_internal_fs_position_deadline_initialize(&deadline,
                                                           &allocator,
                                                           future_instant(UINT64_C(30000000000)),
                                                           deadline_expired,
                                                           context_retain,
                                                           context_release,
                                                           context),
        "initialize suspended deadline");
    r_library_internal_fs_position_deadline_destroy(&deadline);
    require(deadline.token == NULL, "suspended destroy did not clear wrapper");
    context_owner_release(context);
    wait_for_context_destruction(&ledger, "suspended cancel handler did not release context");
    require_source_lifetime(&ledger, 0U, 1U, "suspended deadline source lifetime");
    ledger_destroy(&ledger);
}

static void test_activated_immediate_cancel_destroy(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;

    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 0);
    context = context_create(&allocator, &ledger, &deadline, 0);
    require(
        r_library_internal_fs_position_deadline_initialize(&deadline,
                                                           &allocator,
                                                           future_instant(UINT64_C(30000000000)),
                                                           deadline_expired,
                                                           context_retain,
                                                           context_release,
                                                           context),
        "initialize activated deadline");
    r_library_internal_fs_position_deadline_activate(&deadline);
    r_library_internal_fs_position_deadline_cancel(&deadline);
    r_library_internal_fs_position_deadline_cancel(&deadline);
    r_library_internal_fs_position_deadline_destroy(&deadline);
    context_owner_release(context);
    wait_for_context_destruction(&ledger, "activated cancel handler did not release context");
    require_source_lifetime(&ledger, 0U, 1U, "activated deadline source lifetime");
    ledger_destroy(&ledger);
}

static void test_far_horizon_is_clamped_and_cancelled(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;

    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 0);
    context = context_create(&allocator, &ledger, &deadline, 0);
    require(r_library_internal_fs_position_deadline_initialize(&deadline,
                                                               &allocator,
                                                               (RStdTimeInstant){
                                                                   INT64_MAX,
                                                                   UINT32_C(999999999),
                                                               },
                                                               deadline_expired,
                                                               context_retain,
                                                               context_release,
                                                               context),
            "initialize deadline beyond signed Dispatch interval");
    r_library_internal_fs_position_deadline_activate(&deadline);
    r_library_internal_fs_position_deadline_destroy(&deadline);
    context_owner_release(context);
    wait_for_context_destruction(&ledger, "far deadline context was not released");
    require_source_lifetime(&ledger, 0U, 1U, "far deadline source lifetime");
    ledger_destroy(&ledger);
}

static void test_natural_expiry_exactly_once(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;

    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 0);
    context = context_create(&allocator, &ledger, &deadline, 0);
    require(r_library_internal_fs_position_deadline_initialize(&deadline,
                                                               &allocator,
                                                               future_instant(UINT64_C(1000000)),
                                                               deadline_expired,
                                                               context_retain,
                                                               context_release,
                                                               context),
            "initialize natural deadline");
    r_library_internal_fs_position_deadline_activate(&deadline);
    wait_for_callback_state(&ledger, 1, "natural deadline did not expire");
    r_library_internal_fs_position_deadline_activate(&deadline);
    r_library_internal_fs_position_deadline_destroy(&deadline);
    context_owner_release(context);
    wait_for_context_destruction(&ledger, "natural deadline context was not released");
    require_source_lifetime(&ledger, 1U, 2U, "natural deadline exactly-once lifetime");
    ledger_destroy(&ledger);
}

static void test_expiry_wins_cancel_race(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;
    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 1);
    context = context_create(&allocator, &ledger, &deadline, 0);
    require(r_library_internal_fs_position_deadline_initialize(&deadline,
                                                               &allocator,
                                                               future_instant(UINT64_C(1000000)),
                                                               deadline_expired,
                                                               context_retain,
                                                               context_release,
                                                               context),
            "initialize event-wins deadline");
    r_library_internal_fs_position_deadline_activate(&deadline);
    wait_for_callback_state(&ledger, 0, "event did not enter before cancellation");
    r_library_internal_fs_position_deadline_cancel(&deadline);
    r_library_internal_fs_position_deadline_destroy(&deadline);
    context_owner_release(context);
    require(atomic_load_explicit(&ledger.destroyed_count, memory_order_acquire) == 0U,
            "event-wins context was released during expiry callback");
    allow_callback_return(&ledger);
    wait_for_callback_state(&ledger, 1, "event did not finish after cancellation");
    wait_for_context_destruction(&ledger, "event-wins deadline context was not released");
    require_source_lifetime(&ledger, 1U, 2U, "event-wins cancellation lifetime");
    ledger_destroy(&ledger);
}

static void test_expiry_cancel_stress(void) {
    RRuntimeAllocator allocator;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    for (index = 0U; index < R_TEST_DEADLINE_STRESS_ITERATIONS; ++index) {
        RTestDeadlineLedger ledger;
        RLibraryFsPositionDeadline deadline = {0};
        RTestDeadlineContext *context;
        size_t expired;
        size_t expected_retained;

        ledger_initialize(&ledger, (index & 1U) != 0U);
        context = context_create(&allocator, &ledger, &deadline, 0);
        require(r_library_internal_fs_position_deadline_initialize(
                    &deadline,
                    &allocator,
                    future_instant((index & 1U) != 0U ? UINT64_C(1000000) : UINT64_C(30000000000)),
                    deadline_expired,
                    context_retain,
                    context_release,
                    context),
                "initialize stress deadline");
        r_library_internal_fs_position_deadline_activate(&deadline);
        if ((index & 1U) != 0U) {
            wait_for_callback_state(&ledger, 0, "stress deadline did not enter");
        }
        r_library_internal_fs_position_deadline_cancel(&deadline);
        if ((index & 1U) != 0U) {
            allow_callback_return(&ledger);
            wait_for_callback_state(&ledger, 1, "stress deadline did not finish");
        }
        r_library_internal_fs_position_deadline_destroy(&deadline);
        context_owner_release(context);
        wait_for_context_destruction(&ledger, "stress deadline context was not released");
        expired = atomic_load_explicit(&ledger.expired_count, memory_order_acquire);
        require(expired == ((index & 1U) != 0U ? 1U : 0U),
                "stress deadline winner was not deterministic");
        expected_retained = expired != 0U ? 2U : 1U;
        require_source_lifetime(
            &ledger, expired, expected_retained, "stress deadline source lifetime");
        ledger_destroy(&ledger);
    }
}

static void test_expired_callback_destroys_wrapper(void) {
    RRuntimeAllocator allocator;
    RTestDeadlineLedger ledger;
    RLibraryFsPositionDeadline deadline = {0};
    RTestDeadlineContext *context;

    r_runtime_allocator_initialize(&allocator);
    ledger_initialize(&ledger, 0);
    context = context_create(&allocator, &ledger, &deadline, 1);
    require(r_library_internal_fs_position_deadline_initialize(&deadline,
                                                               &allocator,
                                                               future_instant(UINT64_C(1000000)),
                                                               deadline_expired,
                                                               context_retain,
                                                               context_release,
                                                               context),
            "initialize callback-destroy deadline");
    r_library_internal_fs_position_deadline_activate(&deadline);
    wait_for_callback_state(&ledger, 1, "callback-destroy deadline did not finish");
    require(deadline.token == NULL, "expired callback did not clear wrapper");
    context_owner_release(context);
    wait_for_context_destruction(&ledger, "callback-destroy context was not released");
    require_source_lifetime(&ledger, 1U, 2U, "callback-destroy source lifetime");
    r_library_internal_fs_position_deadline_destroy(&deadline);
    ledger_destroy(&ledger);
}

int main(void) {
    test_suspended_initialize_destroy();
    test_activated_immediate_cancel_destroy();
    test_far_horizon_is_clamped_and_cancelled();
    test_natural_expiry_exactly_once();
    test_expiry_wins_cancel_race();
    test_expiry_cancel_stress();
    test_expired_callback_destroys_wrapper();
    (void)puts("library_fs_deadline_tests: ok");
    return EXIT_SUCCESS;
}

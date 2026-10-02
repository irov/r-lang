#include "r_runtime_darwin_event.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define R_EVENT_TEST_REQUIRE(condition, message)                                                   \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "event test failure: %s\n", (message));                          \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)

enum {
    R_EVENT_TEST_THREAD_COUNT = 8,
    R_EVENT_TEST_VALUES_PER_THREAD = 4096,
    R_EVENT_TEST_VALUE_COUNT = R_EVENT_TEST_THREAD_COUNT * R_EVENT_TEST_VALUES_PER_THREAD,
};

typedef struct REventTestThreadContext {
    _Atomic size_t *ready_count;
    _Atomic _Bool *start;
    uint64_t *values;
} REventTestThreadContext;

static void *generate_sequences(void *argument) {
    REventTestThreadContext *context = argument;
    size_t index;

    (void)atomic_fetch_add_explicit(context->ready_count, 1U, memory_order_release);
    while (!atomic_load_explicit(context->start, memory_order_acquire)) {
        (void)sched_yield();
    }
    for (index = 0U; index < R_EVENT_TEST_VALUES_PER_THREAD; ++index) {
        context->values[index] = r_runtime_darwin_event_sequence_next();
    }
    return NULL;
}

static int compare_sequences(const void *left_pointer, const void *right_pointer) {
    const uint64_t left = *(const uint64_t *)left_pointer;
    const uint64_t right = *(const uint64_t *)right_pointer;

    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static int test_sequential_monotonicity(void) {
    r_runtime_darwin_event_testing_set_sequence(UINT64_C(0));

    R_EVENT_TEST_REQUIRE(r_runtime_darwin_event_sequence_next() == UINT64_C(1),
                         "the initial sequence was not one");
    R_EVENT_TEST_REQUIRE(r_runtime_darwin_event_sequence_next() == UINT64_C(2),
                         "the second sequence was not monotonic");
    R_EVENT_TEST_REQUIRE(r_runtime_darwin_event_sequence_next() == UINT64_C(3),
                         "the third sequence was not monotonic");
    return 1;
}

static int test_concurrent_global_sequence(void) {
    pthread_t threads[R_EVENT_TEST_THREAD_COUNT];
    REventTestThreadContext contexts[R_EVENT_TEST_THREAD_COUNT];
    uint64_t values[R_EVENT_TEST_VALUE_COUNT];
    _Atomic size_t ready_count;
    _Atomic _Bool start;
    size_t thread_index;
    size_t value_index;

    atomic_init(&ready_count, 0U);
    atomic_init(&start, 0);
    for (thread_index = 0U; thread_index < R_EVENT_TEST_THREAD_COUNT; ++thread_index) {
        contexts[thread_index].ready_count = &ready_count;
        contexts[thread_index].start = &start;
        contexts[thread_index].values = &values[thread_index * R_EVENT_TEST_VALUES_PER_THREAD];
        R_EVENT_TEST_REQUIRE(
            pthread_create(
                &threads[thread_index], NULL, generate_sequences, &contexts[thread_index]) == 0,
            "could not create a sequence producer");
    }
    while (atomic_load_explicit(&ready_count, memory_order_acquire) != R_EVENT_TEST_THREAD_COUNT) {
        (void)sched_yield();
    }
    atomic_store_explicit(&start, 1, memory_order_release);
    for (thread_index = 0U; thread_index < R_EVENT_TEST_THREAD_COUNT; ++thread_index) {
        R_EVENT_TEST_REQUIRE(pthread_join(threads[thread_index], NULL) == 0,
                             "could not join a sequence producer");
    }

    qsort(values, R_EVENT_TEST_VALUE_COUNT, sizeof(values[0]), compare_sequences);
    for (value_index = 0U; value_index < R_EVENT_TEST_VALUE_COUNT; ++value_index) {
        const uint64_t expected = UINT64_C(4) + (uint64_t)value_index;

        R_EVENT_TEST_REQUIRE(values[value_index] != UINT64_C(0),
                             "a concurrent producer returned zero");
        R_EVENT_TEST_REQUIRE(values[value_index] == expected,
                             "the concurrent global sequence had a gap or duplicate");
    }
    R_EVENT_TEST_REQUIRE(r_runtime_darwin_event_sequence_next() ==
                             UINT64_C(4) + (uint64_t)R_EVENT_TEST_VALUE_COUNT,
                         "the post-concurrency sequence was not globally monotonic");
    return 1;
}

static _Noreturn void run_overflow_child(void) {
    const struct rlimit no_core = {0U, 0U};

    (void)setrlimit(RLIMIT_CORE, &no_core);
    r_runtime_darwin_event_testing_set_sequence(UINT64_MAX - UINT64_C(1));
    if (r_runtime_darwin_event_sequence_next() != UINT64_MAX) {
        _exit(2);
    }
    (void)r_runtime_darwin_event_sequence_next();
    _exit(3);
}

static int test_overflow_aborts_without_parent_mutation(void) {
    const uint64_t parent_next = UINT64_C(5) + (uint64_t)R_EVENT_TEST_VALUE_COUNT;
    pid_t child;
    int status = 0;

    child = fork();
    R_EVENT_TEST_REQUIRE(child >= 0, "could not fork the overflow probe");
    if (child == 0) {
        run_overflow_child();
    }
    R_EVENT_TEST_REQUIRE(waitpid(child, &status, 0) == child,
                         "could not wait for the overflow probe");
    R_EVENT_TEST_REQUIRE(WIFSIGNALED(status), "overflow did not terminate by signal");
    R_EVENT_TEST_REQUIRE(WTERMSIG(status) == SIGABRT, "overflow did not abort");
    R_EVENT_TEST_REQUIRE(r_runtime_darwin_event_sequence_next() == parent_next,
                         "the overflow probe mutated parent sequence state");
    return 1;
}

int main(void) {
    if (!test_sequential_monotonicity() || !test_concurrent_global_sequence() ||
        !test_overflow_aborts_without_parent_mutation()) {
        return 1;
    }
    (void)puts("runtime_darwin_event_ok");
    return 0;
}

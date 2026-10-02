#include "r_runtime_allocator.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_signal.h"

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct TestSignalCompletion {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinSignalWaitResult result;
    _Bool completed;
    _Bool return_count;
} TestSignalCompletion;

static void completion_initialize(TestSignalCompletion *completion) {
    *completion = (TestSignalCompletion){0};
    if (pthread_mutex_init(&completion->mutex, NULL) != 0 ||
        pthread_cond_init(&completion->condition, NULL) != 0) {
        abort();
    }
}

static void completion_destroy(TestSignalCompletion *completion) {
    (void)pthread_cond_destroy(&completion->condition);
    (void)pthread_mutex_destroy(&completion->mutex);
}

static void test_completed(RRuntimeDarwinSignalWait *wait, void *context) {
    TestSignalCompletion *completion = context;
    RRuntimeDarwinSignalWaitResult result = r_runtime_darwin_signal_wait_result(wait);

    if (completion->return_count) {
        r_runtime_darwin_signal_wait_return_count(wait);
    }
    if (pthread_mutex_lock(&completion->mutex) != 0) {
        abort();
    }
    completion->result = result;
    completion->completed = 1;
    if (pthread_cond_broadcast(&completion->condition) != 0 ||
        pthread_mutex_unlock(&completion->mutex) != 0) {
        abort();
    }
}

static RRuntimeDarwinSignalWaitResult completion_wait(TestSignalCompletion *completion) {
    RRuntimeDarwinSignalWaitResult result;

    if (pthread_mutex_lock(&completion->mutex) != 0) {
        abort();
    }
    while (!completion->completed) {
        if (pthread_cond_wait(&completion->condition, &completion->mutex) != 0) {
            abort();
        }
    }
    result = completion->result;
    if (pthread_mutex_unlock(&completion->mutex) != 0) {
        abort();
    }
    return result;
}

/* Starts one wait with the test completion, as the external start of a task does. */
static RRuntimeDarwinSignalWait *start_wait(RRuntimeAllocator *allocator,
                                            RRuntimeDarwinSignalListener *listener,
                                            _Bool has_timeout,
                                            uint64_t timeout_nanoseconds,
                                            TestSignalCompletion *completion) {
    RRuntimeDarwinSignalWaitPrepareResult prepared = r_runtime_darwin_signal_wait_prepare(
        allocator, listener, has_timeout, timeout_nanoseconds);

    if (prepared.status != R_RUNTIME_DARWIN_SIGNAL_OK || prepared.wait == NULL) {
        abort();
    }
    r_runtime_darwin_signal_wait_bind(prepared.wait, test_completed, completion);
    r_runtime_darwin_signal_wait_activate(prepared.wait);
    return prepared.wait;
}

static void pause_briefly(void) {
    const struct timespec delay = {0, 20000000L};

    (void)nanosleep(&delay, NULL);
}

static int test_delivery_and_restoration(RRuntimeAllocator *allocator) {
    struct sigaction before;
    struct sigaction during;
    struct sigaction after;
    RRuntimeDarwinSignalListenResult first;
    RRuntimeDarwinSignalListenResult second;
    TestSignalCompletion completion;
    RRuntimeDarwinSignalWait *wait;
    RRuntimeDarwinSignalWaitResult result;
    sigset_t replaced;

    CHECK(sigaction(SIGUSR1, NULL, &before) == 0);
    CHECK(before.sa_handler == SIG_DFL);
    first = r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_USER1);
    CHECK(first.status == R_RUNTIME_DARWIN_SIGNAL_OK && first.listener != NULL);
    second = r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_USER1);
    CHECK(second.status == R_RUNTIME_DARWIN_SIGNAL_OK && second.listener != NULL);
    CHECK(sigaction(SIGUSR1, NULL, &during) == 0);
    CHECK(during.sa_handler == SIG_IGN);
    r_runtime_darwin_signal_replaced_set(&replaced);
    CHECK(sigismember(&replaced, SIGUSR1) == 1);
    CHECK(sigismember(&replaced, SIGUSR2) == 0);

    /* A wait queued before the delivery completes with it. */
    completion_initialize(&completion);
    wait = start_wait(allocator, first.listener, 0, UINT64_C(0), &completion);
    CHECK(r_runtime_darwin_signal_raise(R_RUNTIME_DARWIN_SIGNAL_USER1) == 0);
    result = completion_wait(&completion);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    CHECK(result.count >= UINT64_C(1));
    CHECK(result.terminal_event_sequence != UINT64_C(0));
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&completion);

    /* The other listener counted the same delivery before any wait of its own. */
    completion_initialize(&completion);
    wait = start_wait(allocator, second.listener, 0, UINT64_C(0), &completion);
    result = completion_wait(&completion);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    CHECK(result.count >= UINT64_C(1));
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&completion);

    r_runtime_darwin_signal_listener_release(first.listener);
    CHECK(sigaction(SIGUSR1, NULL, &during) == 0);
    CHECK(during.sa_handler == SIG_IGN);
    r_runtime_darwin_signal_listener_release(second.listener);
    CHECK(sigaction(SIGUSR1, NULL, &after) == 0);
    CHECK(after.sa_handler == SIG_DFL);
    r_runtime_darwin_signal_replaced_set(&replaced);
    CHECK(sigismember(&replaced, SIGUSR1) == 0);
    return 0;
}

static int test_deadline_and_order(RRuntimeAllocator *allocator) {
    RRuntimeDarwinSignalListenResult listener =
        r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_USER2);
    TestSignalCompletion timed;
    TestSignalCompletion early;
    TestSignalCompletion late;
    RRuntimeDarwinSignalWait *timed_wait;
    RRuntimeDarwinSignalWait *early_wait;
    RRuntimeDarwinSignalWait *late_wait;
    RRuntimeDarwinSignalWaitResult result;

    CHECK(listener.status == R_RUNTIME_DARWIN_SIGNAL_OK);
    completion_initialize(&timed);
    timed_wait = start_wait(allocator, listener.listener, 1, UINT64_C(20000000), &timed);
    result = completion_wait(&timed);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_TIMED_OUT);
    CHECK(result.count == UINT64_C(0));
    r_runtime_darwin_signal_wait_release(&timed_wait);
    completion_destroy(&timed);

    /* Two queued waits: the delivery completes the older one only. */
    completion_initialize(&early);
    completion_initialize(&late);
    early_wait = start_wait(allocator, listener.listener, 0, UINT64_C(0), &early);
    late_wait = start_wait(allocator, listener.listener, 1, UINT64_C(200000000), &late);
    CHECK(r_runtime_darwin_signal_raise(R_RUNTIME_DARWIN_SIGNAL_USER2) == 0);
    result = completion_wait(&early);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    result = completion_wait(&late);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_TIMED_OUT);
    r_runtime_darwin_signal_wait_release(&early_wait);
    r_runtime_darwin_signal_wait_release(&late_wait);
    completion_destroy(&early);
    completion_destroy(&late);
    r_runtime_darwin_signal_listener_release(listener.listener);
    return 0;
}

static int test_cancellation_returns_count(RRuntimeAllocator *allocator) {
    RRuntimeDarwinSignalListenResult listener =
        r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_HANGUP);
    TestSignalCompletion cancelled;
    TestSignalCompletion refused;
    TestSignalCompletion next;
    RRuntimeDarwinSignalWait *wait;
    RRuntimeDarwinSignalWaitPrepareResult prepared;
    RRuntimeDarwinSignalWaitResult result;
    uint64_t cancellation_sequence;

    CHECK(listener.status == R_RUNTIME_DARWIN_SIGNAL_OK);
    /* A queued wait cancelled before any delivery. */
    completion_initialize(&cancelled);
    wait = start_wait(allocator, listener.listener, 0, UINT64_C(0), &cancelled);
    r_runtime_darwin_signal_wait_cancel(&wait, r_runtime_darwin_event_sequence_next());
    result = completion_wait(&cancelled);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED);
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&cancelled);

    /* A delivery counted before a wait, whose cancellation was requested earlier: the count
       returns to the listener and the next wait receives it. */
    CHECK(r_runtime_darwin_signal_raise(R_RUNTIME_DARWIN_SIGNAL_HANGUP) == 0);
    pause_briefly();
    completion_initialize(&cancelled);
    cancellation_sequence = r_runtime_darwin_event_sequence_next();
    wait = start_wait(allocator, listener.listener, 0, UINT64_C(0), &cancelled);
    r_runtime_darwin_signal_wait_cancel(&wait, cancellation_sequence);
    result = completion_wait(&cancelled);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED);
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&cancelled);
    completion_initialize(&next);
    wait = start_wait(allocator, listener.listener, 1, UINT64_C(1000000000), &next);
    result = completion_wait(&next);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    CHECK(result.count >= UINT64_C(1));
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&next);

    /* A delivered completion that the task refused returns its count too. */
    CHECK(r_runtime_darwin_signal_raise(R_RUNTIME_DARWIN_SIGNAL_HANGUP) == 0);
    pause_briefly();
    completion_initialize(&refused);
    refused.return_count = 1;
    wait = start_wait(allocator, listener.listener, 0, UINT64_C(0), &refused);
    result = completion_wait(&refused);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&refused);
    completion_initialize(&next);
    wait = start_wait(allocator, listener.listener, 1, UINT64_C(1000000000), &next);
    result = completion_wait(&next);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED);
    r_runtime_darwin_signal_wait_release(&wait);
    completion_destroy(&next);

    /* A prepared wait that never starts is disposed of. */
    prepared =
        r_runtime_darwin_signal_wait_prepare(allocator, listener.listener, 1, UINT64_C(1000));
    CHECK(prepared.status == R_RUNTIME_DARWIN_SIGNAL_OK);
    r_runtime_darwin_signal_wait_abort(&prepared.wait);
    CHECK(prepared.wait == NULL);
    r_runtime_darwin_signal_listener_release(listener.listener);
    return 0;
}

static int test_allocation_failure(RRuntimeAllocator *allocator) {
    RRuntimeDarwinSignalListenResult result;
    struct sigaction action;

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    result = r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_TERMINATE);
    CHECK(result.status == R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED && result.listener == NULL);
    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    result = r_runtime_darwin_signal_listen(allocator, R_RUNTIME_DARWIN_SIGNAL_TERMINATE);
    CHECK(result.status == R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED && result.listener == NULL);
    CHECK(sigaction(SIGTERM, NULL, &action) == 0);
    CHECK(action.sa_handler == SIG_DFL);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    CHECK(r_runtime_darwin_signal_number(R_RUNTIME_DARWIN_SIGNAL_INTERRUPT) == SIGINT);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    if (test_delivery_and_restoration(&allocator) != 0 ||
        test_deadline_and_order(&allocator) != 0 ||
        test_cancellation_returns_count(&allocator) != 0 ||
        test_allocation_failure(&allocator) != 0) {
        return 1;
    }
    (void)printf("r_runtime_darwin_signal: ok\n");
    return 0;
}

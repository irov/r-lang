#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_net.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestListenerState {
    size_t references;
    size_t reservations;
    int descriptor;
    _Bool terminal;
    _Bool queue_empty;
    _Bool drain_empty;
} RTestListenerState;

typedef struct RTestCloseThreadContext {
    RStdNetTcpListener *listener;
    RStdNetDeadline deadline;
    RStdNetTaskStartResult started;
} RTestCloseThreadContext;

typedef struct RTestStopThreadContext {
    _Bool stopped;
} RTestStopThreadContext;

static void *start_close_thread(void *context_pointer) {
    RTestCloseThreadContext *context = context_pointer;

    context->started = r_std_net_tcp_listener_close(context->listener, context->deadline);
    return NULL;
}

static void *stop_executor_thread(void *context_pointer) {
    RTestStopThreadContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static RStdNetSocketAddress v4_loopback(uint16_t port) {
    RStdNetSocketAddress address = {0};
    static const uint8_t bytes[4] = {UINT8_C(127), UINT8_C(0), UINT8_C(0), UINT8_C(1)};

    address.address.kind = R_STD_NET_IP_ADDRESS_V4;
    (void)memcpy(address.address.bytes.v4, bytes, sizeof(bytes));
    address.port = port;
    return address;
}

static int await_listener(RStdNetTaskStartResult started, RStdNetTcpListenerResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_connection(RStdNetTaskStartResult started, RStdNetTcpConnectionResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_close(RStdNetTaskStartResult started, RStdNetVoidResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int create_listener(RStdNetTcpListenerResult *listener) {
    const RStdNetListenOptions options = {8U, 0, 0};
    RStdNetTaskStartResult started =
        r_std_net_tcp_listen(v4_loopback(0U), options, (RStdNetDeadline){0});

    return await_listener(started, listener) && listener->r_tag == UINT32_C(0) &&
           listener->value.storage != NULL;
}

static RStdNetDeadline deadline_after(RStdTimeDuration duration) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeInstantResult value;

    if (!now.is_ok) {
        abort();
    }
    value = r_std_time_instant_add(now.value, duration);
    if (!value.is_ok) {
        abort();
    }
    return (RStdNetDeadline){1, value.value};
}

static RTestListenerState listener_state(RStdNetTcpListenerStorage *storage) {
    RTestListenerState state = {0};

    if (pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    state.references = atomic_load_explicit(&storage->handle.references, memory_order_acquire);
    state.reservations = storage->accept_reservations;
    state.descriptor = storage->handle.descriptor;
    state.terminal = storage->handle.terminal;
    state.queue_empty = storage->accept_head == NULL && storage->accept_tail == NULL;
    state.drain_empty = storage->accept_drain == NULL && storage->accept_drain_context == NULL;
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
    return state;
}

static int wait_for_closed_state(RStdNetTcpListenerStorage *storage, size_t references) {
    size_t attempt;

    for (attempt = 0U; attempt < 100000U; ++attempt) {
        const RTestListenerState state = listener_state(storage);

        if (state.references == references && state.reservations == 0U && state.descriptor < 0 &&
            state.terminal && state.queue_empty && state.drain_empty) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static int test_start_failures_preserve_owner(RRuntimeAllocator *allocator) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpListenerStorage *storage;
    uint64_t fail_at;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(2); ++fail_at) {
        RStdNetTaskStartResult started;
        RStdNetSocketAddressResult observed;
        RTestListenerState state;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_listener_close(&listener.value, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL);
        R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == fail_at);
        R_TEST_CHECK(listener.value.storage == storage);
        observed = r_std_net_tcp_listener_local_address(&listener.value);
        R_TEST_CHECK(observed.is_ok);
        state = listener_state(storage);
        R_TEST_CHECK(state.references == 1U && state.reservations == 0U && state.descriptor >= 0 &&
                     !state.terminal && state.queue_empty && state.drain_empty);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_deadline_start_failures_preserve_owner(RRuntimeAllocator *allocator) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpListenerStorage *storage;
    uint64_t fail_at;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(3); ++fail_at) {
        const RStdNetDeadline deadline =
            deadline_after(r_std_time_duration_from_seconds(INT64_C(2)));
        RStdNetTaskStartResult started;
        RStdNetSocketAddressResult observed;
        RTestListenerState state;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_listener_close(&listener.value, deadline);
        R_TEST_CHECK(!started.is_ok && started.task == NULL);
        R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == fail_at);
        R_TEST_CHECK(listener.value.storage == storage);
        observed = r_std_net_tcp_listener_local_address(&listener.value);
        R_TEST_CHECK(observed.is_ok);
        state = listener_state(storage);
        R_TEST_CHECK(state.references == 1U && state.reservations == 0U && state.descriptor >= 0 &&
                     !state.terminal && state.queue_empty && state.drain_empty);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_empty_close(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTcpListenerStorage *storage;
    RStdNetTaskStartResult started;
    int descriptor;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    started = r_std_net_tcp_listener_close(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && listener.value.storage == NULL);
    R_TEST_CHECK(await_close(started, &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(0));
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    r_library_internal_net_handle_release(&storage->handle);
    return 0;
}

static int test_close_drains_accepts_and_rejects_new_operations(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpListener alias = {0};
    RStdNetTcpConnectionResult first_result = {0};
    RStdNetTcpConnectionResult second_result = {0};
    RStdNetTcpConnectionResult third_result = {0};
    RStdNetTcpConnectionResult rejected_result = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTaskStartResult first;
    RStdNetTaskStartResult second;
    RStdNetTaskStartResult third;
    RStdNetTaskStartResult rejected;
    RStdNetTaskStartResult close_started;
    RStdNetTcpListenerStorage *storage;
    RStdNetSocketAddressResult observed;
    int descriptor;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    first = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    second = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    third = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(first.is_ok && second.is_ok && third.is_ok);
    close_started = r_std_net_tcp_listener_close(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL &&
                 listener.value.storage == NULL);

    alias.storage = storage;
    observed = r_std_net_tcp_listener_local_address(&alias);
    R_TEST_CHECK(!observed.is_ok && observed.error.code == R_STD_NET_ERROR_CLOSED);
    rejected = r_std_net_tcp_accept(&alias, (RStdNetDeadline){0});
    R_TEST_CHECK(await_connection(rejected, &rejected_result));
    R_TEST_CHECK(rejected_result.r_tag == UINT32_C(1) &&
                 rejected_result.error.code == R_STD_NET_ERROR_CLOSED);

    R_TEST_CHECK(await_close(close_started, &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_connection(first, &first_result));
    R_TEST_CHECK(await_connection(second, &second_result));
    R_TEST_CHECK(await_connection(third, &third_result));
    R_TEST_CHECK(first_result.r_tag == UINT32_C(1) &&
                 first_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(second_result.r_tag == UINT32_C(1) &&
                 second_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(third_result.r_tag == UINT32_C(1) &&
                 third_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    r_library_internal_net_handle_release(&storage->handle);
    return 0;
}

static int test_immediate_accept_does_not_block_close(void) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult accept_result = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTaskStartResult accept_started;
    RStdNetTaskStartResult close_started;
    RStdNetTcpListenerStorage *storage;
    RTestListenerState state;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    accept_started = r_std_net_tcp_accept(&listener.value, expired);
    R_TEST_CHECK(accept_started.is_ok && accept_started.task != NULL);
    state = listener_state(storage);
    R_TEST_CHECK(state.references == 2U && state.reservations == 0U && state.queue_empty);
    close_started = r_std_net_tcp_listener_close(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(await_close(close_started, &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_connection(accept_started, &accept_result));
    R_TEST_CHECK(accept_result.r_tag == UINT32_C(1) &&
                 accept_result.error.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    return 0;
}

static int test_expired_deadline_still_drains(void) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult accept_result = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTaskStartResult accept_started;
    RStdNetTcpListenerStorage *storage;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    accept_started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(accept_started.is_ok && accept_started.task != NULL);
    R_TEST_CHECK(await_close(r_std_net_tcp_listener_close(&listener.value, expired), &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(1) && closed.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 closed.error.native_code == 0);
    R_TEST_CHECK(await_connection(accept_started, &accept_result));
    R_TEST_CHECK(accept_result.r_tag == UINT32_C(1) &&
                 accept_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    return 0;
}

static int test_preexisting_failure_precedes_expired_deadline(void) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetTcpListenerResult listener = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTcpListenerStorage *storage;
    int descriptor;

    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    R_TEST_CHECK(close(descriptor) == 0);
    R_TEST_CHECK(await_close(r_std_net_tcp_listener_close(&listener.value, expired), &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(1) && closed.error.code == R_STD_NET_ERROR_CLOSED &&
                 closed.error.native_code == EBADF);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    return 0;
}

static int test_cancel_close_races_do_not_skip_cleanup(void) {
    size_t iteration;

    for (iteration = 0U; iteration < 32U; ++iteration) {
        RStdNetTcpListenerResult listener = {0};
        RStdNetTcpConnectionResult first_result = {0};
        RStdNetTcpConnectionResult second_result = {0};
        RStdNetTaskStartResult first;
        RStdNetTaskStartResult second;
        RStdNetTaskStartResult close_started;
        RStdNetTcpListenerStorage *storage;
        int descriptor;
        int sentinel;

        R_TEST_CHECK(create_listener(&listener));
        storage = listener.value.storage;
        descriptor = storage->handle.descriptor;
        R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
        first = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
        second = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
        R_TEST_CHECK(first.is_ok && second.is_ok);
        close_started = r_std_net_tcp_listener_close(&listener.value, (RStdNetDeadline){0});
        R_TEST_CHECK(close_started.is_ok && close_started.task != NULL &&
                     listener.value.storage == NULL);
        r_runtime_task_cancel(&close_started.task);
        R_TEST_CHECK(close_started.task == NULL);
        R_TEST_CHECK(await_connection(first, &first_result));
        R_TEST_CHECK(await_connection(second, &second_result));
        R_TEST_CHECK(first_result.r_tag == UINT32_C(1) &&
                     first_result.error.code == R_STD_NET_ERROR_CANCELLED);
        R_TEST_CHECK(second_result.r_tag == UINT32_C(1) &&
                     second_result.error.code == R_STD_NET_ERROR_CANCELLED);
        R_TEST_CHECK(wait_for_closed_state(storage, 1U));
        errno = 0;
        R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
        sentinel = open("/dev/null", O_RDONLY);
        R_TEST_CHECK(sentinel >= 0);
        r_library_internal_net_handle_release(&storage->handle);
        R_TEST_CHECK(fcntl(sentinel, F_GETFD) >= 0);
        R_TEST_CHECK(close(sentinel) == 0);
    }
    return 0;
}

static int test_deadline_before_cancel_wins_while_drain_is_paused(void) {
    const RStdTimeDuration delay = {0, UINT32_C(20000000)};
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult accept_result = {0};
    RStdNetTaskStartResult accept_started;
    RStdNetTaskStartResult close_started;
    RStdNetTcpListenerStorage *storage;

    r_library_internal_net_listener_close_testing_reset();
    r_library_internal_net_listener_close_testing_pause_before_drain_selection(1);
    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    accept_started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(accept_started.is_ok && accept_started.task != NULL);
    close_started = r_std_net_tcp_listener_close(&listener.value, deadline_after(delay));
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL);
    r_library_internal_net_listener_close_testing_wait_before_drain_selection();
    r_library_internal_net_listener_close_testing_wait_deadline_reported();
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_DEADLINE);
    r_runtime_task_cancel(&close_started.task);
    R_TEST_CHECK(close_started.task == NULL);
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_DEADLINE);
    r_library_internal_net_listener_close_testing_pause_before_drain_selection(0);
    R_TEST_CHECK(await_connection(accept_started, &accept_result));
    R_TEST_CHECK(accept_result.r_tag == UINT32_C(1) &&
                 accept_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    r_library_internal_net_listener_close_testing_reset();
    return 0;
}

static int test_cancel_before_deadline_wins_while_drain_is_paused(void) {
    const RStdTimeDuration delay = {0, UINT32_C(50000000)};
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult accept_result = {0};
    RStdNetTaskStartResult accept_started;
    RStdNetTaskStartResult close_started;
    RStdNetTcpListenerStorage *storage;

    r_library_internal_net_listener_close_testing_reset();
    r_library_internal_net_listener_close_testing_pause_before_drain_selection(1);
    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    accept_started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(accept_started.is_ok && accept_started.task != NULL);
    close_started = r_std_net_tcp_listener_close(&listener.value, deadline_after(delay));
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL);
    r_library_internal_net_listener_close_testing_wait_before_drain_selection();
    r_runtime_task_cancel(&close_started.task);
    R_TEST_CHECK(close_started.task == NULL);
    r_library_internal_net_listener_close_testing_wait_cancel_reported();
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_TASK_CANCEL);
    r_library_internal_net_listener_close_testing_wait_deadline_reported();
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_TASK_CANCEL);
    r_library_internal_net_listener_close_testing_pause_before_drain_selection(0);
    R_TEST_CHECK(await_connection(accept_started, &accept_result));
    R_TEST_CHECK(accept_result.r_tag == UINT32_C(1) &&
                 accept_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    r_library_internal_net_listener_close_testing_reset();
    return 0;
}

static int test_close_before_late_deadline_remains_success(void) {
    const RStdTimeDuration delay = {0, UINT32_C(20000000)};
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult accept_result = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTaskStartResult accept_started;
    RStdNetTaskStartResult close_started;
    RStdNetTcpListenerStorage *storage;

    r_library_internal_net_listener_close_testing_reset();
    r_library_internal_net_listener_close_testing_pause_after_close_selection(1);
    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    accept_started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(accept_started.is_ok && accept_started.task != NULL);
    close_started = r_std_net_tcp_listener_close(&listener.value, deadline_after(delay));
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL);
    r_library_internal_net_listener_close_testing_wait_after_close_selection();
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_SUCCESS);
    r_library_internal_net_listener_close_testing_wait_deadline_reported();
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_SUCCESS);
    r_library_internal_net_listener_close_testing_pause_after_close_selection(0);
    R_TEST_CHECK(await_close(close_started, &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_connection(accept_started, &accept_result));
    R_TEST_CHECK(accept_result.r_tag == UINT32_C(1) &&
                 accept_result.error.code == R_STD_NET_ERROR_CANCELLED);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    r_library_internal_net_listener_close_testing_reset();
    return 0;
}

static int test_prestart_cancel_does_not_hide_earlier_deadline(void) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetTcpListenerResult listener = {0};
    RStdNetVoidResult closed = {0};
    RStdNetTcpListenerStorage *storage;
    RTestCloseThreadContext close_context = {0};
    RTestStopThreadContext stop_context = {0};
    pthread_t close_thread;
    pthread_t stop_thread;

    r_library_internal_net_listener_close_testing_reset();
    r_library_internal_net_listener_close_testing_pause_before_start_selection(1);
    R_TEST_CHECK(create_listener(&listener));
    storage = listener.value.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    close_context.listener = &listener.value;
    close_context.deadline = expired;
    R_TEST_CHECK(pthread_create(&close_thread, NULL, start_close_thread, &close_context) == 0);
    r_library_internal_net_listener_close_testing_wait_before_start_selection();
    R_TEST_CHECK(pthread_create(&stop_thread, NULL, stop_executor_thread, &stop_context) == 0);
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_wait_prestart_cancellation());
    r_library_internal_net_listener_close_testing_pause_before_start_selection(0);
    R_TEST_CHECK(pthread_join(close_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0);
    R_TEST_CHECK(stop_context.stopped);
    R_TEST_CHECK(close_context.started.is_ok && close_context.started.task != NULL);
    R_TEST_CHECK(listener.value.storage == NULL);
    R_TEST_CHECK(await_close(close_context.started, &closed));
    R_TEST_CHECK(closed.r_tag == UINT32_C(1) && closed.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 closed.error.native_code == 0);
    R_TEST_CHECK(r_library_internal_net_listener_close_testing_selected_outcome() ==
                 R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_DEADLINE);
    R_TEST_CHECK(wait_for_closed_state(storage, 1U));
    r_library_internal_net_handle_release(&storage->handle);
    r_library_internal_net_listener_close_testing_reset();
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RStdNetTcpListenerResult stopped_listener = {0};
    RStdNetTcpListenerStorage *unchanged;
    RStdNetTaskStartResult stopped;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_start_failures_preserve_owner(&allocator) == 0);
    R_TEST_CHECK(test_deadline_start_failures_preserve_owner(&allocator) == 0);
    R_TEST_CHECK(test_empty_close() == 0);
    R_TEST_CHECK(test_close_drains_accepts_and_rejects_new_operations() == 0);
    R_TEST_CHECK(test_immediate_accept_does_not_block_close() == 0);
    R_TEST_CHECK(test_expired_deadline_still_drains() == 0);
    R_TEST_CHECK(test_preexisting_failure_precedes_expired_deadline() == 0);
    R_TEST_CHECK(test_cancel_close_races_do_not_skip_cleanup() == 0);
    R_TEST_CHECK(test_deadline_before_cancel_wins_while_drain_is_paused() == 0);
    R_TEST_CHECK(test_cancel_before_deadline_wins_while_drain_is_paused() == 0);
    R_TEST_CHECK(test_close_before_late_deadline_remains_success() == 0);
    R_TEST_CHECK(create_listener(&stopped_listener));
    unchanged = stopped_listener.value.storage;
    R_TEST_CHECK(test_prestart_cancel_does_not_hide_earlier_deadline() == 0);

    stopped = r_std_net_tcp_listener_close(&stopped_listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(!stopped.is_ok && stopped.task == NULL);
    R_TEST_CHECK(stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    R_TEST_CHECK(stopped_listener.value.storage == unchanged);
    r_std_net_tcp_listener_destroy(&stopped_listener.value);
    (void)puts("library_net_listener_close_tests: ok");
    return EXIT_SUCCESS;
}

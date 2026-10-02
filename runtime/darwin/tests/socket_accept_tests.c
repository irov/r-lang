#include "r_runtime_allocator.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct TestAcceptCompletion {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinSocketAccept *request;
    RRuntimeDarwinSocketAcceptResult result;
    int descriptor;
    _Bool completed;
} TestAcceptCompletion;

typedef struct TestListener {
    int descriptor;
    struct sockaddr_in address;
} TestListener;

typedef struct TestAcceptLateCancel {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinSocketAccept *request;
    uint64_t sequence;
    _Bool sequence_ready;
    _Bool proceed;
    _Bool selected;
} TestAcceptLateCancel;

static void completion_initialize(TestAcceptCompletion *completion) {
    *completion = (TestAcceptCompletion){0};
    completion->descriptor = -1;
    if (pthread_mutex_init(&completion->mutex, NULL) != 0 ||
        pthread_cond_init(&completion->condition, NULL) != 0) {
        abort();
    }
}

static void completion_destroy(TestAcceptCompletion *completion) {
    if (completion->request != NULL) {
        r_runtime_darwin_socket_accept_release(completion->request);
        completion->request = NULL;
    }
    if (completion->descriptor >= 0) {
        (void)close(completion->descriptor);
        completion->descriptor = -1;
    }
    if (pthread_cond_destroy(&completion->condition) != 0 ||
        pthread_mutex_destroy(&completion->mutex) != 0) {
        abort();
    }
}

static void accept_completed(RRuntimeDarwinSocketAccept *request, void *context) {
    TestAcceptCompletion *completion = context;
    RRuntimeDarwinSocketAcceptResult result = r_runtime_darwin_socket_accept_result(request);
    int descriptor = r_runtime_darwin_socket_accept_take_descriptor(request);

    if (pthread_mutex_lock(&completion->mutex) != 0) {
        abort();
    }
    completion->request = request;
    completion->result = result;
    completion->descriptor = descriptor;
    completion->completed = 1;
    if (pthread_cond_broadcast(&completion->condition) != 0 ||
        pthread_mutex_unlock(&completion->mutex) != 0) {
        abort();
    }
}

static void completion_wait(TestAcceptCompletion *completion) {
    if (pthread_mutex_lock(&completion->mutex) != 0) {
        abort();
    }
    while (!completion->completed) {
        if (pthread_cond_wait(&completion->condition, &completion->mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&completion->mutex) != 0) {
        abort();
    }
}

static _Bool accept_results_equal(const RRuntimeDarwinSocketAcceptResult *left,
                                  const RRuntimeDarwinSocketAcceptResult *right) {
    if (left->terminal_event != right->terminal_event ||
        left->native_error != right->native_error ||
        left->terminal_event_sequence != right->terminal_event_sequence ||
        left->peer_length != right->peer_length || left->accepted != right->accepted) {
        return 0;
    }
    return left->peer_length == 0U ||
           memcmp(&left->peer, &right->peer, (size_t)left->peer_length) == 0;
}

static void late_cancel_initialize(TestAcceptLateCancel *late_cancel,
                                   RRuntimeDarwinSocketAccept *request) {
    *late_cancel = (TestAcceptLateCancel){0};
    late_cancel->request = request;
    if (pthread_mutex_init(&late_cancel->mutex, NULL) != 0 ||
        pthread_cond_init(&late_cancel->condition, NULL) != 0) {
        abort();
    }
}

static void late_cancel_destroy(TestAcceptLateCancel *late_cancel) {
    if (pthread_cond_destroy(&late_cancel->condition) != 0 ||
        pthread_mutex_destroy(&late_cancel->mutex) != 0) {
        abort();
    }
}

static void *late_cancel_run(void *context) {
    TestAcceptLateCancel *late_cancel = context;
    uint64_t sequence = r_runtime_darwin_event_sequence_next();

    if (pthread_mutex_lock(&late_cancel->mutex) != 0) {
        abort();
    }
    late_cancel->sequence = sequence;
    late_cancel->sequence_ready = 1;
    if (pthread_cond_broadcast(&late_cancel->condition) != 0) {
        abort();
    }
    while (!late_cancel->proceed) {
        if (pthread_cond_wait(&late_cancel->condition, &late_cancel->mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&late_cancel->mutex) != 0) {
        abort();
    }
    late_cancel->selected = r_runtime_darwin_socket_accept_cancel(late_cancel->request, sequence);
    return NULL;
}

static void late_cancel_wait_ready(TestAcceptLateCancel *late_cancel) {
    if (pthread_mutex_lock(&late_cancel->mutex) != 0) {
        abort();
    }
    while (!late_cancel->sequence_ready) {
        if (pthread_cond_wait(&late_cancel->condition, &late_cancel->mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&late_cancel->mutex) != 0) {
        abort();
    }
}

static void late_cancel_proceed(TestAcceptLateCancel *late_cancel) {
    if (pthread_mutex_lock(&late_cancel->mutex) != 0) {
        abort();
    }
    late_cancel->proceed = 1;
    if (pthread_cond_broadcast(&late_cancel->condition) != 0 ||
        pthread_mutex_unlock(&late_cancel->mutex) != 0) {
        abort();
    }
}

static int set_runtime_descriptor_flags(int descriptor) {
    const int descriptor_flags = fcntl(descriptor, F_GETFD);
    const int status_flags = fcntl(descriptor, F_GETFL);

    return descriptor_flags >= 0 && status_flags >= 0 &&
           fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0 &&
           fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) == 0;
}

static int listener_create(TestListener *listener, _Bool runtime_flags) {
    socklen_t length = (socklen_t)sizeof(listener->address);

    *listener = (TestListener){-1, {0}};
    listener->descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (listener->descriptor < 0 ||
        (runtime_flags && !set_runtime_descriptor_flags(listener->descriptor))) {
        if (listener->descriptor >= 0) {
            (void)close(listener->descriptor);
        }
        listener->descriptor = -1;
        return 0;
    }
    listener->address.sin_family = AF_INET;
    listener->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener->descriptor,
             (const struct sockaddr *)&listener->address,
             (socklen_t)sizeof(listener->address)) != 0 ||
        listen(listener->descriptor, 8) != 0 ||
        getsockname(listener->descriptor, (struct sockaddr *)&listener->address, &length) != 0) {
        (void)close(listener->descriptor);
        listener->descriptor = -1;
        return 0;
    }
    return 1;
}

static int client_connect(const TestListener *listener, struct sockaddr_in *local) {
    socklen_t length = (socklen_t)sizeof(*local);
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);

    if (descriptor < 0 ||
        connect(descriptor,
                (const struct sockaddr *)&listener->address,
                (socklen_t)sizeof(listener->address)) != 0 ||
        getsockname(descriptor, (struct sockaddr *)local, &length) != 0) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return -1;
    }
    return descriptor;
}

static int test_success(RRuntimeAllocator *allocator) {
    TestListener listener;
    TestAcceptCompletion completion;
    RRuntimeDarwinSocketAcceptPrepareResult prepared;
    struct sockaddr_in client_local = {0};
    const struct sockaddr_in *peer;
    int client_descriptor;
    int descriptor_flags;
    int status_flags;
#if defined(SO_NOSIGPIPE)
    socklen_t no_sigpipe_length = (socklen_t)sizeof(int);
    int no_sigpipe = 0;
#endif

    CHECK(listener_create(&listener, 1));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_accept_bind(prepared.request, accept_completed, &completion));
    CHECK(r_runtime_darwin_socket_accept_activate(prepared.request));
    client_descriptor = client_connect(&listener, &client_local);
    CHECK(client_descriptor >= 0);
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE);
    CHECK(completion.result.accepted && completion.result.native_error == 0);
    CHECK(completion.result.terminal_event_sequence != UINT64_C(0));
    CHECK((size_t)completion.result.peer_length >= sizeof(struct sockaddr_in));
    CHECK(completion.result.peer.ss_family == AF_INET);
    peer = (const struct sockaddr_in *)&completion.result.peer;
    CHECK(peer->sin_port == client_local.sin_port);
    CHECK(peer->sin_addr.s_addr == client_local.sin_addr.s_addr);
    CHECK(completion.descriptor >= 0);
    descriptor_flags = fcntl(completion.descriptor, F_GETFD);
    status_flags = fcntl(completion.descriptor, F_GETFL);
    CHECK(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0);
    CHECK(status_flags >= 0 && (status_flags & O_NONBLOCK) != 0);
#if defined(SO_NOSIGPIPE)
    CHECK(getsockopt(
              completion.descriptor, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, &no_sigpipe_length) ==
          0);
    CHECK(no_sigpipe == 1);
#endif
    CHECK(close(client_descriptor) == 0);
    CHECK(close(listener.descriptor) == 0);
    completion_destroy(&completion);
    return 0;
}

static int test_cancel_before_activation(RRuntimeAllocator *allocator) {
    TestListener listener;
    TestAcceptCompletion completion;
    RRuntimeDarwinSocketAcceptPrepareResult prepared;
    uint64_t sequence;

    CHECK(listener_create(&listener, 1));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_accept_bind(prepared.request, accept_completed, &completion));
    sequence = r_runtime_darwin_event_sequence_next();
    CHECK(r_runtime_darwin_socket_accept_cancel(prepared.request, sequence));
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED);
    CHECK(completion.result.terminal_event_sequence == sequence);
    CHECK(!completion.result.accepted && completion.descriptor < 0);
    CHECK(close(listener.descriptor) == 0);
    completion_destroy(&completion);
    return 0;
}

static int test_deadline(RRuntimeAllocator *allocator) {
    TestListener listener;
    TestAcceptCompletion completion;
    RRuntimeDarwinSocketAcceptPrepareResult prepared;

    CHECK(listener_create(&listener, 1));
    completion_initialize(&completion);
    prepared =
        r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, UINT64_C(2000000));
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_accept_bind(prepared.request, accept_completed, &completion));
    CHECK(r_runtime_darwin_socket_accept_arm_deadline(prepared.request));
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT);
    CHECK(completion.result.native_error == 0);
    CHECK(completion.result.terminal_event_sequence != UINT64_C(0));
    CHECK(!completion.result.accepted && completion.descriptor < 0);
    CHECK(close(listener.descriptor) == 0);
    completion_destroy(&completion);
    return 0;
}

static int test_cancel_native_races(RRuntimeAllocator *allocator) {
    size_t iteration;

    for (iteration = 0U; iteration < 32U; ++iteration) {
        TestListener listener;
        TestAcceptCompletion completion;
        RRuntimeDarwinSocketAcceptPrepareResult prepared;
        RRuntimeDarwinSocketAcceptResult observed;
        struct sockaddr_in client_local = {0};
        uint64_t cancellation_sequence;
        int client_descriptor;
        _Bool cancellation_selected;

        CHECK(listener_create(&listener, 1));
        completion_initialize(&completion);
        prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
        CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
        CHECK(r_runtime_darwin_socket_accept_bind(prepared.request, accept_completed, &completion));
        CHECK(r_runtime_darwin_socket_accept_activate(prepared.request));
        client_descriptor = client_connect(&listener, &client_local);
        CHECK(client_descriptor >= 0);
        cancellation_sequence = r_runtime_darwin_event_sequence_next();
        cancellation_selected =
            r_runtime_darwin_socket_accept_cancel(prepared.request, cancellation_sequence);
        completion_wait(&completion);
        observed = r_runtime_darwin_socket_accept_result(completion.request);
        CHECK(accept_results_equal(&completion.result, &observed));
        if (completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE) {
            CHECK(completion.result.accepted && completion.descriptor >= 0);
            CHECK(!cancellation_selected);
        } else {
            CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED);
            CHECK(completion.result.terminal_event_sequence == cancellation_sequence);
            CHECK(!completion.result.accepted && completion.descriptor < 0);
            CHECK(cancellation_selected);
        }
        CHECK(close(client_descriptor) == 0);
        CHECK(close(listener.descriptor) == 0);
        completion_destroy(&completion);
    }
    return 0;
}

static int test_late_cancel_does_not_replace_delivered_result(RRuntimeAllocator *allocator) {
    TestListener listener;
    TestAcceptCompletion completion;
    TestAcceptLateCancel late_cancel;
    RRuntimeDarwinSocketAcceptPrepareResult prepared;
    RRuntimeDarwinSocketAcceptResult observed;
    struct sockaddr_in client_local = {0};
    pthread_t thread;
    int client_descriptor;

    CHECK(listener_create(&listener, 1));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_accept_bind(prepared.request, accept_completed, &completion));
    late_cancel_initialize(&late_cancel, prepared.request);
    CHECK(pthread_create(&thread, NULL, late_cancel_run, &late_cancel) == 0);
    late_cancel_wait_ready(&late_cancel);
    CHECK(r_runtime_darwin_socket_accept_activate(prepared.request));
    client_descriptor = client_connect(&listener, &client_local);
    CHECK(client_descriptor >= 0);
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE);
    CHECK(completion.result.accepted && completion.descriptor >= 0);
    CHECK(completion.result.terminal_event_sequence > late_cancel.sequence);
    late_cancel_proceed(&late_cancel);
    CHECK(pthread_join(thread, NULL) == 0);
    observed = r_runtime_darwin_socket_accept_result(completion.request);
    CHECK(!late_cancel.selected);
    CHECK(accept_results_equal(&completion.result, &observed));
    CHECK(r_runtime_darwin_socket_accept_take_descriptor(completion.request) == -1);
    CHECK(close(client_descriptor) == 0);
    CHECK(close(listener.descriptor) == 0);
    late_cancel_destroy(&late_cancel);
    completion_destroy(&completion);
    return 0;
}

static int test_abort_validation_and_allocation_failure(RRuntimeAllocator *allocator) {
    TestListener listener;
    RRuntimeDarwinSocketAcceptPrepareResult prepared;

    CHECK(listener_create(&listener, 1));
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    r_runtime_darwin_socket_accept_abort(&prepared.request);
    CHECK(prepared.request == NULL);

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED);
    CHECK(prepared.request == NULL && prepared.native_error == ENOMEM);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    CHECK(close(listener.descriptor) == 0);

    CHECK(listener_create(&listener, 0));
    prepared = r_runtime_darwin_socket_accept_prepare(allocator, listener.descriptor, 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID);
    CHECK(prepared.request == NULL && prepared.native_error == 0);
    CHECK(close(listener.descriptor) == 0);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    CHECK(test_abort_validation_and_allocation_failure(&allocator) == 0);
    CHECK(test_success(&allocator) == 0);
    CHECK(test_cancel_before_activation(&allocator) == 0);
    CHECK(test_deadline(&allocator) == 0);
    CHECK(test_cancel_native_races(&allocator) == 0);
    CHECK(test_late_cancel_does_not_replace_delivered_result(&allocator) == 0);
    (void)puts("socket_accept_tests: ok");
    return EXIT_SUCCESS;
}

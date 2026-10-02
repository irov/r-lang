#include "r_runtime_allocator.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
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

typedef struct TestCompletion {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinSocketConnect *request;
    RRuntimeDarwinSocketConnectResult result;
    int descriptor;
    _Bool completed;
} TestCompletion;

typedef struct TestServer {
    int descriptor;
    struct sockaddr_in address;
} TestServer;

typedef struct TestConnectLateCancel {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinSocketConnect *request;
    uint64_t sequence;
    _Bool sequence_ready;
    _Bool proceed;
    _Bool selected;
} TestConnectLateCancel;

static void completion_initialize(TestCompletion *completion) {
    (void)memset(completion, 0, sizeof(*completion));
    completion->descriptor = -1;
    if (pthread_mutex_init(&completion->mutex, NULL) != 0 ||
        pthread_cond_init(&completion->condition, NULL) != 0) {
        abort();
    }
}

static void completion_destroy(TestCompletion *completion) {
    if (completion->request != NULL) {
        r_runtime_darwin_socket_connect_release(completion->request);
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

static void connect_completed(RRuntimeDarwinSocketConnect *request, void *context) {
    TestCompletion *completion = context;
    RRuntimeDarwinSocketConnectResult result = r_runtime_darwin_socket_connect_result(request);
    int descriptor = r_runtime_darwin_socket_connect_take_descriptor(request);

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

static _Bool connect_results_equal(const RRuntimeDarwinSocketConnectResult *left,
                                   const RRuntimeDarwinSocketConnectResult *right) {
    return left->terminal_event == right->terminal_event &&
           left->native_error == right->native_error &&
           left->terminal_event_sequence == right->terminal_event_sequence &&
           left->connected == right->connected;
}

static void late_cancel_initialize(TestConnectLateCancel *late_cancel,
                                   RRuntimeDarwinSocketConnect *request) {
    (void)memset(late_cancel, 0, sizeof(*late_cancel));
    late_cancel->request = request;
    if (pthread_mutex_init(&late_cancel->mutex, NULL) != 0 ||
        pthread_cond_init(&late_cancel->condition, NULL) != 0) {
        abort();
    }
}

static void late_cancel_destroy(TestConnectLateCancel *late_cancel) {
    if (pthread_cond_destroy(&late_cancel->condition) != 0 ||
        pthread_mutex_destroy(&late_cancel->mutex) != 0) {
        abort();
    }
}

static void *late_cancel_run(void *context) {
    TestConnectLateCancel *late_cancel = context;
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
    late_cancel->selected = r_runtime_darwin_socket_connect_cancel(late_cancel->request, sequence);
    return NULL;
}

static void late_cancel_wait_ready(TestConnectLateCancel *late_cancel) {
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

static void late_cancel_proceed(TestConnectLateCancel *late_cancel) {
    if (pthread_mutex_lock(&late_cancel->mutex) != 0) {
        abort();
    }
    late_cancel->proceed = 1;
    if (pthread_cond_broadcast(&late_cancel->condition) != 0 ||
        pthread_mutex_unlock(&late_cancel->mutex) != 0) {
        abort();
    }
}

static void completion_wait(TestCompletion *completion) {
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

static int server_create(TestServer *server) {
    socklen_t length = (socklen_t)sizeof(server->address);

    (void)memset(server, 0, sizeof(*server));
    server->descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server->descriptor < 0) {
        return 0;
    }
    server->address.sin_family = AF_INET;
    server->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server->descriptor, (const struct sockaddr *)&server->address, length) != 0 ||
        listen(server->descriptor, 4) != 0 ||
        getsockname(server->descriptor, (struct sockaddr *)&server->address, &length) != 0) {
        (void)close(server->descriptor);
        server->descriptor = -1;
        return 0;
    }
    return 1;
}

static int test_success(RRuntimeAllocator *allocator) {
    TestServer server;
    TestCompletion completion;
    RRuntimeDarwinSocketPrepareResult prepared;
    int accepted;
    int no_sigpipe = 0;
    socklen_t no_sigpipe_size = (socklen_t)sizeof(no_sigpipe);

    CHECK(server_create(&server));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&server.address, (socklen_t)sizeof(server.address), 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_connect_bind(prepared.request, connect_completed, &completion));
    CHECK(r_runtime_darwin_socket_connect_activate(prepared.request));
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE);
    CHECK(completion.result.connected && completion.result.native_error == 0);
    CHECK(completion.result.terminal_event_sequence != UINT64_C(0));
    CHECK(completion.descriptor >= 0);
#if defined(SO_NOSIGPIPE)
    CHECK(getsockopt(
              completion.descriptor, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, &no_sigpipe_size) == 0);
    CHECK(no_sigpipe == 1);
#else
    (void)no_sigpipe;
    (void)no_sigpipe_size;
#endif
    accepted = accept(server.descriptor, NULL, NULL);
    CHECK(accepted >= 0);
    CHECK(close(accepted) == 0);
    CHECK(close(server.descriptor) == 0);
    completion_destroy(&completion);
    return 0;
}

static int test_cancel_before_submission(RRuntimeAllocator *allocator) {
    TestServer server;
    TestCompletion completion;
    RRuntimeDarwinSocketPrepareResult prepared;
    uint64_t sequence;

    CHECK(server_create(&server));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&server.address, (socklen_t)sizeof(server.address), 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_connect_bind(prepared.request, connect_completed, &completion));
    sequence = r_runtime_darwin_event_sequence_next();
    CHECK(r_runtime_darwin_socket_connect_cancel(prepared.request, sequence));
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED);
    CHECK(completion.result.terminal_event_sequence == sequence);
    CHECK(!completion.result.connected && completion.descriptor < 0);
    CHECK(close(server.descriptor) == 0);
    completion_destroy(&completion);
    return 0;
}

static int test_late_cancel_does_not_replace_delivered_result(RRuntimeAllocator *allocator) {
    TestServer server;
    TestCompletion completion;
    TestConnectLateCancel late_cancel;
    RRuntimeDarwinSocketPrepareResult prepared;
    RRuntimeDarwinSocketConnectResult observed;
    pthread_t thread;
    int accepted;

    CHECK(server_create(&server));
    completion_initialize(&completion);
    prepared = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&server.address, (socklen_t)sizeof(server.address), 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    CHECK(r_runtime_darwin_socket_connect_bind(prepared.request, connect_completed, &completion));
    late_cancel_initialize(&late_cancel, prepared.request);
    CHECK(pthread_create(&thread, NULL, late_cancel_run, &late_cancel) == 0);
    late_cancel_wait_ready(&late_cancel);
    CHECK(r_runtime_darwin_socket_connect_activate(prepared.request));
    completion_wait(&completion);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE);
    CHECK(completion.result.connected && completion.descriptor >= 0);
    CHECK(completion.result.terminal_event_sequence > late_cancel.sequence);
    late_cancel_proceed(&late_cancel);
    CHECK(pthread_join(thread, NULL) == 0);
    observed = r_runtime_darwin_socket_connect_result(completion.request);
    CHECK(!late_cancel.selected);
    CHECK(connect_results_equal(&completion.result, &observed));
    CHECK(r_runtime_darwin_socket_connect_take_descriptor(completion.request) == -1);
    accepted = accept(server.descriptor, NULL, NULL);
    CHECK(accepted >= 0);
    CHECK(close(accepted) == 0);
    CHECK(close(server.descriptor) == 0);
    late_cancel_destroy(&late_cancel);
    completion_destroy(&completion);
    return 0;
}

static int test_abort_and_allocation_failure(RRuntimeAllocator *allocator) {
    TestServer server;
    RRuntimeDarwinSocketPrepareResult prepared;

    CHECK(server_create(&server));
    prepared = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&server.address, (socklen_t)sizeof(server.address), 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_OK && prepared.request != NULL);
    r_runtime_darwin_socket_connect_abort(&prepared.request);
    CHECK(prepared.request == NULL);

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    prepared = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&server.address, (socklen_t)sizeof(server.address), 0U);
    CHECK(prepared.status == R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED);
    CHECK(prepared.request == NULL && prepared.native_error == ENOMEM);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    CHECK(close(server.descriptor) == 0);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    CHECK(test_abort_and_allocation_failure(&allocator) == 0);
    CHECK(test_success(&allocator) == 0);
    CHECK(test_cancel_before_submission(&allocator) == 0);
    CHECK(test_late_cancel_does_not_replace_delivered_result(&allocator) == 0);
    (void)puts("socket_tests: ok");
    return EXIT_SUCCESS;
}

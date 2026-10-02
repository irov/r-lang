#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef enum RLibraryNetBindMode {
    R_LIBRARY_NET_BIND_TCP_LISTENER = 0,
    R_LIBRARY_NET_BIND_UDP_SOCKET = 1,
    /* R-SLIB-NET-0014, R-SLIB-NET-0017: Unix-domain listeners and datagram sockets. */
    R_LIBRARY_NET_BIND_UNIX_LISTENER = 2,
    R_LIBRARY_NET_BIND_UNIX_DATAGRAM = 3,
    R_LIBRARY_NET_BIND_UNIX_DATAGRAM_CONNECT = 4
} RLibraryNetBindMode;

typedef union RLibraryNetBindStorage {
    RStdNetTcpListenerStorage *listener;
    RStdNetUdpSocketStorage *udp_socket;
} RLibraryNetBindStorage;

typedef struct RLibraryNetBindPayload {
    RLibraryNetBindMode mode;
    RStdNetSocketAddress local;
    RStdNetListenOptions listen_options;
    /* The converted path of a Unix-domain mode; length zero is an invalid path. */
    struct sockaddr_un unix_path;
    socklen_t unix_path_length;
    _Bool replace;
    RStdNetDeadline deadline;
    RLibraryNetBindStorage storage;
} RLibraryNetBindPayload;

static void net_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static RStdNetTaskStartResult start_failure(RRuntimeTaskStartStatus status) {
    RStdNetTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        net_panic();
    }
    net_panic();
    return result;
}

static _Bool mode_is_listener(RLibraryNetBindMode mode) {
    return mode == R_LIBRARY_NET_BIND_TCP_LISTENER || mode == R_LIBRARY_NET_BIND_UNIX_LISTENER;
}

static _Bool mode_is_unix(RLibraryNetBindMode mode) {
    return mode == R_LIBRARY_NET_BIND_UNIX_LISTENER || mode == R_LIBRARY_NET_BIND_UNIX_DATAGRAM ||
           mode == R_LIBRARY_NET_BIND_UNIX_DATAGRAM_CONNECT;
}

static RLibraryNetHandleStorage *payload_handle(RLibraryNetBindPayload *payload) {
    if (mode_is_listener(payload->mode)) {
        return payload->storage.listener == NULL ? NULL : &payload->storage.listener->handle;
    }
    return payload->storage.udp_socket == NULL ? NULL : &payload->storage.udp_socket->handle;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetBindPayload *destination = destination_pointer;
    RLibraryNetBindPayload *source = source_pointer;

    *destination = *source;
    source->storage.listener = NULL;
}

static void payload_drop(void *value) {
    RLibraryNetBindPayload *payload = value;

    r_library_internal_net_handle_release(payload_handle(payload));
    payload->storage.listener = NULL;
}

static void listener_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpListenerResult *destination = destination_pointer;
    RStdNetTcpListenerResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void listener_result_drop(void *value) {
    RStdNetTcpListenerResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_tcp_listener_drop(&result->r_payload.r_ok);
    }
}

static void udp_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUdpSocketResult *destination = destination_pointer;
    RStdNetUdpSocketResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void udp_result_drop(void *value) {
    RStdNetUdpSocketResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_udp_socket_drop(&result->r_payload.r_ok);
    }
}

static void unix_listener_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUnixListenerResult *destination = destination_pointer;
    RStdNetUnixListenerResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void unix_listener_result_drop(void *value) {
    RStdNetUnixListenerResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_unix_listener_drop(&result->r_payload.r_ok);
    }
}

static void unix_datagram_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUnixDatagramResult *destination = destination_pointer;
    RStdNetUnixDatagramResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void unix_datagram_result_drop(void *value) {
    RStdNetUnixDatagramResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_unix_datagram_drop(&result->r_payload.r_ok);
    }
}

static RRuntimeTypeInfo result_type(RLibraryNetBindMode mode) {
    if (mode == R_LIBRARY_NET_BIND_TCP_LISTENER) {
        return (RRuntimeTypeInfo){
            sizeof(RStdNetTcpListenerResult),
            _Alignof(RStdNetTcpListenerResult),
            listener_result_move,
            listener_result_drop,
        };
    }
    if (mode == R_LIBRARY_NET_BIND_UNIX_LISTENER) {
        return (RRuntimeTypeInfo){
            sizeof(RStdNetUnixListenerResult),
            _Alignof(RStdNetUnixListenerResult),
            unix_listener_result_move,
            unix_listener_result_drop,
        };
    }
    if (mode_is_unix(mode)) {
        return (RRuntimeTypeInfo){
            sizeof(RStdNetUnixDatagramResult),
            _Alignof(RStdNetUnixDatagramResult),
            unix_datagram_result_move,
            unix_datagram_result_drop,
        };
    }
    return (RRuntimeTypeInfo){
        sizeof(RStdNetUdpSocketResult),
        _Alignof(RStdNetUdpSocketResult),
        udp_result_move,
        udp_result_drop,
    };
}

static int socket_create(int domain, int type, RStdNetError *error) {
    int descriptor;
    int descriptor_flags;
    int status_flags;
    int native_code;

    errno = 0;
    descriptor = socket(domain, type, 0);
    if (descriptor < 0) {
        *error = r_library_internal_net_error_from_native(errno);
        return -1;
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return -1;
    }
    status_flags = fcntl(descriptor, F_GETFL);
    if (status_flags < 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return -1;
    }
    if (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return -1;
    }
    if (fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return -1;
    }
    return descriptor;
}

static _Bool
socket_set_boolean(int descriptor, int level, int option, _Bool enabled, RStdNetError *error) {
    const int native_value = enabled ? 1 : 0;

    if (setsockopt(descriptor, level, option, &native_value, (socklen_t)sizeof(native_value)) ==
        0) {
        return 1;
    }
    *error = r_library_internal_net_error_from_native(errno);
    return 0;
}

static int listen_backlog(uint32_t backlog) {
    return backlog == 0U ? 128 : (backlog > (uint32_t)INT_MAX ? INT_MAX : (int)backlog);
}

static _Bool bind_native(RLibraryNetBindPayload *payload,
                         const struct sockaddr *native,
                         socklen_t native_length,
                         int domain,
                         RStdNetError *error) {
    const int type = payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER ? SOCK_STREAM : SOCK_DGRAM;
    const _Bool reuse_address = payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER
                                    ? payload->listen_options.reuse_address
                                    : payload->listen_options.reuse_address;
    int descriptor = socket_create(domain, type, error);
    int native_code;

    if (descriptor < 0) {
        return 0;
    }
    if (!socket_set_boolean(descriptor, SOL_SOCKET, SO_REUSEADDR, reuse_address, error)) {
        (void)close(descriptor);
        return 0;
    }
    if ((payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER) && (domain == AF_INET6) &&
        !socket_set_boolean(
            descriptor, IPPROTO_IPV6, IPV6_V6ONLY, payload->listen_options.v6_only, error)) {
        (void)close(descriptor);
        return 0;
    }
    errno = 0;
    if (bind(descriptor, native, native_length) != 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return 0;
    }
    if (payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER) {
        errno = 0;
        if (listen(descriptor, listen_backlog(payload->listen_options.backlog)) != 0) {
            native_code = errno;
            (void)close(descriptor);
            *error = r_library_internal_net_error_from_native(native_code);
            return 0;
        }
        r_library_internal_net_tcp_listener_publish(payload->storage.listener, descriptor);
    } else {
        r_library_internal_net_udp_socket_publish(payload->storage.udp_socket, descriptor);
    }
    return 1;
}

/* With replace, a socket file left at the path is removed; other files stay and fail bind. */
static void unix_remove_socket_file(const struct sockaddr_un *path) {
    struct stat status;

    if (lstat(path->sun_path, &status) == 0 && S_ISSOCK(status.st_mode)) {
        (void)unlink(path->sun_path);
    }
}

static _Bool unix_bind_native(RLibraryNetBindPayload *payload, RStdNetError *error) {
    const _Bool listener = payload->mode == R_LIBRARY_NET_BIND_UNIX_LISTENER;
    int descriptor = socket_create(AF_UNIX, listener ? SOCK_STREAM : SOCK_DGRAM, error);
    int native_code;

    if (descriptor < 0) {
        return 0;
    }
    errno = 0;
    if (payload->mode == R_LIBRARY_NET_BIND_UNIX_DATAGRAM_CONNECT) {
        if (connect(descriptor,
                    (const struct sockaddr *)&payload->unix_path,
                    payload->unix_path_length) != 0) {
            native_code = errno;
            (void)close(descriptor);
            *error = r_library_internal_net_error_from_native(native_code);
            return 0;
        }
        r_library_internal_net_udp_socket_publish(payload->storage.udp_socket, descriptor);
        return 1;
    }
    if (payload->replace) {
        unix_remove_socket_file(&payload->unix_path);
    }
    if (bind(descriptor, (const struct sockaddr *)&payload->unix_path, payload->unix_path_length) !=
            0 ||
        (listener && listen(descriptor, listen_backlog(payload->listen_options.backlog)) != 0)) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_library_internal_net_error_from_native(native_code);
        return 0;
    }
    if (listener) {
        r_library_internal_net_tcp_listener_publish(payload->storage.listener, descriptor);
    } else {
        r_library_internal_net_udp_socket_publish(payload->storage.udp_socket, descriptor);
    }
    return 1;
}

static void initialize_failure_result(RLibraryNetBindPayload *payload,
                                      void *result_pointer,
                                      RStdNetError error) {
    if (payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER) {
        RStdNetTcpListenerResult *result = result_pointer;

        *result = (RStdNetTcpListenerResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else if (payload->mode == R_LIBRARY_NET_BIND_UNIX_LISTENER) {
        RStdNetUnixListenerResult *result = result_pointer;

        *result = (RStdNetUnixListenerResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else if (mode_is_unix(payload->mode)) {
        RStdNetUnixDatagramResult *result = result_pointer;

        *result = (RStdNetUnixDatagramResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else {
        RStdNetUdpSocketResult *result = result_pointer;

        *result = (RStdNetUdpSocketResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    }
}

static void initialize_success_result(RLibraryNetBindPayload *payload, void *result_pointer) {
    if (payload->mode == R_LIBRARY_NET_BIND_TCP_LISTENER) {
        RStdNetTcpListenerResult *result = result_pointer;

        *result = (RStdNetTcpListenerResult){0};
        result->r_payload.r_ok.storage = payload->storage.listener;
        payload->storage.listener = NULL;
    } else if (payload->mode == R_LIBRARY_NET_BIND_UNIX_LISTENER) {
        RStdNetUnixListenerResult *result = result_pointer;

        *result = (RStdNetUnixListenerResult){0};
        result->r_payload.r_ok.storage = payload->storage.listener;
        payload->storage.listener = NULL;
    } else if (mode_is_unix(payload->mode)) {
        RStdNetUnixDatagramResult *result = result_pointer;

        *result = (RStdNetUnixDatagramResult){0};
        result->r_payload.r_ok.storage = payload->storage.udp_socket;
        payload->storage.udp_socket = NULL;
    } else {
        RStdNetUdpSocketResult *result = result_pointer;

        *result = (RStdNetUdpSocketResult){0};
        result->r_payload.r_ok.storage = payload->storage.udp_socket;
        payload->storage.udp_socket = NULL;
    }
}

static void finish_external(RRuntimeTaskExternalExecution *execution, _Bool native_committed) {
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();
    _Bool selected = r_runtime_task_external_try_select_completion_at(execution, event_sequence);

    if (native_committed && !selected) {
        selected = r_runtime_task_external_select_terminal_completion(execution);
    }
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        r_runtime_task_external_acknowledge(execution);
    }
}

static void bind_external_start(RRuntimeTaskExternalExecution *execution,
                                void *payload_pointer,
                                void *result_pointer) {
    RLibraryNetBindPayload *payload = payload_pointer;
    struct sockaddr_storage native = {0};
    socklen_t native_length = 0;
    int domain = 0;
    RStdNetError error = {0};
    uint64_t timeout_nanoseconds = 0U;
    _Bool committed;

    if (mode_is_unix(payload->mode) ? payload->unix_path_length == 0
                                     : !r_library_internal_net_address_to_native(
                                           payload->local, &native, &native_length, &domain)) {
        initialize_failure_result(
            payload, result_pointer, net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)));
        finish_external(execution, 0);
        return;
    }
    if (r_library_internal_net_deadline_timeout(payload->deadline, &timeout_nanoseconds, &error) !=
        R_LIBRARY_NET_DEADLINE_READY) {
        initialize_failure_result(payload, result_pointer, error);
        finish_external(execution, 0);
        return;
    }
    (void)timeout_nanoseconds;
    if (r_runtime_task_external_cancel_requested(execution)) {
        r_runtime_task_external_start_ready(execution);
        return;
    }
    committed =
        mode_is_unix(payload->mode)
            ? unix_bind_native(payload, &error)
            : bind_native(payload, (const struct sockaddr *)&native, native_length, domain, &error);
    if (committed) {
        initialize_success_result(payload, result_pointer);
    } else {
        initialize_failure_result(payload, result_pointer, error);
    }
    finish_external(execution, committed);
}

static void bind_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload) {
    (void)payload;
    r_runtime_task_external_acknowledge(execution);
}

static RStdNetTaskStartResult bind_start(RLibraryNetBindPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetBindPayload),
        _Alignof(RLibraryNetBindPayload),
        payload_move,
        payload_drop,
    };
    RRuntimeTaskPrepareResult prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type(payload->mode), bind_external_start, bind_external_cancel);
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RStdNetTaskStartResult result = {0};

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(prepared.status);
    }
    allocator = r_runtime_task_start_allocator(prepared.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&prepared.transaction);
        net_panic();
    }
    if (mode_is_listener(payload->mode)) {
        payload->storage.listener = r_library_internal_net_tcp_listener_reserve(allocator);
    } else {
        payload->storage.udp_socket = r_library_internal_net_udp_socket_reserve(allocator);
    }
    if (payload_handle(payload) == NULL) {
        r_runtime_task_start_abort(&prepared.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    started = r_runtime_task_start_commit(&prepared.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        payload_drop(payload);
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_tcp_listen(RStdNetSocketAddress local,
                                                         RStdNetListenOptions options,
                                                         RStdNetDeadline deadline) {
    RLibraryNetBindPayload payload = {0};

    payload.mode = R_LIBRARY_NET_BIND_TCP_LISTENER;
    payload.local = local;
    payload.listen_options = options;
    payload.deadline = deadline;
    return bind_start(&payload);
}

RStdNetTaskStartResult r_library_internal_net_udp_bind(RStdNetSocketAddress local,
                                                       _Bool reuse_address,
                                                       RStdNetDeadline deadline) {
    RLibraryNetBindPayload payload = {0};

    payload.mode = R_LIBRARY_NET_BIND_UDP_SOCKET;
    payload.local = local;
    payload.listen_options.reuse_address = reuse_address;
    payload.deadline = deadline;
    return bind_start(&payload);
}

static RStdNetTaskStartResult unix_bind_start(RLibraryNetBindMode mode,
                                              RStdStringView path,
                                              uint32_t backlog,
                                              _Bool replace,
                                              RStdNetDeadline deadline) {
    RLibraryNetBindPayload payload = {0};

    payload.mode = mode;
    if (!r_library_internal_net_unix_address(path, &payload.unix_path, &payload.unix_path_length)) {
        payload.unix_path_length = 0;
    }
    payload.listen_options.backlog = backlog;
    payload.replace = replace;
    payload.deadline = deadline;
    return bind_start(&payload);
}

RStdNetTaskStartResult r_library_internal_net_unix_listen(RStdStringView path,
                                                          uint32_t backlog,
                                                          _Bool replace,
                                                          RStdNetDeadline deadline) {
    return unix_bind_start(R_LIBRARY_NET_BIND_UNIX_LISTENER, path, backlog, replace, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_datagram_bind(RStdStringView path,
                                                                 _Bool replace,
                                                                 RStdNetDeadline deadline) {
    return unix_bind_start(R_LIBRARY_NET_BIND_UNIX_DATAGRAM, path, 0U, replace, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_datagram_connect(RStdStringView path,
                                                                    RStdNetDeadline deadline) {
    return unix_bind_start(R_LIBRARY_NET_BIND_UNIX_DATAGRAM_CONNECT, path, 0U, 0, deadline);
}

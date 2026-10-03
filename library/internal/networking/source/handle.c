#include "r_library_net_internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static RStdNetError net_error(RStdNetErrorCode code, int native_code) {
    return (RStdNetError){code, (int64_t)native_code};
}

RStdNetError r_library_internal_net_error_from_native(int native_code) {
    switch (native_code) {
    case EAFNOSUPPORT:
#if defined(EPROTONOSUPPORT)
    case EPROTONOSUPPORT:
#endif
#if defined(ESOCKTNOSUPPORT)
    case ESOCKTNOSUPPORT:
#endif
#if defined(EPROTOTYPE)
    case EPROTOTYPE:
#endif
        return net_error(R_STD_NET_ERROR_UNSUPPORTED, native_code);
    case ECONNREFUSED:
        return net_error(R_STD_NET_ERROR_CONNECTION_REFUSED, native_code);
    case ECONNRESET:
        return net_error(R_STD_NET_ERROR_CONNECTION_RESET, native_code);
    case ECONNABORTED:
        return net_error(R_STD_NET_ERROR_CONNECTION_ABORTED, native_code);
    case EADDRINUSE:
        return net_error(R_STD_NET_ERROR_ADDRESS_IN_USE, native_code);
    case EADDRNOTAVAIL:
    /* R-SLIB-NET-0014: a Unix-domain path that names no socket. */
    case ENOENT:
    case ENOTDIR:
        return net_error(R_STD_NET_ERROR_ADDRESS_NOT_AVAILABLE, native_code);
    case ENETUNREACH:
        return net_error(R_STD_NET_ERROR_NETWORK_UNREACHABLE, native_code);
    case EHOSTUNREACH:
        return net_error(R_STD_NET_ERROR_HOST_UNREACHABLE, native_code);
    case ENOTCONN:
    case EDESTADDRREQ:
#if defined(EPIPE) && (EPIPE != ENOTCONN)
    case EPIPE:
#endif
        return net_error(R_STD_NET_ERROR_NOT_CONNECTED, native_code);
    case EBADF:
        return net_error(R_STD_NET_ERROR_CLOSED, native_code);
    case EACCES:
    case EPERM:
        return net_error(R_STD_NET_ERROR_PERMISSION_DENIED, native_code);
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM:
#if defined(EAGAIN)
    case EAGAIN:
#endif
        return net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, native_code);
    case EMSGSIZE:
        return net_error(R_STD_NET_ERROR_MESSAGE_TOO_LARGE, native_code);
    case ECANCELED:
        return net_error(R_STD_NET_ERROR_CANCELLED, native_code);
    case ETIMEDOUT:
        return net_error(R_STD_NET_ERROR_TIMED_OUT, native_code);
#if defined(ENOTSUP)
    case ENOTSUP:
        return net_error(R_STD_NET_ERROR_UNSUPPORTED, native_code);
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || (EOPNOTSUPP != ENOTSUP))
    case EOPNOTSUPP:
        return net_error(R_STD_NET_ERROR_UNSUPPORTED, native_code);
#endif
#if defined(ENOPROTOOPT)
    case ENOPROTOOPT:
        return net_error(R_STD_NET_ERROR_UNSUPPORTED, native_code);
#endif
    default:
        return net_error(R_STD_NET_ERROR_OTHER, native_code);
    }
}

static RLibraryNetHandleStorage *reserve_storage(RRuntimeAllocator *allocator,
                                                 size_t size,
                                                 size_t alignment,
                                                 RLibraryNetHandleKind kind) {
    RLibraryNetHandleStorage *storage = NULL;

    if ((allocator == NULL) ||
        (r_runtime_allocator_allocate(allocator, size, alignment, (void **)&storage) !=
         R_RUNTIME_ALLOCATION_OK)) {
        return NULL;
    }
    (void)memset(storage, 0, size);
    if (pthread_mutex_init(&storage->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(storage, alignment);
        return NULL;
    }
    atomic_init(&storage->references, 1U);
    storage->allocator = allocator;
    storage->allocation_alignment = alignment;
    storage->descriptor = -1;
    storage->kind = kind;
    return storage;
}

RStdNetTcpListenerStorage *
r_library_internal_net_tcp_listener_reserve(RRuntimeAllocator *allocator) {
    return (RStdNetTcpListenerStorage *)reserve_storage(allocator,
                                                        sizeof(RStdNetTcpListenerStorage),
                                                        _Alignof(RStdNetTcpListenerStorage),
                                                        R_LIBRARY_NET_HANDLE_TCP_LISTENER);
}

RStdNetTcpStreamStorage *r_library_internal_net_tcp_stream_reserve(RRuntimeAllocator *allocator) {
    return (RStdNetTcpStreamStorage *)reserve_storage(allocator,
                                                      sizeof(RStdNetTcpStreamStorage),
                                                      _Alignof(RStdNetTcpStreamStorage),
                                                      R_LIBRARY_NET_HANDLE_TCP_STREAM);
}

RStdNetUdpSocketStorage *r_library_internal_net_udp_socket_reserve(RRuntimeAllocator *allocator) {
    return (RStdNetUdpSocketStorage *)reserve_storage(allocator,
                                                      sizeof(RStdNetUdpSocketStorage),
                                                      _Alignof(RStdNetUdpSocketStorage),
                                                      R_LIBRARY_NET_HANDLE_UDP_SOCKET);
}

static void publish_storage(RLibraryNetHandleStorage *storage,
                            RLibraryNetHandleKind expected_kind,
                            int descriptor) {
    int descriptor_flags;
    int status_flags;

    if ((storage == NULL) || (descriptor < 0) || (storage->kind != expected_kind) ||
        (pthread_mutex_lock(&storage->mutex) != 0)) {
        abort();
    }
    if ((storage->allocator == NULL) || (storage->descriptor >= 0) || storage->terminal ||
        storage->close_reserved ||
        (atomic_load_explicit(&storage->references, memory_order_acquire) != 1U)) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    status_flags = fcntl(descriptor, F_GETFL);
    if ((descriptor_flags < 0) || (status_flags < 0) || ((descriptor_flags & FD_CLOEXEC) == 0) ||
        ((status_flags & O_NONBLOCK) == 0)) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    storage->descriptor = descriptor;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_listener_publish(RStdNetTcpListenerStorage *storage,
                                                 int descriptor) {
    publish_storage(
        storage == NULL ? NULL : &storage->handle, R_LIBRARY_NET_HANDLE_TCP_LISTENER, descriptor);
}

void r_library_internal_net_tcp_stream_publish(RStdNetTcpStreamStorage *storage, int descriptor) {
    publish_storage(
        storage == NULL ? NULL : &storage->handle, R_LIBRARY_NET_HANDLE_TCP_STREAM, descriptor);
}

void r_library_internal_net_udp_socket_publish(RStdNetUdpSocketStorage *storage, int descriptor) {
    publish_storage(
        storage == NULL ? NULL : &storage->handle, R_LIBRARY_NET_HANDLE_UDP_SOCKET, descriptor);
}

static _Bool retain_locked(RLibraryNetHandleStorage *storage) {
    size_t references;

    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    while (!storage->terminal && !storage->close_reserved && (storage->descriptor >= 0) &&
           (references != 0U) && (references != SIZE_MAX)) {
        if (atomic_compare_exchange_weak_explicit(&storage->references,
                                                  &references,
                                                  references + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return 1;
        }
    }
    return 0;
}

_Bool r_library_internal_net_handle_retain(RLibraryNetHandleStorage *storage) {
    _Bool retained;

    if ((storage == NULL) || (pthread_mutex_lock(&storage->mutex) != 0)) {
        return 0;
    }
    retained = retain_locked(storage);
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return retained;
}

_Bool r_library_internal_net_handle_retain_descriptor(RLibraryNetHandleStorage *storage,
                                                      RLibraryNetHandleKind expected_kind,
                                                      int *descriptor) {
    _Bool retained = 0;

    if (storage == NULL || descriptor == NULL || storage->kind != expected_kind ||
        pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    if (retain_locked(storage)) {
        *descriptor = storage->descriptor;
        retained = 1;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return retained;
}

static RLibraryNetTcpReadPrepareResult
tcp_read_prepare_result(RRuntimeDarwinIoStartStatus status, int native_error, _Bool end) {
    RLibraryNetTcpReadPrepareResult result = {0};

    result.status = status;
    result.native_error = native_error;
    result.end = end;
    return result;
}

RLibraryNetTcpReadPrepareResult
r_library_internal_net_tcp_stream_prepare_read(RStdNetTcpStreamStorage *stream,
                                               const RRuntimeDarwinIoBuffer *buffer,
                                               uint64_t timeout_nanoseconds) {
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoPrepareResult prepared;

    if (stream == NULL || buffer == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        return tcp_read_prepare_result(R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED, 0, 0);
    }
    if (stream->handle.kind != R_LIBRARY_NET_HANDLE_TCP_STREAM) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        abort();
    }
    if (stream->handle.terminal || stream->handle.close_reserved || stream->handle.descriptor < 0) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return tcp_read_prepare_result(R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED, 0, 0);
    }
    if (stream->handle.read_shutdown) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return tcp_read_prepare_result(R_RUNTIME_DARWIN_IO_START_OK, 0, 1);
    }
    if (stream->data_io == NULL) {
        created = r_runtime_darwin_io_handle_create_socket(stream->handle.allocator,
                                                           stream->handle.descriptor);
        if (created.status != R_RUNTIME_DARWIN_IO_START_OK) {
            if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
                abort();
            }
            return tcp_read_prepare_result(created.status, created.native_error, 0);
        }
        if (created.handle == NULL) {
            (void)pthread_mutex_unlock(&stream->handle.mutex);
            abort();
        }
        stream->data_io = created.handle;
    }
    prepared = r_runtime_darwin_io_prepare_read_some(
        stream->data_io, (off_t)0, buffer, timeout_nanoseconds);
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    return (RLibraryNetTcpReadPrepareResult){
        prepared.prepared,
        prepared.status,
        prepared.native_error,
        0,
    };
}

static RLibraryNetTcpWritePrepareResult tcp_write_prepare_result(RRuntimeDarwinIoStartStatus status,
                                                                 int native_error,
                                                                 _Bool write_shutdown) {
    RLibraryNetTcpWritePrepareResult result = {0};

    result.status = status;
    result.native_error = native_error;
    result.write_shutdown = write_shutdown;
    return result;
}

RLibraryNetTcpWritePrepareResult
r_library_internal_net_tcp_stream_prepare_write(RStdNetTcpStreamStorage *stream,
                                                const RRuntimeDarwinIoBuffer *buffer,
                                                uint64_t timeout_nanoseconds,
                                                _Bool write_all) {
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoPrepareResult prepared;

    if (stream == NULL || buffer == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        return tcp_write_prepare_result(R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED, 0, 0);
    }
    if (stream->handle.kind != R_LIBRARY_NET_HANDLE_TCP_STREAM) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        abort();
    }
    if (stream->handle.terminal || stream->handle.close_reserved || stream->handle.descriptor < 0) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return tcp_write_prepare_result(R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED, 0, 0);
    }
    if (stream->handle.write_shutdown) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return tcp_write_prepare_result(R_RUNTIME_DARWIN_IO_START_OK, 0, 1);
    }
    if (stream->data_io == NULL) {
        created = r_runtime_darwin_io_handle_create_socket(stream->handle.allocator,
                                                           stream->handle.descriptor);
        if (created.status != R_RUNTIME_DARWIN_IO_START_OK) {
            if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
                abort();
            }
            return tcp_write_prepare_result(created.status, created.native_error, 0);
        }
        if (created.handle == NULL) {
            (void)pthread_mutex_unlock(&stream->handle.mutex);
            abort();
        }
        stream->data_io = created.handle;
    }
    if (write_all) {
        prepared = r_runtime_darwin_io_prepare_write(
            stream->data_io, (off_t)0, buffer, timeout_nanoseconds);
    } else {
        prepared = r_runtime_darwin_io_prepare_write_some(
            stream->data_io, (off_t)0, buffer, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    return (RLibraryNetTcpWritePrepareResult){
        prepared.prepared,
        prepared.status,
        prepared.native_error,
        0,
    };
}

static RRuntimeDarwinIoShutdownDirection
runtime_shutdown_direction(RStdNetShutdownDirection direction) {
    switch (direction) {
    case R_STD_NET_SHUTDOWN_READ:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT;
    case R_STD_NET_SHUTDOWN_WRITE:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT;
    case R_STD_NET_SHUTDOWN_BOTH:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH;
    }
    abort();
}

static void shutdown_committed(void *context, RRuntimeDarwinIoShutdownDirection direction) {
    RStdNetTcpStreamStorage *stream = context;

    if (stream == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        abort();
    }
    switch (direction) {
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT:
        stream->handle.read_shutdown = 1;
        break;
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT:
        stream->handle.write_shutdown = 1;
        break;
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH:
        stream->handle.read_shutdown = 1;
        stream->handle.write_shutdown = 1;
        break;
    }
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
}

static _Bool shutdown_already_committed(const RStdNetTcpStreamStorage *stream,
                                        RStdNetShutdownDirection direction) {
    switch (direction) {
    case R_STD_NET_SHUTDOWN_READ:
        return stream->handle.read_shutdown;
    case R_STD_NET_SHUTDOWN_WRITE:
        return stream->handle.write_shutdown;
    case R_STD_NET_SHUTDOWN_BOTH:
        return stream->handle.read_shutdown && stream->handle.write_shutdown;
    }
    abort();
}

static RRuntimeDarwinIoStartStatus ensure_tcp_data_io_locked(RStdNetTcpStreamStorage *stream,
                                                             int *native_error) {
    RRuntimeDarwinIoHandleCreateResult created;

    *native_error = 0;
    if (stream->data_io != NULL) {
        return R_RUNTIME_DARWIN_IO_START_OK;
    }
    created = r_runtime_darwin_io_handle_create_socket(stream->handle.allocator,
                                                       stream->handle.descriptor);
    if (created.status != R_RUNTIME_DARWIN_IO_START_OK) {
        *native_error = created.native_error;
        return created.status;
    }
    if (created.handle == NULL) {
        abort();
    }
    stream->data_io = created.handle;
    return R_RUNTIME_DARWIN_IO_START_OK;
}

RLibraryNetTcpShutdownPrepareResult
r_library_internal_net_tcp_stream_prepare_shutdown(RStdNetTcpStreamStorage *stream,
                                                   RStdNetShutdownDirection direction,
                                                   uint64_t timeout_nanoseconds,
                                                   _Bool prepare_native) {
    RLibraryNetTcpShutdownPrepareResult result = {0};
    RRuntimeDarwinIoPrepareResult prepared;
    int native_error = 0;

    if (stream == NULL ||
        (direction != R_STD_NET_SHUTDOWN_READ && direction != R_STD_NET_SHUTDOWN_WRITE &&
         direction != R_STD_NET_SHUTDOWN_BOTH) ||
        pthread_mutex_lock(&stream->handle.mutex) != 0) {
        result.status = R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT;
        return result;
    }
    if (stream->handle.kind != R_LIBRARY_NET_HANDLE_TCP_STREAM) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        abort();
    }
    if (stream->handle.terminal || stream->handle.close_reserved || stream->handle.descriptor < 0) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        result.status = R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED;
        return result;
    }
    result.already_shutdown = shutdown_already_committed(stream, direction);
    if (!result.already_shutdown && prepare_native) {
        result.status = ensure_tcp_data_io_locked(stream, &native_error);
        result.native_error = native_error;
    } else {
        result.status = R_RUNTIME_DARWIN_IO_START_OK;
    }
    if (result.status == R_RUNTIME_DARWIN_IO_START_OK && !retain_locked(&stream->handle)) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        abort();
    }
    if (result.status != R_RUNTIME_DARWIN_IO_START_OK || result.already_shutdown ||
        !prepare_native) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        result.storage = result.status == R_RUNTIME_DARWIN_IO_START_OK ? stream : NULL;
        return result;
    }
    prepared = r_runtime_darwin_io_prepare_shutdown(stream->data_io,
                                                    runtime_shutdown_direction(direction),
                                                    timeout_nanoseconds,
                                                    shutdown_committed,
                                                    stream);
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    if (prepared.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_library_internal_net_handle_release(&stream->handle);
        result.status = prepared.status;
        result.native_error = prepared.native_error;
        return result;
    }
    result.prepared = prepared.prepared;
    result.storage = stream;
    return result;
}

RLibraryNetTcpClosePrepareResult
r_library_internal_net_tcp_stream_prepare_close(RStdNetTcpStreamStorage *stream,
                                                uint64_t timeout_nanoseconds) {
    RLibraryNetTcpClosePrepareResult result = {0};
    RRuntimeDarwinIoPrepareResult prepared;
    int descriptor_status;
    int native_error;

    if (stream == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        result.status = R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT;
        return result;
    }
    if (stream->handle.kind != R_LIBRARY_NET_HANDLE_TCP_STREAM || stream->handle.terminal ||
        stream->handle.close_reserved || stream->handle.descriptor < 0) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        result.status = R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED;
        return result;
    }
    do {
        errno = 0;
        descriptor_status = fcntl(stream->handle.descriptor, F_GETFD);
        native_error = descriptor_status < 0 ? errno : 0;
    } while (descriptor_status < 0 && native_error == EINTR);
    if (descriptor_status < 0) {
        result.preexisting_failure = 1;
        result.preexisting_error = r_library_internal_net_error_from_native(native_error);
    }
    if (stream->data_io == NULL && result.preexisting_failure) {
        result.status = R_RUNTIME_DARWIN_IO_START_OK;
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return result;
    }
    result.status = ensure_tcp_data_io_locked(stream, &result.native_error);
    if (result.status != R_RUNTIME_DARWIN_IO_START_OK) {
        if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
            abort();
        }
        return result;
    }
    result.data_io = stream->data_io;
    prepared = r_runtime_darwin_io_prepare_close(result.data_io, timeout_nanoseconds);
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    result.prepared = prepared.prepared;
    result.status = prepared.status;
    result.native_error = prepared.native_error;
    return result;
}

_Bool r_library_internal_net_tcp_stream_mark_closing(RStdNetTcpStreamStorage *stream) {
    _Bool marked = 0;

    if (stream == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        return 0;
    }
    if (stream->handle.kind == R_LIBRARY_NET_HANDLE_TCP_STREAM && !stream->handle.terminal &&
        !stream->handle.close_reserved && stream->handle.descriptor >= 0) {
        stream->handle.close_reserved = 1;
        marked = 1;
    }
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    return marked;
}

int r_library_internal_net_tcp_stream_take_close_descriptor(RStdNetTcpStreamStorage *stream,
                                                            RRuntimeDarwinIoHandle **data_io) {
    int descriptor;

    if (stream == NULL || data_io == NULL || pthread_mutex_lock(&stream->handle.mutex) != 0) {
        abort();
    }
    if (stream->handle.kind != R_LIBRARY_NET_HANDLE_TCP_STREAM || stream->handle.terminal ||
        !stream->handle.close_reserved || stream->handle.descriptor < 0) {
        (void)pthread_mutex_unlock(&stream->handle.mutex);
        abort();
    }
    descriptor = stream->handle.descriptor;
    stream->handle.descriptor = -1;
    stream->handle.terminal = 1;
    stream->handle.read_shutdown = 1;
    stream->handle.write_shutdown = 1;
    *data_io = stream->data_io;
    stream->data_io = NULL;
    if (pthread_mutex_unlock(&stream->handle.mutex) != 0) {
        abort();
    }
    return descriptor;
}

void r_library_internal_net_handle_release(RLibraryNetHandleStorage *storage) {
    RRuntimeDarwinIoHandle *data_io = NULL;
    size_t previous;

    if (storage == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&storage->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        abort();
    }
    if (previous != 1U) {
        return;
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->kind == R_LIBRARY_NET_HANDLE_TCP_LISTENER) {
        const RStdNetTcpListenerStorage *listener = (const RStdNetTcpListenerStorage *)storage;

        if (listener->accept_head != NULL || listener->accept_tail != NULL ||
            listener->accept_drain != NULL || listener->accept_drain_context != NULL ||
            listener->accept_reservations != 0U) {
            (void)pthread_mutex_unlock(&storage->mutex);
            abort();
        }
    } else if (storage->kind == R_LIBRARY_NET_HANDLE_TCP_STREAM) {
        RStdNetTcpStreamStorage *stream = (RStdNetTcpStreamStorage *)storage;

        /*
         * The cached view belongs to the stream storage. Each active request has an independent
         * runtime retain, so detach the view under the storage mutex and release it only after
         * unlocking. A future close operation must reserve closing under this same mutex, reject
         * new preparations, and wait for the cached root's final acknowledgement.
         */
        data_io = stream->data_io;
        stream->data_io = NULL;
    } else if (storage->kind == R_LIBRARY_NET_HANDLE_UDP_SOCKET) {
        const RStdNetUdpSocketStorage *socket = (const RStdNetUdpSocketStorage *)storage;

        if (socket->send_head != NULL || socket->send_tail != NULL ||
            socket->receive_head != NULL || socket->receive_tail != NULL || socket->drain != NULL ||
            socket->drain_context != NULL || socket->send_reservations != 0U ||
            socket->receive_reservations != 0U) {
            (void)pthread_mutex_unlock(&storage->mutex);
            abort();
        }
    }
    storage->terminal = 1;
    if (storage->descriptor >= 0) {
        (void)close(storage->descriptor);
        storage->descriptor = -1;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&storage->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_io_handle_release(data_io);
    r_runtime_allocator_deallocate(storage, storage->allocation_alignment);
}

_Bool r_library_internal_net_address_from_native(const struct sockaddr *native,
                                                 socklen_t native_length,
                                                 RStdNetSocketAddress *result) {
    if ((native == NULL) || (result == NULL)) {
        return 0;
    }
    (void)memset(result, 0, sizeof(*result));
    if ((native->sa_family == AF_INET) && ((size_t)native_length >= sizeof(struct sockaddr_in))) {
        const struct sockaddr_in *address = (const struct sockaddr_in *)native;

        result->address.kind = R_STD_NET_IP_ADDRESS_V4;
        (void)memcpy(
            result->address.bytes.v4, &address->sin_addr, sizeof(result->address.bytes.v4));
        result->port = ntohs(address->sin_port);
        return 1;
    }
    if ((native->sa_family == AF_INET6) && ((size_t)native_length >= sizeof(struct sockaddr_in6))) {
        const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)native;

        result->address.kind = R_STD_NET_IP_ADDRESS_V6;
        (void)memcpy(
            result->address.bytes.v6, &address->sin6_addr, sizeof(result->address.bytes.v6));
        result->port = ntohs(address->sin6_port);
        result->scope_id = address->sin6_scope_id;
        return 1;
    }
    return 0;
}

RStdNetSocketAddressResult r_library_internal_net_observe_address(
    RLibraryNetHandleStorage *storage, RLibraryNetHandleKind expected_kind, _Bool peer) {
    RStdNetSocketAddressResult result = {0};
    struct sockaddr_storage native = {0};
    socklen_t native_length = (socklen_t)sizeof(native);
    int status;
    int native_code;

    if ((storage == NULL) || (storage->kind != expected_kind)) {
        result.error = net_error(R_STD_NET_ERROR_CLOSED, 0);
        return result;
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        result.error = net_error(R_STD_NET_ERROR_OTHER, 0);
        return result;
    }
    if (storage->terminal || storage->close_reserved || (storage->descriptor < 0)) {
        result.error = net_error(R_STD_NET_ERROR_CLOSED, 0);
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return result;
    }
    errno = 0;
    status = peer ? getpeername(storage->descriptor, (struct sockaddr *)&native, &native_length)
                  : getsockname(storage->descriptor, (struct sockaddr *)&native, &native_length);
    native_code = status == 0 ? 0 : errno;
    if (status == 0) {
        result.is_ok = r_library_internal_net_address_from_native(
            (const struct sockaddr *)&native, native_length, &result.value);
        if (!result.is_ok) {
            result.error = net_error(R_STD_NET_ERROR_INVALID_ADDRESS, 0);
        }
    } else {
        result.error = r_library_internal_net_error_from_native(native_code);
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return result;
}

void r_library_internal_net_tcp_listener_move(RStdNetTcpListener *destination,
                                              RStdNetTcpListener *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_tcp_listener_drop(RStdNetTcpListener *listener) {
    r_library_internal_net_handle_release(listener->storage == NULL ? NULL
                                                                    : &listener->storage->handle);
    listener->storage = NULL;
}

void r_library_internal_net_tcp_stream_move(RStdNetTcpStream *destination,
                                            RStdNetTcpStream *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_tcp_stream_drop(RStdNetTcpStream *stream) {
    r_library_internal_net_handle_release(stream->storage == NULL ? NULL
                                                                  : &stream->storage->handle);
    stream->storage = NULL;
}

void r_library_internal_net_udp_socket_move(RStdNetUdpSocket *destination,
                                            RStdNetUdpSocket *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_udp_socket_drop(RStdNetUdpSocket *socket) {
    r_library_internal_net_handle_release(socket->storage == NULL ? NULL
                                                                  : &socket->storage->handle);
    socket->storage = NULL;
}

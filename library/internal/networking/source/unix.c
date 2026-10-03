#include "r_library_net_internal.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ucred.h>
#include <sys/un.h>
#include <unistd.h>

/*
 * R-SLIB-NET-0014: Unix-domain handles reuse the TCP listener, TCP stream and UDP socket
 * storages. Each operation below lends the storage to the shared implementation through a
 * temporary shell; a consuming operation hands the consumption back to the caller's shell.
 */

_Bool r_library_internal_net_unix_address(RStdStringView path,
                                          struct sockaddr_un *native,
                                          socklen_t *native_length) {
    size_t index;

    (void)memset(native, 0, sizeof(*native));
    *native_length = 0;
    if (path.length == 0U || path.length >= sizeof(native->sun_path) || path.data == NULL) {
        return 0;
    }
    for (index = 0U; index < path.length; ++index) {
        if (path.data[index] == 0U) {
            return 0;
        }
    }
    native->sun_family = AF_UNIX;
    (void)memcpy(native->sun_path, path.data, path.length);
    *native_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path.length + 1U);
    native->sun_len = (unsigned char)*native_length;
    return 1;
}

void r_library_internal_net_unix_listener_move(RStdNetUnixListener *destination,
                                               RStdNetUnixListener *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_unix_listener_drop(RStdNetUnixListener *listener) {
    r_library_internal_net_handle_release(listener->storage == NULL ? NULL
                                                                    : &listener->storage->handle);
    listener->storage = NULL;
}

void r_library_internal_net_unix_stream_move(RStdNetUnixStream *destination,
                                             RStdNetUnixStream *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_unix_stream_drop(RStdNetUnixStream *stream) {
    r_library_internal_net_handle_release(stream->storage == NULL ? NULL
                                                                  : &stream->storage->handle);
    stream->storage = NULL;
}

void r_library_internal_net_unix_datagram_move(RStdNetUnixDatagram *destination,
                                               RStdNetUnixDatagram *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_net_unix_datagram_drop(RStdNetUnixDatagram *socket) {
    r_library_internal_net_handle_release(socket->storage == NULL ? NULL
                                                                  : &socket->storage->handle);
    socket->storage = NULL;
}

/* R-SLIB-NET-0016: the credentials the target recorded for the peer at establishment. */
RStdNetPeerCredentialsResult
r_library_internal_net_unix_peer_credentials(const RStdNetUnixStream *stream) {
    RStdNetPeerCredentialsResult result = {0};
    int descriptor = -1;
    uid_t user_id = 0;
    gid_t group_id = 0;
    pid_t process_id = 0;
    socklen_t length = (socklen_t)sizeof(process_id);

    if (stream == NULL || stream->storage == NULL ||
        !r_library_internal_net_handle_retain_descriptor(
            &stream->storage->handle, R_LIBRARY_NET_HANDLE_TCP_STREAM, &descriptor)) {
        result.error = (RStdNetError){R_STD_NET_ERROR_CLOSED, INT64_C(0)};
        return result;
    }
    errno = 0;
    if (getpeereid(descriptor, &user_id, &group_id) != 0 ||
        getsockopt(descriptor, SOL_LOCAL, LOCAL_PEERPID, &process_id, &length) != 0) {
        result.error = r_library_internal_net_error_from_native(errno);
    } else {
        result.is_ok = 1;
        result.value.user_id = (uint32_t)user_id;
        result.value.group_id = (uint32_t)group_id;
        result.value.process_id = (int32_t)process_id;
    }
    r_library_internal_net_handle_release(&stream->storage->handle);
    return result;
}

RStdNetTaskStartResult r_library_internal_net_unix_read_into(const RStdNetUnixStream *stream,
                                                             RStdNetMutableBytes target,
                                                             RStdNetDeadline deadline) {
    const RStdNetTcpStream shell = {stream->storage};

    return r_library_internal_net_tcp_read_into(&shell, target, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_write_from(const RStdNetUnixStream *stream,
                                                              RStdNetConstBytes source,
                                                              RStdNetDeadline deadline) {
    const RStdNetTcpStream shell = {stream->storage};

    return r_library_internal_net_tcp_write_from(&shell, source, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_write_all_from(const RStdNetUnixStream *stream,
                                                                  RStdNetConstBytes source,
                                                                  RStdNetDeadline deadline) {
    const RStdNetTcpStream shell = {stream->storage};

    return r_library_internal_net_tcp_write_all_from(&shell, source, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_shutdown(const RStdNetUnixStream *stream,
                                                            RStdNetShutdownDirection direction,
                                                            RStdNetDeadline deadline) {
    const RStdNetTcpStream shell = {stream->storage};

    return r_library_internal_net_tcp_shutdown(&shell, direction, deadline);
}

RStdNetTaskStartResult r_library_internal_net_unix_close(RStdNetUnixStream *stream,
                                                         RStdNetDeadline deadline) {
    RStdNetTcpStream shell = {stream->storage};
    const RStdNetTaskStartResult result = r_library_internal_net_tcp_close(&shell, deadline);

    stream->storage = shell.storage;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_unix_listener_close(RStdNetUnixListener *listener,
                                                                  RStdNetDeadline deadline) {
    RStdNetTcpListener shell = {listener->storage};
    const RStdNetTaskStartResult result =
        r_library_internal_net_tcp_listener_close(&shell, deadline);

    listener->storage = shell.storage;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_unix_datagram_close(RStdNetUnixDatagram *socket,
                                                                  RStdNetDeadline deadline) {
    RStdNetUdpSocket shell = {socket->storage};
    const RStdNetTaskStartResult result = r_library_internal_net_udp_close(&shell, deadline);

    socket->storage = shell.storage;
    return result;
}

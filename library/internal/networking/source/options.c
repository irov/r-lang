#include "r_library_net_internal.h"

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* R-SLIB-NET-0012..0013: option values that the target does not accept are unsupported. */
static RStdNetError option_native_error(int native_code) {
    if (native_code == EINVAL || native_code == ENOPROTOOPT || native_code == EDOM) {
        return (RStdNetError){R_STD_NET_ERROR_UNSUPPORTED, (int64_t)native_code};
    }
    return r_library_internal_net_error_from_native(native_code);
}

static RStdNetError option_unsupported(void) {
    return (RStdNetError){R_STD_NET_ERROR_UNSUPPORTED, INT64_C(0)};
}

/* Locks a live handle of the expected kind; a closing or closed identity reports closed. */
static _Bool option_lock(RLibraryNetHandleStorage *storage,
                         RLibraryNetHandleKind kind,
                         RStdNetError *error) {
    if ((storage == NULL) || (storage->kind != kind)) {
        *error = (RStdNetError){R_STD_NET_ERROR_CLOSED, INT64_C(0)};
        return 0;
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        *error = (RStdNetError){R_STD_NET_ERROR_OTHER, INT64_C(0)};
        return 0;
    }
    if (storage->terminal || storage->close_reserved || (storage->descriptor < 0)) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        *error = (RStdNetError){R_STD_NET_ERROR_CLOSED, INT64_C(0)};
        return 0;
    }
    return 1;
}

static void option_unlock(RLibraryNetHandleStorage *storage) {
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
}

static _Bool option_family(int descriptor, int *family, RStdNetError *error) {
    struct sockaddr_storage native = {0};
    socklen_t length = (socklen_t)sizeof(native);

    if (getsockname(descriptor, (struct sockaddr *)&native, &length) != 0) {
        *error = option_native_error(errno);
        return 0;
    }
    if (native.ss_family != AF_INET && native.ss_family != AF_INET6) {
        *error = option_unsupported();
        return 0;
    }
    *family = native.ss_family;
    return 1;
}

static _Bool option_get_int(int descriptor, int level, int name, int *value, RStdNetError *error) {
    socklen_t length = (socklen_t)sizeof(*value);

    *value = 0;
    if (getsockopt(descriptor, level, name, value, &length) != 0) {
        *error = option_native_error(errno);
        return 0;
    }
    return 1;
}

static _Bool option_set_int(int descriptor, int level, int name, int value, RStdNetError *error) {
    if (setsockopt(descriptor, level, name, &value, (socklen_t)sizeof(value)) != 0) {
        *error = option_native_error(errno);
        return 0;
    }
    return 1;
}

/* The BSD IPv4 multicast options take one byte. */
static _Bool option_get_byte(int descriptor, int name, int *value, RStdNetError *error) {
    unsigned char byte = 0U;
    socklen_t length = (socklen_t)sizeof(byte);

    if (getsockopt(descriptor, IPPROTO_IP, name, &byte, &length) != 0) {
        *error = option_native_error(errno);
        return 0;
    }
    *value = (int)byte;
    return 1;
}

static _Bool option_set_byte(int descriptor, int name, int value, RStdNetError *error) {
    const unsigned char byte = (unsigned char)value;

    if (setsockopt(descriptor, IPPROTO_IP, name, &byte, (socklen_t)sizeof(byte)) != 0) {
        *error = option_native_error(errno);
        return 0;
    }
    return 1;
}

static _Bool option_get_hops(int descriptor, int family, int *value, RStdNetError *error) {
    return family == AF_INET
               ? option_get_int(descriptor, IPPROTO_IP, IP_TTL, value, error)
               : option_get_int(descriptor, IPPROTO_IPV6, IPV6_UNICAST_HOPS, value, error);
}

static _Bool option_set_hops(int descriptor, int family, int value, RStdNetError *error) {
    return family == AF_INET
               ? option_set_int(descriptor, IPPROTO_IP, IP_TTL, value, error)
               : option_set_int(descriptor, IPPROTO_IPV6, IPV6_UNICAST_HOPS, value, error);
}

static _Bool option_buffers(int descriptor, size_t *receive, size_t *send, RStdNetError *error) {
    int receive_value;
    int send_value;

    if (!option_get_int(descriptor, SOL_SOCKET, SO_RCVBUF, &receive_value, error) ||
        !option_get_int(descriptor, SOL_SOCKET, SO_SNDBUF, &send_value, error)) {
        return 0;
    }
    *receive = receive_value < 0 ? 0U : (size_t)receive_value;
    *send = send_value < 0 ? 0U : (size_t)send_value;
    return 1;
}

/* A buffer size of 1..INT_MAX bytes; zero and larger sizes are unsupported. */
static _Bool option_set_buffer(int descriptor, int name, size_t current, size_t wanted,
                               RStdNetError *error) {
    if (wanted == current) {
        return 1;
    }
    if (wanted == 0U || wanted > (size_t)INT_MAX) {
        *error = option_unsupported();
        return 0;
    }
    return option_set_int(descriptor, SOL_SOCKET, name, (int)wanted, error);
}

/* The keepalive idle time in whole seconds, rounded up and at least one. */
static _Bool option_keepalive_seconds(RStdTimeDuration duration, int *seconds) {
    int64_t whole;

    if (duration.seconds < 0 || duration.nanoseconds >= UINT32_C(1000000000)) {
        return 0;
    }
    whole = duration.seconds;
    if (duration.nanoseconds != 0U) {
        if (whole == INT64_MAX) {
            return 0;
        }
        whole += 1;
    }
    if (whole == 0) {
        whole = 1;
    }
    if (whole > (int64_t)INT_MAX) {
        return 0;
    }
    *seconds = (int)whole;
    return 1;
}

static _Bool tcp_options_locked(int descriptor, RStdNetTcpOptions *options, RStdNetError *error) {
    int family;
    int nodelay;
    int keepalive;
    int idle = 0;
    int hops;

    if (!option_family(descriptor, &family, error) ||
        !option_get_int(descriptor, IPPROTO_TCP, TCP_NODELAY, &nodelay, error) ||
        !option_get_int(descriptor, SOL_SOCKET, SO_KEEPALIVE, &keepalive, error) ||
        (keepalive != 0 &&
         !option_get_int(descriptor, IPPROTO_TCP, TCP_KEEPALIVE, &idle, error)) ||
        !option_get_hops(descriptor, family, &hops, error) ||
        !option_buffers(descriptor, &options->receive_buffer, &options->send_buffer, error)) {
        return 0;
    }
    options->nodelay = nodelay != 0;
    options->keepalive = (RStdTimeDurationOption){0};
    if (keepalive != 0) {
        options->keepalive.r_tag = UINT32_C(1);
        options->keepalive.r_payload.r_some =
            (RStdTimeDuration){(int64_t)(idle < 0 ? 0 : idle), UINT32_C(0)};
    }
    options->hop_limit = hops < 0 ? 0U : (uint32_t)hops;
    return 1;
}

static _Bool udp_options_locked(int descriptor, RStdNetUdpOptions *options, RStdNetError *error) {
    int family;
    int hops;
    int broadcast;
    int multicast_hops;
    int multicast_loop;

    if (!option_family(descriptor, &family, error) ||
        !option_get_hops(descriptor, family, &hops, error) ||
        !option_buffers(descriptor, &options->receive_buffer, &options->send_buffer, error) ||
        !option_get_int(descriptor, SOL_SOCKET, SO_BROADCAST, &broadcast, error)) {
        return 0;
    }
    if (family == AF_INET) {
        if (!option_get_byte(descriptor, IP_MULTICAST_TTL, &multicast_hops, error) ||
            !option_get_byte(descriptor, IP_MULTICAST_LOOP, &multicast_loop, error)) {
            return 0;
        }
    } else if (!option_get_int(
                   descriptor, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &multicast_hops, error) ||
               !option_get_int(
                   descriptor, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &multicast_loop, error)) {
        return 0;
    }
    options->hop_limit = hops < 0 ? 0U : (uint32_t)hops;
    options->broadcast = broadcast != 0;
    options->multicast_hop_limit = multicast_hops < 0 ? 0U : (uint32_t)multicast_hops;
    options->multicast_loop = multicast_loop != 0;
    return 1;
}

RStdNetTcpOptionsResult r_library_internal_net_tcp_get_options(RLibraryNetHandleStorage *storage) {
    RStdNetTcpOptionsResult result = {0};

    if (!option_lock(storage, R_LIBRARY_NET_HANDLE_TCP_STREAM, &result.error)) {
        return result;
    }
    result.is_ok = tcp_options_locked(storage->descriptor, &result.value, &result.error);
    option_unlock(storage);
    return result;
}

RStdNetOptionResult r_library_internal_net_tcp_set_options(RLibraryNetHandleStorage *storage,
                                                           RStdNetTcpOptions options) {
    RStdNetOptionResult result = {0};
    RStdNetTcpOptions current = {0};
    int descriptor;
    int family;
    int seconds = 0;
    const _Bool wanted_keepalive = options.keepalive.r_tag == UINT32_C(1);

    if ((wanted_keepalive &&
         !option_keepalive_seconds(options.keepalive.r_payload.r_some, &seconds)) ||
        options.hop_limit == 0U || options.hop_limit > 255U ||
        options.receive_buffer == 0U || options.receive_buffer > (size_t)INT_MAX ||
        options.send_buffer == 0U || options.send_buffer > (size_t)INT_MAX) {
        result.error = option_unsupported();
        return result;
    }
    if (!option_lock(storage, R_LIBRARY_NET_HANDLE_TCP_STREAM, &result.error)) {
        return result;
    }
    descriptor = storage->descriptor;
    if (tcp_options_locked(descriptor, &current, &result.error) &&
        option_family(descriptor, &family, &result.error) &&
        (options.nodelay == current.nodelay ||
         option_set_int(descriptor, IPPROTO_TCP, TCP_NODELAY, options.nodelay ? 1 : 0,
                        &result.error)) &&
        (wanted_keepalive == (current.keepalive.r_tag == UINT32_C(1)) ||
         option_set_int(descriptor, SOL_SOCKET, SO_KEEPALIVE, wanted_keepalive ? 1 : 0,
                        &result.error)) &&
        (!wanted_keepalive ||
         (current.keepalive.r_tag == UINT32_C(1) &&
          current.keepalive.r_payload.r_some.seconds == (int64_t)seconds) ||
         option_set_int(descriptor, IPPROTO_TCP, TCP_KEEPALIVE, seconds, &result.error)) &&
        (options.hop_limit == current.hop_limit ||
         option_set_hops(descriptor, family, (int)options.hop_limit, &result.error)) &&
        option_set_buffer(descriptor, SO_RCVBUF, current.receive_buffer, options.receive_buffer,
                          &result.error) &&
        option_set_buffer(descriptor, SO_SNDBUF, current.send_buffer, options.send_buffer,
                          &result.error)) {
        result.is_ok = 1;
    }
    option_unlock(storage);
    return result;
}

RStdNetUdpOptionsResult r_library_internal_net_udp_get_options(RLibraryNetHandleStorage *storage) {
    RStdNetUdpOptionsResult result = {0};

    if (!option_lock(storage, R_LIBRARY_NET_HANDLE_UDP_SOCKET, &result.error)) {
        return result;
    }
    result.is_ok = udp_options_locked(storage->descriptor, &result.value, &result.error);
    option_unlock(storage);
    return result;
}

RStdNetOptionResult r_library_internal_net_udp_set_options(RLibraryNetHandleStorage *storage,
                                                           RStdNetUdpOptions options) {
    RStdNetOptionResult result = {0};
    RStdNetUdpOptions current = {0};
    int descriptor;
    int family;
    _Bool multicast_hops_ok;
    _Bool multicast_loop_ok;

    if (options.hop_limit == 0U || options.hop_limit > 255U ||
        options.multicast_hop_limit > 255U || options.receive_buffer == 0U ||
        options.receive_buffer > (size_t)INT_MAX || options.send_buffer == 0U ||
        options.send_buffer > (size_t)INT_MAX) {
        result.error = option_unsupported();
        return result;
    }
    if (!option_lock(storage, R_LIBRARY_NET_HANDLE_UDP_SOCKET, &result.error)) {
        return result;
    }
    descriptor = storage->descriptor;
    if (!udp_options_locked(descriptor, &current, &result.error) ||
        !option_family(descriptor, &family, &result.error) ||
        !(options.hop_limit == current.hop_limit ||
          option_set_hops(descriptor, family, (int)options.hop_limit, &result.error)) ||
        !option_set_buffer(descriptor, SO_RCVBUF, current.receive_buffer, options.receive_buffer,
                           &result.error) ||
        !option_set_buffer(descriptor, SO_SNDBUF, current.send_buffer, options.send_buffer,
                           &result.error) ||
        !(options.broadcast == current.broadcast ||
          option_set_int(descriptor, SOL_SOCKET, SO_BROADCAST, options.broadcast ? 1 : 0,
                         &result.error))) {
        option_unlock(storage);
        return result;
    }
    if (family == AF_INET) {
        multicast_hops_ok =
            options.multicast_hop_limit == current.multicast_hop_limit ||
            option_set_byte(descriptor, IP_MULTICAST_TTL, (int)options.multicast_hop_limit,
                            &result.error);
        multicast_loop_ok =
            multicast_hops_ok &&
            (options.multicast_loop == current.multicast_loop ||
             option_set_byte(descriptor, IP_MULTICAST_LOOP, options.multicast_loop ? 1 : 0,
                             &result.error));
    } else {
        multicast_hops_ok =
            options.multicast_hop_limit == current.multicast_hop_limit ||
            option_set_int(descriptor, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                           (int)options.multicast_hop_limit, &result.error);
        multicast_loop_ok =
            multicast_hops_ok &&
            (options.multicast_loop == current.multicast_loop ||
             option_set_int(descriptor, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                            options.multicast_loop ? 1 : 0, &result.error));
    }
    result.is_ok = multicast_loop_ok;
    option_unlock(storage);
    return result;
}

/* Changes the membership. Darwin selects no interface for MCAST_JOIN_GROUP with index zero, so a
   v4 group on the default interface goes through IP_ADD_MEMBERSHIP with INADDR_ANY, which routes
   the group; v6 has no such lookup and index zero reaches the target unchanged. */
static int option_membership(int descriptor,
                             int family,
                             const struct group_req *request,
                             RStdNetIpAddress group,
                             _Bool join) {
    if ((family == AF_INET) && (request->gr_interface == 0U)) {
        struct ip_mreq legacy;

        (void)memset(&legacy, 0, sizeof(legacy));
        (void)memcpy(&legacy.imr_multiaddr.s_addr, group.bytes.v4, sizeof(group.bytes.v4));
        legacy.imr_interface.s_addr = INADDR_ANY;
        return setsockopt(descriptor,
                          IPPROTO_IP,
                          join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                          &legacy,
                          (socklen_t)sizeof(legacy));
    }
    return setsockopt(descriptor,
                      family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                      join ? MCAST_JOIN_GROUP : MCAST_LEAVE_GROUP,
                      request,
                      (socklen_t)sizeof(*request));
}

/* R-SLIB-NET-0013: a group of the socket's family, 224.0.0.0/4 or ff00::/8, on an interface
   index or zero for the interface the target routes the group to. */
RStdNetOptionResult r_library_internal_net_udp_membership(RLibraryNetHandleStorage *storage,
                                                          RStdNetIpAddress group,
                                                          uint32_t interface_index,
                                                          _Bool join) {
    RStdNetOptionResult result = {0};
    struct group_req request;
    socklen_t native_length = 0;
    int domain = 0;
    int family;
    const _Bool multicast = group.kind == R_STD_NET_IP_ADDRESS_V4
                                ? (group.bytes.v4[0] & 0xF0U) == 0xE0U
                                : group.bytes.v6[0] == 0xFFU;

    (void)memset(&request, 0, sizeof(request));
    if (!multicast ||
        !r_library_internal_net_address_to_native((RStdNetSocketAddress){group, 0U, 0U},
                                                  &request.gr_group,
                                                  &native_length,
                                                  &domain)) {
        result.error = (RStdNetError){R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)};
        return result;
    }
    request.gr_interface = interface_index;
    if (!option_lock(storage, R_LIBRARY_NET_HANDLE_UDP_SOCKET, &result.error)) {
        return result;
    }
    if (option_family(storage->descriptor, &family, &result.error)) {
        if (family != domain) {
            result.error = option_unsupported();
        } else if (option_membership(storage->descriptor, family, &request, group, join) != 0) {
            result.error = option_native_error(errno);
        } else {
            result.is_ok = 1;
        }
    }
    option_unlock(storage);
    return result;
}

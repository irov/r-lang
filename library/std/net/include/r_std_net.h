#ifndef R_STD_NET_H
#define R_STD_NET_H

#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_std_async.h"
#include "r_std_error_types.h"
#include "r_std_string.h"
#include "r_std_time.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RStdNetIpAddressKind {
    R_STD_NET_IP_ADDRESS_V4 = 0,
    R_STD_NET_IP_ADDRESS_V6 = 1
} RStdNetIpAddressKind;

typedef union RStdNetIpAddressBytes {
    uint8_t v4[4];
    uint8_t v6[16];
} RStdNetIpAddressBytes;

typedef struct RStdNetIpAddress {
    RStdNetIpAddressKind kind;
    RStdNetIpAddressBytes bytes;
} RStdNetIpAddress;

typedef struct RStdNetSocketAddress {
    RStdNetIpAddress address;
    uint16_t port;
    uint32_t scope_id;
} RStdNetSocketAddress;

typedef enum RStdNetAddressErrorCode {
    R_STD_NET_ADDRESS_ERROR_EMPTY = 0,
    R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER = 1,
    R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT = 2,
    R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE = 3
} RStdNetAddressErrorCode;

typedef struct RStdNetAddressError {
    RStdNetAddressErrorCode code;
    size_t index;
} RStdNetAddressError;

typedef enum RStdNetErrorCode {
    R_STD_NET_ERROR_INVALID_ADDRESS = 0,
    R_STD_NET_ERROR_INVALID_NAME = 1,
    R_STD_NET_ERROR_NAME_NOT_FOUND = 2,
    R_STD_NET_ERROR_TEMPORARY_FAILURE = 3,
    R_STD_NET_ERROR_CONNECTION_REFUSED = 4,
    R_STD_NET_ERROR_CONNECTION_RESET = 5,
    R_STD_NET_ERROR_CONNECTION_ABORTED = 6,
    R_STD_NET_ERROR_ADDRESS_IN_USE = 7,
    R_STD_NET_ERROR_ADDRESS_NOT_AVAILABLE = 8,
    R_STD_NET_ERROR_NETWORK_UNREACHABLE = 9,
    R_STD_NET_ERROR_HOST_UNREACHABLE = 10,
    R_STD_NET_ERROR_NOT_CONNECTED = 11,
    R_STD_NET_ERROR_CLOSED = 12,
    R_STD_NET_ERROR_PERMISSION_DENIED = 13,
    R_STD_NET_ERROR_RESOURCE_EXHAUSTED = 14,
    R_STD_NET_ERROR_MESSAGE_TOO_LARGE = 15,
    R_STD_NET_ERROR_CANCELLED = 16,
    R_STD_NET_ERROR_TIMED_OUT = 17,
    R_STD_NET_ERROR_UNSUPPORTED = 18,
    R_STD_NET_ERROR_OTHER = 19
} RStdNetErrorCode;

typedef struct RStdNetError {
    RStdNetErrorCode code;
    int64_t native_code;
} RStdNetError;

typedef enum RStdNetFamily {
    R_STD_NET_FAMILY_ANY = 0,
    R_STD_NET_FAMILY_V4 = 1,
    R_STD_NET_FAMILY_V6 = 2
} RStdNetFamily;

typedef struct RStdNetListenOptions {
    uint32_t backlog;
    _Bool reuse_address;
    _Bool v6_only;
} RStdNetListenOptions;

/* R-SLIB-NET-0012: the options of a TCP stream and of a UDP socket. */
typedef struct RStdNetTcpOptions {
    _Bool nodelay;
    RStdTimeDurationOption keepalive;
    uint32_t hop_limit;
    size_t receive_buffer;
    size_t send_buffer;
} RStdNetTcpOptions;

typedef struct RStdNetUdpOptions {
    uint32_t hop_limit;
    size_t receive_buffer;
    size_t send_buffer;
    _Bool broadcast;
    uint32_t multicast_hop_limit;
    _Bool multicast_loop;
} RStdNetUdpOptions;

typedef enum RStdNetShutdownDirection {
    R_STD_NET_SHUTDOWN_READ = 0,
    R_STD_NET_SHUTDOWN_WRITE = 1,
    R_STD_NET_SHUTDOWN_BOTH = 2
} RStdNetShutdownDirection;

typedef struct RStdNetTcpListenerStorage RStdNetTcpListenerStorage;
typedef struct RStdNetTcpStreamStorage RStdNetTcpStreamStorage;
typedef struct RStdNetUdpSocketStorage RStdNetUdpSocketStorage;

/*
 * Move-only Send+Sync handle shells. An initialized shell owns one reference to immutable native
 * identity; successful asynchronous submissions acquire their own reference before publication.
 */
typedef struct RStdNetTcpListener {
    RStdNetTcpListenerStorage *storage;
} RStdNetTcpListener;

typedef struct RStdNetTcpStream {
    RStdNetTcpStreamStorage *storage;
} RStdNetTcpStream;

typedef struct RStdNetUdpSocket {
    RStdNetUdpSocketStorage *storage;
} RStdNetUdpSocket;

typedef struct RStdNetTcpConnection {
    RStdNetTcpStream stream;
    RStdNetSocketAddress peer;
} RStdNetTcpConnection;

/*
 * R-SLIB-NET-0014: Move-only Send+Sync Unix-domain handle shells. A listener and a stream share
 * the storage of their TCP counterparts and a datagram socket that of a UDP socket.
 */
typedef struct RStdNetUnixListener {
    RStdNetTcpListenerStorage *storage;
} RStdNetUnixListener;

typedef struct RStdNetUnixStream {
    RStdNetTcpStreamStorage *storage;
} RStdNetUnixStream;

typedef struct RStdNetUnixDatagram {
    RStdNetUdpSocketStorage *storage;
} RStdNetUnixDatagram;

/* R-SLIB-NET-0016: the effective identifiers and process of a Unix-domain peer. */
typedef struct RStdNetPeerCredentials {
    uint32_t user_id;
    uint32_t group_id;
    int32_t process_id;
} RStdNetPeerCredentials;

/* R-SLIB-NET-0017: one received Unix-domain datagram prefix. */
typedef struct RStdNetUnixMessage {
    size_t count;
    _Bool truncated;
} RStdNetUnixMessage;

typedef enum RStdNetTcpReadResultKind {
    R_STD_NET_TCP_READ_RESULT_READ = 0,
    R_STD_NET_TCP_READ_RESULT_END = 1,
    R_STD_NET_TCP_READ_RESULT_FAILED = 2
} RStdNetTcpReadResultKind;

typedef struct RStdNetTcpReadResult {
    RStdNetTcpReadResultKind kind;
    RRuntimeArray buffer;
    size_t count;
    RStdNetError error;
} RStdNetTcpReadResult;

typedef enum RStdNetTcpWriteResultKind {
    R_STD_NET_TCP_WRITE_RESULT_WRITTEN = 0,
    R_STD_NET_TCP_WRITE_RESULT_FAILED = 1
} RStdNetTcpWriteResultKind;

typedef struct RStdNetTcpWriteResult {
    RStdNetTcpWriteResultKind kind;
    RRuntimeArray buffer;
    size_t written;
    RStdNetError error;
} RStdNetTcpWriteResult;

typedef enum RStdNetTcpWriteAllResultKind {
    R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN = 0,
    R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED = 1
} RStdNetTcpWriteAllResultKind;

typedef struct RStdNetTcpWriteAllResult {
    RStdNetTcpWriteAllResultKind kind;
    RRuntimeArray buffer;
    size_t written;
    RStdNetError error;
} RStdNetTcpWriteAllResult;

typedef enum RStdNetUdpSendResultKind {
    R_STD_NET_UDP_SEND_RESULT_SENT = 0,
    R_STD_NET_UDP_SEND_RESULT_FAILED = 1
} RStdNetUdpSendResultKind;

typedef struct RStdNetUdpSendResult {
    RStdNetUdpSendResultKind kind;
    RRuntimeArray buffer;
    RStdNetError error;
} RStdNetUdpSendResult;

typedef enum RStdNetUdpReceiveResultKind {
    R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED = 0,
    R_STD_NET_UDP_RECEIVE_RESULT_FAILED = 1
} RStdNetUdpReceiveResultKind;

typedef struct RStdNetUdpReceiveResult {
    RStdNetUdpReceiveResultKind kind;
    RRuntimeArray buffer;
    size_t count;
    RStdNetSocketAddress peer;
    _Bool truncated;
    RStdNetError error;
} RStdNetUdpReceiveResult;

typedef struct RStdNetSocketAddressResult {
    _Bool is_ok;
    RStdNetError error;
    RStdNetSocketAddress value;
} RStdNetSocketAddressResult;

typedef struct RStdNetTcpListenerResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetTcpListener r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetTcpListener value;
        RStdNetError error;
    };
} RStdNetTcpListenerResult;

typedef struct RStdNetTcpStreamResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetTcpStream r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetTcpStream value;
        RStdNetError error;
    };
} RStdNetTcpStreamResult;

typedef struct RStdNetTcpConnectionResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetTcpConnection r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetTcpConnection value;
        RStdNetError error;
    };
} RStdNetTcpConnectionResult;

typedef struct RStdNetUdpSocketResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetUdpSocket r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetUdpSocket value;
        RStdNetError error;
    };
} RStdNetUdpSocketResult;

typedef struct RStdNetUnixListenerResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetUnixListener r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetUnixListener value;
        RStdNetError error;
    };
} RStdNetUnixListenerResult;

typedef struct RStdNetUnixStreamResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetUnixStream r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetUnixStream value;
        RStdNetError error;
    };
} RStdNetUnixStreamResult;

typedef struct RStdNetUnixDatagramResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetUnixDatagram r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetUnixDatagram value;
        RStdNetError error;
    };
} RStdNetUnixDatagramResult;

typedef struct RStdNetResolveResult {
    uint32_t r_tag;
    union {
        union {
            RRuntimeArray r_ok;
            RStdNetError r_error_00000001;
        } r_payload;
        RRuntimeArray value;
        RStdNetError error;
    };
} RStdNetResolveResult;

typedef struct RStdNetVoidResult {
    uint32_t r_tag;
    union {
        union {
            RStdNetError r_error_00000001;
        } r_payload;
        RStdNetError error;
    };
} RStdNetVoidResult;

typedef struct RStdNetDeadline {
    _Bool has_value;
    RStdTimeInstant value;
} RStdNetDeadline;

typedef struct RStdNetTaskStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdNetTaskStartResult;

/*
 * Borrowed byte views for the scoped operations. The caller keeps the storage alive and
 * unmoved until the completion result is published; the library never frees or retains it.
 */
typedef struct RStdNetMutableBytes {
    uint8_t *data;
    size_t length;
} RStdNetMutableBytes;

typedef struct RStdNetConstBytes {
    const uint8_t *data;
    size_t length;
} RStdNetConstBytes;

/* Canonical hidden checked carrier: r_tag 0 carries a byte count, 1 carries net_error. */
typedef struct RStdNetCountResult {
    uint32_t r_tag;
    union {
        size_t r_value;
        RStdNetError r_error_00000001;
    } r_payload;
} RStdNetCountResult;

typedef struct RStdNetDatagram {
    size_t count;
    RStdNetSocketAddress peer;
    _Bool truncated;
} RStdNetDatagram;

/* Canonical hidden checked carrier: r_tag 0 carries one Unix-domain message, 1 net_error. */
typedef struct RStdNetUnixMessageResult {
    uint32_t r_tag;
    union {
        RStdNetUnixMessage r_value;
        RStdNetError r_error_00000001;
    } r_payload;
} RStdNetUnixMessageResult;

/* Canonical hidden checked carrier: r_tag 0 carries one received datagram, 1 carries net_error. */
typedef struct RStdNetDatagramResult {
    uint32_t r_tag;
    union {
        RStdNetDatagram r_value;
        RStdNetError r_error_00000001;
    } r_payload;
} RStdNetDatagramResult;

/* CONTRACT_VIOLATION is a compiler/runtime ABI state and is not an R error variant. */
typedef enum RStdNetCallStatus {
    R_STD_NET_CALL_SUCCESS = 0,
    R_STD_NET_CALL_ERROR = 1,
    R_STD_NET_CALL_CONTRACT_VIOLATION = 2
} RStdNetCallStatus;

typedef struct RStdNetIpAddressResult {
    RStdNetCallStatus status;
    RStdNetAddressError error;
    RStdNetIpAddress value;
} RStdNetIpAddressResult;

typedef struct RStdNetStringResult {
    RStdNetCallStatus status;
    RStdAllocError error;
    RStdString value;
} RStdNetStringResult;

/*
 * Ownership: text is a valid UTF-8 call-bounded str borrow and is never retained. Parsing is
 * allocation-free and reports the first byte that establishes an address error.
 */
RStdNetIpAddressResult r_std_net_parse_ip(RStdStringView text);

/*
 * Ownership: allocator is retained by the successful string owner. The address is copied.
 * Allocation failure exposes no partial string.
 */
RStdNetStringResult r_std_net_format_ip(RRuntimeAllocator *allocator, RStdNetIpAddress address);

/* R-TYPE-0046 (L32): the standard texts of addresses; text holds at least 39 bytes for an ip
 * address and 64 bytes for a socket address. Each returns the length written. */
size_t r_library_internal_net_ip_text(RStdNetIpAddress address, uint8_t *text);
size_t r_library_internal_net_socket_text(RStdNetSocketAddress address, uint8_t *text);

/* Exact non-allocating adaptation of std.net::net_error to std.error::error. */
RStdError r_std_net_as_error(RStdNetError value);

/*
 * Ownership: host is copied into task-owned immutable storage before successful eager start and is
 * never retained as a borrow. Success returns the sole owner of a nonempty address array in target
 * resolver callback order. Every start failure leaves the complete host source unchanged.
 */
RStdNetTaskStartResult r_std_net_resolve(RStdStringView host,
                                         uint16_t port,
                                         RStdNetFamily family,
                                         RStdNetDeadline deadline);

/* Bounded, allocation-free observations of a live native socket identity. */
RStdNetSocketAddressResult r_std_net_tcp_listener_local_address(const RStdNetTcpListener *listener);
RStdNetSocketAddressResult r_std_net_tcp_local_address(const RStdNetTcpStream *stream);
RStdNetSocketAddressResult r_std_net_tcp_peer_address(const RStdNetTcpStream *stream);
RStdNetSocketAddressResult r_std_net_udp_local_address(const RStdNetUdpSocket *socket);

typedef struct RStdNetTcpOptionsResult {
    _Bool is_ok;
    RStdNetError error;
    RStdNetTcpOptions value;
} RStdNetTcpOptionsResult;

typedef struct RStdNetUdpOptionsResult {
    _Bool is_ok;
    RStdNetError error;
    RStdNetUdpOptions value;
} RStdNetUdpOptionsResult;

typedef struct RStdNetOptionResult {
    _Bool is_ok;
    RStdNetError error;
} RStdNetOptionResult;

typedef struct RStdNetPeerCredentialsResult {
    _Bool is_ok;
    RStdNetError error;
    RStdNetPeerCredentials value;
} RStdNetPeerCredentialsResult;

/*
 * R-SLIB-NET-0012..0013: bounded native option queries and changes on a live handle; they
 * allocate nothing. A set applies, in field order, only the fields that differ from the current
 * values.
 */
RStdNetTcpOptionsResult r_std_net_tcp_get_options(const RStdNetTcpStream *stream);
RStdNetOptionResult r_std_net_tcp_set_options(const RStdNetTcpStream *stream,
                                              RStdNetTcpOptions options);
RStdNetUdpOptionsResult r_std_net_udp_get_options(const RStdNetUdpSocket *socket);
RStdNetOptionResult r_std_net_udp_set_options(const RStdNetUdpSocket *socket,
                                              RStdNetUdpOptions options);
RStdNetOptionResult r_std_net_udp_join_multicast(const RStdNetUdpSocket *socket,
                                                 RStdNetIpAddress group,
                                                 uint32_t interface_index);
RStdNetOptionResult r_std_net_udp_leave_multicast(const RStdNetUdpSocket *socket,
                                                  RStdNetIpAddress group,
                                                  uint32_t interface_index);

/*
 * Eager native establishment. Address validation and the deadline check run in task context.
 * Complete task/native state is reserved before start commit; start failure has no socket effect.
 * tcp_accept acquires an independent listener retain before returning a successfully started task;
 * accepted connections are serviced by the listener identity's FIFO submission queue.
 * A successful tcp_connect result owns one independently retained Move-only stream. Cancellation
 * keeps its provisional descriptor and Dispatch sources alive through native acknowledgement.
 */
RStdNetTaskStartResult r_std_net_tcp_listen(RStdNetSocketAddress local,
                                            RStdNetListenOptions options,
                                            RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_tcp_accept(const RStdNetTcpListener *listener,
                                            RStdNetDeadline deadline);
/*
 * Ownership: a successful task start consumes listener. Closing rejects new submissions, drains
 * all accepted-operation retains, closes the descriptor and releases the view before terminal
 * task publication; cancellation and an expired deadline do not skip those duties.
 */
RStdNetTaskStartResult r_std_net_tcp_listener_close(RStdNetTcpListener *listener,
                                                    RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_tcp_connect(RStdNetSocketAddress remote, RStdNetDeadline deadline);
/*
 * Ownership: success consumes buffer into an eager task; start failure preserves the complete
 * descriptor and every byte. The borrowed stream is retained by a precommit native reservation.
 * Reads sharing one stream are FIFO; their direction remains independent of future writes.
 */
RStdNetTaskStartResult
r_std_net_tcp_read(const RStdNetTcpStream *stream, RRuntimeArray *buffer, RStdNetDeadline deadline);
/*
 * Ownership: a successful task start consumes buffer into an eager task; start failure preserves
 * the complete descriptor and every byte. Every terminal task outcome returns that exact owner
 * without mutating its bytes. tcp_write reports one positive native prefix for nonempty input;
 * tcp_write_all continues until the complete buffer or a terminal condition. Writes sharing one
 * stream are FIFO and progress independently from reads.
 */
RStdNetTaskStartResult r_std_net_tcp_write(const RStdNetTcpStream *stream,
                                           RRuntimeArray *buffer,
                                           RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_tcp_write_all(const RStdNetTcpStream *stream,
                                               RRuntimeArray *buffer,
                                               RStdNetDeadline deadline);
/*
 * Scoped forms borrow caller storage instead of consuming an owner. target/source stay borrowed,
 * unmoved and unfreed until the completion result is published (after native acknowledgement);
 * start failure leaves them untouched. tcp_read_into completes with RStdNetCountResult carrying
 * the bytes placed at the start of target (0 = end of stream or empty target). tcp_write_from
 * carries one positive native prefix count; tcp_write_all_from completes with RStdNetVoidResult
 * only after every byte or a terminal error. Zero-length views complete successfully without
 * native submission. Failed outcomes carry only the error and never report partial progress.
 * Ordering and direction independence match the owned forms.
 */
RStdNetTaskStartResult r_std_net_tcp_read_into(const RStdNetTcpStream *stream,
                                               RStdNetMutableBytes target,
                                               RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_tcp_write_from(const RStdNetTcpStream *stream,
                                                RStdNetConstBytes source,
                                                RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_tcp_write_all_from(const RStdNetTcpStream *stream,
                                                    RStdNetConstBytes source,
                                                    RStdNetDeadline deadline);
/*
 * Borrows stream through submission and retains its identity on successful eager start. The
 * selected direction is FIFO-ordered with earlier operations in that direction; repeated
 * half-close reserves no native shutdown request and has no native side effect.
 */
RStdNetTaskStartResult r_std_net_tcp_shutdown(const RStdNetTcpStream *stream,
                                              RStdNetShutdownDirection direction,
                                              RStdNetDeadline deadline);
/*
 * Ownership: successful eager start consumes stream. Close rejects new operations, cancels and
 * acknowledges both direction queues, closes the native identity, and releases the consumed view
 * exactly once even when cancellation, deadline, or native failure selects the task outcome.
 */
RStdNetTaskStartResult r_std_net_tcp_close(RStdNetTcpStream *stream, RStdNetDeadline deadline);
RStdNetTaskStartResult
r_std_net_udp_bind(RStdNetSocketAddress local, _Bool reuse_address, RStdNetDeadline deadline);
/*
 * Ownership: successful eager start consumes buffer and acquires an independent socket retain;
 * start failure preserves both. Every task result returns the same array owner, and send never
 * mutates its bytes. One socket serializes sends independently from receives. A zero-length array
 * is submitted as one real empty datagram.
 */
RStdNetTaskStartResult r_std_net_udp_send_to(const RStdNetUdpSocket *socket,
                                             RStdNetSocketAddress peer,
                                             RRuntimeArray *buffer,
                                             RStdNetDeadline deadline);
/*
 * Ownership matches udp_send_to. Receive writes one datagram prefix only after successful native
 * completion; failed and cancelled outcomes preserve every original byte. Receives are FIFO and
 * progress independently from sends.
 */
RStdNetTaskStartResult r_std_net_udp_receive_from(const RStdNetUdpSocket *socket,
                                                  RRuntimeArray *buffer,
                                                  RStdNetDeadline deadline);
/*
 * Scoped datagram forms borrow caller storage until the completion result is published; start
 * failure leaves it untouched. udp_send_from submits one real datagram (a zero-length source is
 * still sent) and completes with RStdNetVoidResult. udp_receive_into copies one datagram prefix
 * into target only after successful native completion and completes with RStdNetDatagramResult
 * (count, peer, truncated as udp_receive_from); failed and cancelled outcomes leave target
 * unchanged and carry only the error.
 */
RStdNetTaskStartResult r_std_net_udp_send_from(const RStdNetUdpSocket *socket,
                                               RStdNetSocketAddress peer,
                                               RStdNetConstBytes source,
                                               RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_udp_receive_into(const RStdNetUdpSocket *socket,
                                                  RStdNetMutableBytes target,
                                                  RStdNetDeadline deadline);
/*
 * Ownership: successful eager start consumes socket. Close rejects new operations, cancels and
 * acknowledges both datagram queues, closes the native identity, and releases the consumed view
 * exactly once even when cancellation, deadline, or native failure selects the task outcome.
 */
RStdNetTaskStartResult r_std_net_udp_close(RStdNetUdpSocket *socket, RStdNetDeadline deadline);

/*
 * R-SLIB-NET-0014..0017: Unix-domain sockets. A path is borrowed only for the start call and
 * converted there; an invalid path completes the task with invalid_address. Every operation
 * otherwise follows the ownership, ordering and terminal rules of its TCP or UDP counterpart:
 * unix_listen/unix_accept/unix_connect those of tcp_listen/tcp_accept/tcp_connect (an accepted
 * stream carries no peer address), the stream operations those of the scoped TCP forms,
 * tcp_shutdown and tcp_close, and the datagram operations those of the scoped UDP forms and
 * udp_close, a send going to the connected peer and a receive reporting no sender.
 * unix_peer_credentials is a bounded, allocation-free native query.
 */
RStdNetPeerCredentialsResult r_std_net_unix_peer_credentials(const RStdNetUnixStream *stream);
RStdNetTaskStartResult r_std_net_unix_listen(RStdStringView path,
                                             uint32_t backlog,
                                             _Bool replace,
                                             RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_accept(const RStdNetUnixListener *listener,
                                             RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_connect(RStdStringView path, RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_read_into(const RStdNetUnixStream *stream,
                                                RStdNetMutableBytes target,
                                                RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_write_from(const RStdNetUnixStream *stream,
                                                 RStdNetConstBytes source,
                                                 RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_write_all_from(const RStdNetUnixStream *stream,
                                                     RStdNetConstBytes source,
                                                     RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_shutdown(const RStdNetUnixStream *stream,
                                               RStdNetShutdownDirection direction,
                                               RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_close(RStdNetUnixStream *stream, RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_listener_close(RStdNetUnixListener *listener,
                                                     RStdNetDeadline deadline);
RStdNetTaskStartResult
r_std_net_unix_datagram_bind(RStdStringView path, _Bool replace, RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_datagram_connect(RStdStringView path,
                                                       RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_send_from(const RStdNetUnixDatagram *socket,
                                                RStdNetConstBytes source,
                                                RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_receive_into(const RStdNetUnixDatagram *socket,
                                                   RStdNetMutableBytes target,
                                                   RStdNetDeadline deadline);
RStdNetTaskStartResult r_std_net_unix_datagram_close(RStdNetUnixDatagram *socket,
                                                     RStdNetDeadline deadline);

/* Private compiler drop/move ABI used by the inline typed glue below. */
void r_library_internal_net_tcp_listener_move(RStdNetTcpListener *destination,
                                              RStdNetTcpListener *source);
void r_library_internal_net_tcp_listener_drop(RStdNetTcpListener *listener);
void r_library_internal_net_tcp_stream_move(RStdNetTcpStream *destination,
                                            RStdNetTcpStream *source);
void r_library_internal_net_tcp_stream_drop(RStdNetTcpStream *stream);
void r_library_internal_net_udp_socket_move(RStdNetUdpSocket *destination,
                                            RStdNetUdpSocket *source);
void r_library_internal_net_udp_socket_drop(RStdNetUdpSocket *socket);
void r_library_internal_net_unix_listener_move(RStdNetUnixListener *destination,
                                               RStdNetUnixListener *source);
void r_library_internal_net_unix_listener_drop(RStdNetUnixListener *listener);
void r_library_internal_net_unix_stream_move(RStdNetUnixStream *destination,
                                             RStdNetUnixStream *source);
void r_library_internal_net_unix_stream_drop(RStdNetUnixStream *stream);
void r_library_internal_net_unix_datagram_move(RStdNetUnixDatagram *destination,
                                               RStdNetUnixDatagram *source);
void r_library_internal_net_unix_datagram_drop(RStdNetUnixDatagram *socket);

static inline void r_std_net_tcp_listener_move_initialize(RStdNetTcpListener *destination,
                                                          RStdNetTcpListener *source) {
    r_library_internal_net_tcp_listener_move(destination, source);
}

static inline void r_std_net_tcp_listener_destroy(RStdNetTcpListener *listener) {
    r_library_internal_net_tcp_listener_drop(listener);
}

static inline void r_std_net_tcp_stream_move_initialize(RStdNetTcpStream *destination,
                                                        RStdNetTcpStream *source) {
    r_library_internal_net_tcp_stream_move(destination, source);
}

static inline void r_std_net_tcp_stream_destroy(RStdNetTcpStream *stream) {
    r_library_internal_net_tcp_stream_drop(stream);
}

static inline void r_std_net_udp_socket_move_initialize(RStdNetUdpSocket *destination,
                                                        RStdNetUdpSocket *source) {
    r_library_internal_net_udp_socket_move(destination, source);
}

static inline void r_std_net_udp_socket_destroy(RStdNetUdpSocket *socket) {
    r_library_internal_net_udp_socket_drop(socket);
}

static inline void r_std_net_unix_listener_move_initialize(RStdNetUnixListener *destination,
                                                           RStdNetUnixListener *source) {
    r_library_internal_net_unix_listener_move(destination, source);
}

static inline void r_std_net_unix_listener_destroy(RStdNetUnixListener *listener) {
    r_library_internal_net_unix_listener_drop(listener);
}

static inline void r_std_net_unix_stream_move_initialize(RStdNetUnixStream *destination,
                                                         RStdNetUnixStream *source) {
    r_library_internal_net_unix_stream_move(destination, source);
}

static inline void r_std_net_unix_stream_destroy(RStdNetUnixStream *stream) {
    r_library_internal_net_unix_stream_drop(stream);
}

static inline void r_std_net_unix_datagram_move_initialize(RStdNetUnixDatagram *destination,
                                                           RStdNetUnixDatagram *source) {
    r_library_internal_net_unix_datagram_move(destination, source);
}

static inline void r_std_net_unix_datagram_destroy(RStdNetUnixDatagram *socket) {
    r_library_internal_net_unix_datagram_drop(socket);
}

static inline void r_std_net_tcp_connection_move_initialize(RStdNetTcpConnection *destination,
                                                            RStdNetTcpConnection *source) {
    *destination = *source;
    *source = (RStdNetTcpConnection){0};
}

static inline void r_std_net_tcp_read_result_move_initialize(RStdNetTcpReadResult *destination,
                                                             RStdNetTcpReadResult *source) {
    *destination = *source;
    *source = (RStdNetTcpReadResult){0};
}

static inline void r_std_net_tcp_write_result_move_initialize(RStdNetTcpWriteResult *destination,
                                                              RStdNetTcpWriteResult *source) {
    *destination = *source;
    *source = (RStdNetTcpWriteResult){0};
}

static inline void
r_std_net_tcp_write_all_result_move_initialize(RStdNetTcpWriteAllResult *destination,
                                               RStdNetTcpWriteAllResult *source) {
    *destination = *source;
    *source = (RStdNetTcpWriteAllResult){0};
}

static inline void r_std_net_udp_send_result_move_initialize(RStdNetUdpSendResult *destination,
                                                             RStdNetUdpSendResult *source) {
    *destination = *source;
    *source = (RStdNetUdpSendResult){0};
}

static inline void
r_std_net_udp_receive_result_move_initialize(RStdNetUdpReceiveResult *destination,
                                             RStdNetUdpReceiveResult *source) {
    *destination = *source;
    *source = (RStdNetUdpReceiveResult){0};
}

static inline void r_std_net_tcp_connection_destroy(RStdNetTcpConnection *connection) {
    r_std_net_tcp_stream_destroy(&connection->stream);
    *connection = (RStdNetTcpConnection){0};
}

static inline void r_std_net_tcp_read_result_destroy(RStdNetTcpReadResult *result) {
    r_runtime_array_destroy(&result->buffer);
    *result = (RStdNetTcpReadResult){0};
}

static inline void r_std_net_tcp_write_result_destroy(RStdNetTcpWriteResult *result) {
    r_runtime_array_destroy(&result->buffer);
    *result = (RStdNetTcpWriteResult){0};
}

static inline void r_std_net_tcp_write_all_result_destroy(RStdNetTcpWriteAllResult *result) {
    r_runtime_array_destroy(&result->buffer);
    *result = (RStdNetTcpWriteAllResult){0};
}

static inline void r_std_net_udp_send_result_destroy(RStdNetUdpSendResult *result) {
    r_runtime_array_destroy(&result->buffer);
    *result = (RStdNetUdpSendResult){0};
}

static inline void r_std_net_udp_receive_result_destroy(RStdNetUdpReceiveResult *result) {
    r_runtime_array_destroy(&result->buffer);
    *result = (RStdNetUdpReceiveResult){0};
}

#endif

#ifndef R_LIBRARY_NET_INTERNAL_H
#define R_LIBRARY_NET_INTERNAL_H

#include "r_runtime_darwin_io.h"
#include "r_std_net.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef enum RLibraryNetDeadlineStatus {
    R_LIBRARY_NET_DEADLINE_READY = 0,
    R_LIBRARY_NET_DEADLINE_EXPIRED,
    R_LIBRARY_NET_DEADLINE_ERROR
} RLibraryNetDeadlineStatus;

typedef enum RLibraryNetHandleKind {
    R_LIBRARY_NET_HANDLE_TCP_LISTENER = 0,
    R_LIBRARY_NET_HANDLE_TCP_STREAM = 1,
    R_LIBRARY_NET_HANDLE_UDP_SOCKET = 2
} RLibraryNetHandleKind;

typedef struct RLibraryNetHandleStorage {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    size_t allocation_alignment;
    pthread_mutex_t mutex;
    int descriptor;
    RLibraryNetHandleKind kind;
    _Bool close_reserved;
    _Bool terminal;
    _Bool read_shutdown;
    _Bool write_shutdown;
} RLibraryNetHandleStorage;

typedef struct RLibraryNetAcceptQueueNode RLibraryNetAcceptQueueNode;
typedef void (*RLibraryNetListenerDrainFn)(void *context);
typedef struct RLibraryNetUdpQueueNode RLibraryNetUdpQueueNode;
typedef void (*RLibraryNetUdpDrainFn)(void *context);

struct RStdNetTcpListenerStorage {
    RLibraryNetHandleStorage handle;
    RLibraryNetAcceptQueueNode *accept_head;
    RLibraryNetAcceptQueueNode *accept_tail;
    RLibraryNetListenerDrainFn accept_drain;
    void *accept_drain_context;
    size_t accept_reservations;
    uint64_t close_sequence;
};

struct RStdNetTcpStreamStorage {
    RLibraryNetHandleStorage handle;
    /*
     * One shared STREAM view supplies FIFO input/output reservations. The runtime serializes each
     * direction independently, so a later public tcp_write can share this identity without being
     * ordered behind reads. It is created lazily before the first data task commits.
     */
    RRuntimeDarwinIoHandle *data_io;
};

struct RStdNetUdpSocketStorage {
    RLibraryNetHandleStorage handle;
    RLibraryNetUdpQueueNode *send_head;
    RLibraryNetUdpQueueNode *send_tail;
    RLibraryNetUdpQueueNode *receive_head;
    RLibraryNetUdpQueueNode *receive_tail;
    RLibraryNetUdpDrainFn drain;
    void *drain_context;
    size_t send_reservations;
    size_t receive_reservations;
    uint64_t close_sequence;
};

RStdNetTcpListenerStorage *
r_library_internal_net_tcp_listener_reserve(RRuntimeAllocator *allocator);
RStdNetTcpStreamStorage *r_library_internal_net_tcp_stream_reserve(RRuntimeAllocator *allocator);
RStdNetUdpSocketStorage *r_library_internal_net_udp_socket_reserve(RRuntimeAllocator *allocator);

void r_library_internal_net_tcp_listener_publish(RStdNetTcpListenerStorage *storage,
                                                 int descriptor);
void r_library_internal_net_tcp_stream_publish(RStdNetTcpStreamStorage *storage, int descriptor);
void r_library_internal_net_udp_socket_publish(RStdNetUdpSocketStorage *storage, int descriptor);

_Bool r_library_internal_net_handle_retain(RLibraryNetHandleStorage *storage);
_Bool r_library_internal_net_handle_retain_descriptor(RLibraryNetHandleStorage *storage,
                                                      RLibraryNetHandleKind expected_kind,
                                                      int *descriptor);
void r_library_internal_net_handle_release(RLibraryNetHandleStorage *storage);

RStdNetSocketAddressResult r_library_internal_net_observe_address(
    RLibraryNetHandleStorage *storage, RLibraryNetHandleKind expected_kind, _Bool peer);

RStdNetError r_library_internal_net_error_from_native(int native_code);
RStdNetTcpOptionsResult r_library_internal_net_tcp_get_options(RLibraryNetHandleStorage *storage);
RStdNetOptionResult r_library_internal_net_tcp_set_options(RLibraryNetHandleStorage *storage,
                                                           RStdNetTcpOptions options);
RStdNetUdpOptionsResult r_library_internal_net_udp_get_options(RLibraryNetHandleStorage *storage);
RStdNetOptionResult r_library_internal_net_udp_set_options(RLibraryNetHandleStorage *storage,
                                                           RStdNetUdpOptions options);
RStdNetOptionResult r_library_internal_net_udp_membership(RLibraryNetHandleStorage *storage,
                                                          RStdNetIpAddress group,
                                                          uint32_t interface_index,
                                                          _Bool join);
_Bool r_library_internal_net_address_from_native(const struct sockaddr *native,
                                                 socklen_t native_length,
                                                 RStdNetSocketAddress *result);
_Bool r_library_internal_net_address_to_native(RStdNetSocketAddress address,
                                               struct sockaddr_storage *native,
                                               socklen_t *native_length,
                                               int *domain);
RLibraryNetDeadlineStatus r_library_internal_net_deadline_timeout(RStdNetDeadline deadline,
                                                                  uint64_t *timeout_nanoseconds,
                                                                  RStdNetError *error);
RStdNetIpAddressResult r_library_internal_net_parse_ip(RStdStringView text);

RStdNetTaskStartResult r_library_internal_net_tcp_listen(RStdNetSocketAddress local,
                                                         RStdNetListenOptions options,
                                                         RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_connect(RStdNetSocketAddress remote,
                                                          RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_accept(const RStdNetTcpListener *listener,
                                                         RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_listener_close(RStdNetTcpListener *listener,
                                                                 RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_read(const RStdNetTcpStream *stream,
                                                       RRuntimeArray *buffer,
                                                       RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_write(const RStdNetTcpStream *stream,
                                                        RRuntimeArray *buffer,
                                                        RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_write_all(const RStdNetTcpStream *stream,
                                                            RRuntimeArray *buffer,
                                                            RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_read_into(const RStdNetTcpStream *stream,
                                                            RStdNetMutableBytes target,
                                                            RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_write_from(const RStdNetTcpStream *stream,
                                                             RStdNetConstBytes source,
                                                             RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_write_all_from(const RStdNetTcpStream *stream,
                                                                 RStdNetConstBytes source,
                                                                 RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_shutdown(const RStdNetTcpStream *stream,
                                                           RStdNetShutdownDirection direction,
                                                           RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_tcp_close(RStdNetTcpStream *stream,
                                                        RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_bind(RStdNetSocketAddress local,
                                                       _Bool reuse_address,
                                                       RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_resolve(RStdStringView host,
                                                      uint16_t port,
                                                      RStdNetFamily family,
                                                      RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_send_to(const RStdNetUdpSocket *socket,
                                                          RStdNetSocketAddress peer,
                                                          RRuntimeArray *buffer,
                                                          RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_receive_from(const RStdNetUdpSocket *socket,
                                                               RRuntimeArray *buffer,
                                                               RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_send_from(const RStdNetUdpSocket *socket,
                                                            RStdNetSocketAddress peer,
                                                            RStdNetConstBytes source,
                                                            RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_receive_into(const RStdNetUdpSocket *socket,
                                                               RStdNetMutableBytes target,
                                                               RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_udp_close(RStdNetUdpSocket *socket,
                                                        RStdNetDeadline deadline);

/*
 * R-SLIB-NET-0014..0017: Unix-domain sockets. A path converts to a sockaddr_un when it is
 * nonempty, shorter than sun_path and free of zero bytes; the handles reuse the TCP listener,
 * TCP stream and UDP socket storages and their operations.
 */
_Bool r_library_internal_net_unix_address(RStdStringView path,
                                          struct sockaddr_un *native,
                                          socklen_t *native_length);
RStdNetPeerCredentialsResult
r_library_internal_net_unix_peer_credentials(const RStdNetUnixStream *stream);
RStdNetTaskStartResult r_library_internal_net_unix_listen(RStdStringView path,
                                                          uint32_t backlog,
                                                          _Bool replace,
                                                          RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_accept(const RStdNetUnixListener *listener,
                                                          RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_connect(RStdStringView path,
                                                           RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_read_into(const RStdNetUnixStream *stream,
                                                             RStdNetMutableBytes target,
                                                             RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_write_from(const RStdNetUnixStream *stream,
                                                              RStdNetConstBytes source,
                                                              RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_write_all_from(const RStdNetUnixStream *stream,
                                                                  RStdNetConstBytes source,
                                                                  RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_shutdown(const RStdNetUnixStream *stream,
                                                            RStdNetShutdownDirection direction,
                                                            RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_close(RStdNetUnixStream *stream,
                                                         RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_listener_close(RStdNetUnixListener *listener,
                                                                  RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_datagram_bind(RStdStringView path,
                                                                 _Bool replace,
                                                                 RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_datagram_connect(RStdStringView path,
                                                                    RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_send_from(const RStdNetUnixDatagram *socket,
                                                             RStdNetConstBytes source,
                                                             RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_receive_into(const RStdNetUnixDatagram *socket,
                                                                RStdNetMutableBytes target,
                                                                RStdNetDeadline deadline);
RStdNetTaskStartResult r_library_internal_net_unix_datagram_close(RStdNetUnixDatagram *socket,
                                                                  RStdNetDeadline deadline);

typedef struct RLibraryNetTcpReadPrepareResult {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
    _Bool end;
} RLibraryNetTcpReadPrepareResult;

typedef struct RLibraryNetTcpWritePrepareResult {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
    _Bool write_shutdown;
} RLibraryNetTcpWritePrepareResult;

typedef struct RLibraryNetTcpShutdownPrepareResult {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RStdNetTcpStreamStorage *storage;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
    _Bool already_shutdown;
} RLibraryNetTcpShutdownPrepareResult;

typedef struct RLibraryNetTcpClosePrepareResult {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoHandle *data_io;
    RRuntimeDarwinIoStartStatus status;
    RStdNetError preexisting_error;
    int native_error;
    _Bool preexisting_failure;
} RLibraryNetTcpClosePrepareResult;

RLibraryNetTcpReadPrepareResult
r_library_internal_net_tcp_stream_prepare_read(RStdNetTcpStreamStorage *stream,
                                               const RRuntimeDarwinIoBuffer *buffer,
                                               uint64_t timeout_nanoseconds);
RLibraryNetTcpWritePrepareResult
r_library_internal_net_tcp_stream_prepare_write(RStdNetTcpStreamStorage *stream,
                                                const RRuntimeDarwinIoBuffer *buffer,
                                                uint64_t timeout_nanoseconds,
                                                _Bool write_all);
RLibraryNetTcpShutdownPrepareResult
r_library_internal_net_tcp_stream_prepare_shutdown(RStdNetTcpStreamStorage *stream,
                                                   RStdNetShutdownDirection direction,
                                                   uint64_t timeout_nanoseconds,
                                                   _Bool prepare_native);
RLibraryNetTcpClosePrepareResult
r_library_internal_net_tcp_stream_prepare_close(RStdNetTcpStreamStorage *stream,
                                                uint64_t timeout_nanoseconds);
_Bool r_library_internal_net_tcp_stream_mark_closing(RStdNetTcpStreamStorage *stream);
int r_library_internal_net_tcp_stream_take_close_descriptor(RStdNetTcpStreamStorage *stream,
                                                            RRuntimeDarwinIoHandle **data_io);

_Bool r_library_internal_net_tcp_listener_mark_closing(RStdNetTcpListenerStorage *listener);
void r_library_internal_net_tcp_listener_drain_accepts(RStdNetTcpListenerStorage *listener,
                                                       RLibraryNetListenerDrainFn drain,
                                                       void *context);
int r_library_internal_net_tcp_listener_take_close_descriptor(RStdNetTcpListenerStorage *listener);

_Bool r_library_internal_net_udp_socket_preflight_close(RStdNetUdpSocketStorage *socket,
                                                        RStdNetError *preexisting_error,
                                                        _Bool *preexisting_failure);
_Bool r_library_internal_net_udp_socket_mark_closing(RStdNetUdpSocketStorage *socket);
void r_library_internal_net_udp_socket_drain(RStdNetUdpSocketStorage *socket,
                                             RLibraryNetUdpDrainFn drain,
                                             void *context);
int r_library_internal_net_udp_socket_take_close_descriptor(RStdNetUdpSocketStorage *socket);

#if defined(R_LIBRARY_NET_TESTING)
enum {
    R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_NONE = 0U,
    R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_SUCCESS = 1U,
    R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_ERROR = 2U,
    R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_DEADLINE = 3U,
    R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_TASK_CANCEL = 4U
};

void r_library_internal_net_listener_close_testing_reset(void);
void r_library_internal_net_listener_close_testing_pause_before_drain_selection(_Bool enabled);
void r_library_internal_net_listener_close_testing_wait_before_drain_selection(void);
void r_library_internal_net_listener_close_testing_pause_after_close_selection(_Bool enabled);
void r_library_internal_net_listener_close_testing_wait_after_close_selection(void);
void r_library_internal_net_listener_close_testing_pause_before_start_selection(_Bool enabled);
void r_library_internal_net_listener_close_testing_wait_before_start_selection(void);
_Bool r_library_internal_net_listener_close_testing_wait_prestart_cancellation(void);
void r_library_internal_net_listener_close_testing_wait_deadline_reported(void);
void r_library_internal_net_listener_close_testing_wait_cancel_reported(void);
unsigned int r_library_internal_net_listener_close_testing_selected_outcome(void);
void r_library_internal_net_tcp_read_testing_arm_cancel_acknowledgement(void);
void r_library_internal_net_tcp_read_testing_wait_cancel_acknowledgement(void);
void r_library_internal_net_tcp_write_testing_arm_cancel_acknowledgement(void);
void r_library_internal_net_tcp_write_testing_wait_cancel_reported(void);
void r_library_internal_net_tcp_write_testing_wait_cancel_acknowledgement(void);
void r_library_internal_net_tcp_close_testing_arm_cancel_acknowledgement(void);
void r_library_internal_net_tcp_close_testing_wait_cancel_reported(void);
void r_library_internal_net_tcp_close_testing_wait_cancel_acknowledgement(void);
void r_library_internal_net_udp_testing_arm_cancel_acknowledgement(void);
void r_library_internal_net_udp_testing_wait_cancel_reported(void);
void r_library_internal_net_udp_testing_wait_cancel_acknowledgement(void);
#endif

#endif

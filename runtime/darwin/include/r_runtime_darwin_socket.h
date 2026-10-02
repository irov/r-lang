#ifndef R_RUNTIME_DARWIN_SOCKET_H
#define R_RUNTIME_DARWIN_SOCKET_H

#include "r_runtime_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

typedef struct RRuntimeDarwinSocketConnect RRuntimeDarwinSocketConnect;
typedef struct RRuntimeDarwinSocketAccept RRuntimeDarwinSocketAccept;
typedef struct RRuntimeDarwinSocketDatagram RRuntimeDarwinSocketDatagram;

typedef enum RRuntimeDarwinSocketPrepareStatus {
    R_RUNTIME_DARWIN_SOCKET_PREPARE_OK = 0,
    R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID,
    R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED
} RRuntimeDarwinSocketPrepareStatus;

typedef enum RRuntimeDarwinSocketTerminalEvent {
    R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE = 0,
    R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED,
    R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT
} RRuntimeDarwinSocketTerminalEvent;

typedef struct RRuntimeDarwinSocketPrepareResult {
    RRuntimeDarwinSocketConnect *request;
    RRuntimeDarwinSocketPrepareStatus status;
    int native_error;
} RRuntimeDarwinSocketPrepareResult;

typedef struct RRuntimeDarwinSocketConnectResult {
    RRuntimeDarwinSocketTerminalEvent terminal_event;
    int native_error;
    uint64_t terminal_event_sequence;
    _Bool connected;
} RRuntimeDarwinSocketConnectResult;

typedef struct RRuntimeDarwinSocketAcceptPrepareResult {
    RRuntimeDarwinSocketAccept *request;
    RRuntimeDarwinSocketPrepareStatus status;
    int native_error;
} RRuntimeDarwinSocketAcceptPrepareResult;

typedef struct RRuntimeDarwinSocketAcceptResult {
    RRuntimeDarwinSocketTerminalEvent terminal_event;
    int native_error;
    uint64_t terminal_event_sequence;
    struct sockaddr_storage peer;
    socklen_t peer_length;
    _Bool accepted;
} RRuntimeDarwinSocketAcceptResult;

typedef enum RRuntimeDarwinSocketDatagramOperation {
    R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND = 0,
    R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE
} RRuntimeDarwinSocketDatagramOperation;

typedef struct RRuntimeDarwinSocketDatagramPrepareResult {
    RRuntimeDarwinSocketDatagram *request;
    RRuntimeDarwinSocketPrepareStatus status;
    int native_error;
} RRuntimeDarwinSocketDatagramPrepareResult;

typedef struct RRuntimeDarwinSocketDatagramResult {
    RRuntimeDarwinSocketDatagramOperation operation;
    RRuntimeDarwinSocketTerminalEvent terminal_event;
    int native_error;
    uint64_t terminal_event_sequence;
    struct sockaddr_storage peer;
    socklen_t peer_length;
    size_t count;
    _Bool completed;
    _Bool truncated;
} RRuntimeDarwinSocketDatagramResult;

/* The request borrow is valid only for the duration of the completion callback. */
typedef void (*RRuntimeDarwinSocketConnectCompletionFn)(RRuntimeDarwinSocketConnect *request,
                                                        void *context);
typedef void (*RRuntimeDarwinSocketAcceptCompletionFn)(RRuntimeDarwinSocketAccept *request,
                                                       void *context);
typedef void (*RRuntimeDarwinSocketDatagramCompletionFn)(RRuntimeDarwinSocketDatagram *request,
                                                         void *context);

/*
 * Two-phase accept reservation. listener_descriptor remains borrowed through completion and must
 * name an open nonblocking close-on-exec listening socket. Success reserves suspended Dispatch
 * sources but performs no accept submission. timeout_nanoseconds == 0 means no timer.
 */
RRuntimeDarwinSocketAcceptPrepareResult r_runtime_darwin_socket_accept_prepare(
    RRuntimeAllocator *allocator, int listener_descriptor, uint64_t timeout_nanoseconds);

/* Installs the sole completion observer without activating either Dispatch source. */
_Bool r_runtime_darwin_socket_accept_bind(RRuntimeDarwinSocketAccept *request,
                                          RRuntimeDarwinSocketAcceptCompletionFn completion,
                                          void *context);

/*
 * Arms only the optional deadline source. This lets a higher-level FIFO keep read readiness
 * suspended while queued time still counts toward the operation's absolute deadline.
 */
_Bool r_runtime_darwin_socket_accept_arm_deadline(RRuntimeDarwinSocketAccept *request);

/*
 * Activates readiness observation. accept is invoked only by the Dispatch read handler. Repeated
 * activation and activation after an already selected terminal event are successful no-ops.
 */
_Bool r_runtime_darwin_socket_accept_activate(RRuntimeDarwinSocketAccept *request);

/*
 * Before completion delivery, selects cancellation only when no earlier terminal event was
 * captured. After delivery it returns false and the callback-visible result remains immutable.
 */
_Bool r_runtime_darwin_socket_accept_cancel(RRuntimeDarwinSocketAccept *request,
                                            uint64_t cancellation_sequence);

/* Available after the completion callback begins; it never blocks. */
RRuntimeDarwinSocketAcceptResult
r_runtime_darwin_socket_accept_result(RRuntimeDarwinSocketAccept *request);

/* Transfers the accepted descriptor exactly once; every other state returns -1. */
int r_runtime_darwin_socket_accept_take_descriptor(RRuntimeDarwinSocketAccept *request);

/* Completed requests use release; prepared-but-unsubmitted requests use accept_abort. */
void r_runtime_darwin_socket_accept_release(RRuntimeDarwinSocketAccept *request);
void r_runtime_darwin_socket_accept_abort(RRuntimeDarwinSocketAccept **request);

/*
 * Two-phase connect reservation. Success owns one nonblocking close-on-exec socket and suspended
 * Dispatch sources, but performs no connect submission. timeout_nanoseconds == 0 means no timer.
 * Native socket creation failure is distinct from allocation/source reservation failure.
 */
RRuntimeDarwinSocketPrepareResult
r_runtime_darwin_socket_connect_prepare(RRuntimeAllocator *allocator,
                                        const struct sockaddr *remote,
                                        socklen_t remote_length,
                                        uint64_t timeout_nanoseconds);

/*
 * Installs the sole completion observer without submission. Binding is allocation-free and makes
 * a later pre-submission cancellation observable through the same callback protocol.
 */
_Bool r_runtime_darwin_socket_connect_bind(RRuntimeDarwinSocketConnect *request,
                                           RRuntimeDarwinSocketConnectCompletionFn completion,
                                           void *context);

/*
 * Consumes the prepared state transition and submits the nonblocking connect. The callback runs
 * exactly once after every Dispatch source cancel handler has acknowledged that it can no longer
 * access the request.
 */
_Bool r_runtime_darwin_socket_connect_activate(RRuntimeDarwinSocketConnect *request);

/*
 * Before completion delivery, selects cancellation only when no earlier terminal event was
 * captured. After delivery it returns false and the callback-visible result remains immutable.
 */
_Bool r_runtime_darwin_socket_connect_cancel(RRuntimeDarwinSocketConnect *request,
                                             uint64_t cancellation_sequence);

/* Available after the completion callback begins; it never blocks. */
RRuntimeDarwinSocketConnectResult
r_runtime_darwin_socket_connect_result(RRuntimeDarwinSocketConnect *request);

/* Transfers the connected descriptor exactly once; every other state returns -1. */
int r_runtime_darwin_socket_connect_take_descriptor(RRuntimeDarwinSocketConnect *request);

/*
 * Consumes the request after completion callback delivery. Prepared-but-unsubmitted requests use
 * connect_abort instead so suspended Dispatch objects are destroyed without callbacks.
 */
void r_runtime_darwin_socket_connect_release(RRuntimeDarwinSocketConnect *request);
void r_runtime_darwin_socket_connect_abort(RRuntimeDarwinSocketConnect **request);

/*
 * Two-phase datagram reservation. descriptor is borrowed through completion and must remain an
 * open nonblocking close-on-exec SOCK_DGRAM identity. Send borrows data without mutation; receive
 * reserves independent bounce storage so a failed/cancelled outcome cannot alter the caller's
 * buffer. A zero-size send remains a real native datagram submission. A NULL peer with length
 * zero sends to the peer the socket is connected to.
 */
RRuntimeDarwinSocketDatagramPrepareResult
r_runtime_darwin_socket_datagram_send_prepare(RRuntimeAllocator *allocator,
                                              int descriptor,
                                              const struct sockaddr *peer,
                                              socklen_t peer_length,
                                              const void *data,
                                              size_t size,
                                              uint64_t timeout_nanoseconds);
RRuntimeDarwinSocketDatagramPrepareResult r_runtime_darwin_socket_datagram_receive_prepare(
    RRuntimeAllocator *allocator, int descriptor, size_t capacity, uint64_t timeout_nanoseconds);

/* Binding and deadline arming are allocation-free. Readiness activation is separately FIFO-gated.
 */
_Bool r_runtime_darwin_socket_datagram_bind(RRuntimeDarwinSocketDatagram *request,
                                            RRuntimeDarwinSocketDatagramCompletionFn completion,
                                            void *context);
_Bool r_runtime_darwin_socket_datagram_arm_deadline(RRuntimeDarwinSocketDatagram *request);
_Bool r_runtime_darwin_socket_datagram_activate(RRuntimeDarwinSocketDatagram *request);
_Bool r_runtime_darwin_socket_datagram_cancel(RRuntimeDarwinSocketDatagram *request,
                                              uint64_t cancellation_sequence);

/* Result/copy are callback-bounded and nonblocking. Copy succeeds only for received completion. */
RRuntimeDarwinSocketDatagramResult
r_runtime_darwin_socket_datagram_result(RRuntimeDarwinSocketDatagram *request);
_Bool r_runtime_darwin_socket_datagram_copy_received(RRuntimeDarwinSocketDatagram *request,
                                                     void *destination,
                                                     size_t capacity);
void r_runtime_darwin_socket_datagram_release(RRuntimeDarwinSocketDatagram *request);
void r_runtime_darwin_socket_datagram_abort(RRuntimeDarwinSocketDatagram **request);

#if defined(R_RUNTIME_DARWIN_SOCKET_TESTING)
void r_runtime_darwin_socket_testing_pause_next_datagram_before_native(void);
void r_runtime_darwin_socket_testing_wait_for_datagram_before_native(void);
void r_runtime_darwin_socket_testing_release_datagram_before_native(void);
void r_runtime_darwin_socket_testing_fail_next_datagram_native(int native_error);
#endif

#endif

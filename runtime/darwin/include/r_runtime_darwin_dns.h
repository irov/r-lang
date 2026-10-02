#ifndef R_RUNTIME_DARWIN_DNS_H
#define R_RUNTIME_DARWIN_DNS_H

#include "r_runtime_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

typedef struct RRuntimeDarwinDnsRequest RRuntimeDarwinDnsRequest;

typedef enum RRuntimeDarwinDnsProtocol {
    R_RUNTIME_DARWIN_DNS_PROTOCOL_V4 = 1,
    R_RUNTIME_DARWIN_DNS_PROTOCOL_V6 = 2,
    R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY = 3
} RRuntimeDarwinDnsProtocol;

typedef enum RRuntimeDarwinDnsPrepareStatus {
    R_RUNTIME_DARWIN_DNS_PREPARE_OK = 0,
    R_RUNTIME_DARWIN_DNS_PREPARE_INVALID,
    R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED
} RRuntimeDarwinDnsPrepareStatus;

typedef enum RRuntimeDarwinDnsTerminalEvent {
    R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE = 0,
    R_RUNTIME_DARWIN_DNS_TERMINAL_CANCELLED,
    R_RUNTIME_DARWIN_DNS_TERMINAL_TIMED_OUT
} RRuntimeDarwinDnsTerminalEvent;

typedef enum RRuntimeDarwinDnsResultStatus {
    R_RUNTIME_DARWIN_DNS_RESULT_ADDRESSES = 0,
    R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR,
    R_RUNTIME_DARWIN_DNS_RESULT_ALLOCATION_FAILED
} RRuntimeDarwinDnsResultStatus;

typedef struct RRuntimeDarwinDnsPrepareResult {
    RRuntimeDarwinDnsRequest *request;
    RRuntimeDarwinDnsPrepareStatus status;
} RRuntimeDarwinDnsPrepareResult;

typedef struct RRuntimeDarwinDnsResult {
    RRuntimeDarwinDnsTerminalEvent terminal_event;
    RRuntimeDarwinDnsResultStatus status;
    int32_t native_error;
    uint64_t terminal_event_sequence;
    size_t address_count;
} RRuntimeDarwinDnsResult;

/* The request borrow is valid only for the duration of the completion callback. */
typedef void (*RRuntimeDarwinDnsCompletionFn)(RRuntimeDarwinDnsRequest *request, void *context);

/*
 * Two-phase resolver reservation. Success owns an immutable hostname copy, private serial queue
 * and optional suspended deadline source, but performs no DNSServiceGetAddrInfo submission.
 * reserve_deadline reserves all native timer state required by a later set_timeout call.
 */
RRuntimeDarwinDnsPrepareResult r_runtime_darwin_dns_prepare(RRuntimeAllocator *allocator,
                                                            const char *hostname,
                                                            size_t hostname_length,
                                                            RRuntimeDarwinDnsProtocol protocol,
                                                            _Bool reserve_deadline);

/* Installs the sole observer and configures already-reserved timer state before activation. */
_Bool r_runtime_darwin_dns_bind(RRuntimeDarwinDnsRequest *request,
                                RRuntimeDarwinDnsCompletionFn completion,
                                void *context);
_Bool r_runtime_darwin_dns_set_timeout(RRuntimeDarwinDnsRequest *request,
                                       uint64_t timeout_nanoseconds);

/* Immutable task-owned hostname view, valid until release or abort. */
_Bool r_runtime_darwin_dns_hostname(RRuntimeDarwinDnsRequest *request,
                                    const char **hostname,
                                    size_t *hostname_length);

/*
 * Submits DNSServiceGetAddrInfo on the request's private serial queue. Every DNS callback,
 * DNSServiceRefDeallocate and terminal acknowledgement executes on that same queue.
 */
_Bool r_runtime_darwin_dns_activate(RRuntimeDarwinDnsRequest *request);

/*
 * Before completion delivery, queues cancellation using the supplied shared event sequence.
 * Completion is delivered only after DNSServiceRef deallocation and timer cancellation quiesce.
 */
_Bool r_runtime_darwin_dns_cancel(RRuntimeDarwinDnsRequest *request,
                                  uint64_t cancellation_sequence);

/* Callback-bounded immutable result access. */
RRuntimeDarwinDnsResult r_runtime_darwin_dns_result(RRuntimeDarwinDnsRequest *request);
_Bool r_runtime_darwin_dns_copy_address(RRuntimeDarwinDnsRequest *request,
                                        size_t index,
                                        struct sockaddr_storage *address,
                                        socklen_t *address_length);

/* Completed requests use release; prepared-but-unsubmitted requests use abort. */
void r_runtime_darwin_dns_release(RRuntimeDarwinDnsRequest *request);
void r_runtime_darwin_dns_abort(RRuntimeDarwinDnsRequest **request);

#endif

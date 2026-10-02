#ifndef R_RUNTIME_DARWIN_SIGNAL_H
#define R_RUNTIME_DARWIN_SIGNAL_H

#include "r_runtime_allocator.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Process signals delivered as task completions (Library R-SLIB-SIGNAL-0001..0003). No R code and
 * no allocation run in a signal handler: a Dispatch signal source per watched signal counts the
 * deliveries and serves the waits of every listener of that signal.
 */
typedef enum RRuntimeDarwinSignalKind {
    R_RUNTIME_DARWIN_SIGNAL_TERMINATE = 0,
    R_RUNTIME_DARWIN_SIGNAL_INTERRUPT,
    R_RUNTIME_DARWIN_SIGNAL_HANGUP,
    R_RUNTIME_DARWIN_SIGNAL_USER1,
    R_RUNTIME_DARWIN_SIGNAL_USER2,
    R_RUNTIME_DARWIN_SIGNAL_KIND_COUNT
} RRuntimeDarwinSignalKind;

typedef struct RRuntimeDarwinSignalListener RRuntimeDarwinSignalListener;
typedef struct RRuntimeDarwinSignalWait RRuntimeDarwinSignalWait;

typedef enum RRuntimeDarwinSignalStatus {
    R_RUNTIME_DARWIN_SIGNAL_OK = 0,
    R_RUNTIME_DARWIN_SIGNAL_INVALID,
    R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_SIGNAL_NATIVE_FAILED
} RRuntimeDarwinSignalStatus;

typedef struct RRuntimeDarwinSignalListenResult {
    RRuntimeDarwinSignalListener *listener;
    RRuntimeDarwinSignalStatus status;
    int native_error;
} RRuntimeDarwinSignalListenResult;

typedef struct RRuntimeDarwinSignalWaitPrepareResult {
    RRuntimeDarwinSignalWait *wait;
    RRuntimeDarwinSignalStatus status;
} RRuntimeDarwinSignalWaitPrepareResult;

typedef enum RRuntimeDarwinSignalTerminalEvent {
    R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED = 0,
    R_RUNTIME_DARWIN_SIGNAL_TERMINAL_TIMED_OUT,
    R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED
} RRuntimeDarwinSignalTerminalEvent;

typedef struct RRuntimeDarwinSignalWaitResult {
    RRuntimeDarwinSignalTerminalEvent terminal_event;
    uint64_t terminal_event_sequence;
    uint64_t count;
} RRuntimeDarwinSignalWaitResult;

typedef void (*RRuntimeDarwinSignalWaitCompletionFn)(RRuntimeDarwinSignalWait *wait, void *context);

/* The native signal number of a kind. */
int r_runtime_darwin_signal_number(RRuntimeDarwinSignalKind kind);

/*
 * Ownership: success returns one listener reference. The first listener of a kind registers the
 * Dispatch source of its signal, waits until the kernel records deliveries and then replaces the
 * disposition of the signal by ignore, keeping the previous one; deliveries after the return
 * are counted for this listener until it is released.
 */
RRuntimeDarwinSignalListenResult r_runtime_darwin_signal_listen(RRuntimeAllocator *allocator,
                                                                RRuntimeDarwinSignalKind kind);
void r_runtime_darwin_signal_listener_retain(RRuntimeDarwinSignalListener *listener);

/*
 * Releases one reference. The last reference of the last listener of a kind restores the kept
 * disposition of its signal and cancels its source; uncounted deliveries are then dropped.
 */
void r_runtime_darwin_signal_listener_release(RRuntimeDarwinSignalListener *listener);

/* Sends the signal of kind to this process; returns zero or the native error. */
int r_runtime_darwin_signal_raise(RRuntimeDarwinSignalKind kind);

/*
 * The signals whose disposition a listener replaced: a spawned child starts with their kept
 * dispositions reset to the default (POSIX_SPAWN_SETSIGDEF).
 */
void r_runtime_darwin_signal_replaced_set(sigset_t *signals);

/*
 * One wait for the next deliveries of a listener. prepare retains the listener and creates a
 * suspended strict timer, which fires after timeout_nanoseconds when has_timeout is set. bind
 * stores the completion, which runs once after the selected terminal event and after the timer
 * can no longer run; activate completes the wait at once when deliveries are already counted and
 * otherwise queues it behind the earlier waits of the listener. cancel selects cancellation
 * unless an earlier event was selected; a selected delivery that cancellation overtakes returns
 * its count to the listener, as does return_count after a completion that the task did not
 * accept.
 */
RRuntimeDarwinSignalWaitPrepareResult
r_runtime_darwin_signal_wait_prepare(RRuntimeAllocator *allocator,
                                     RRuntimeDarwinSignalListener *listener,
                                     _Bool has_timeout,
                                     uint64_t timeout_nanoseconds);
void r_runtime_darwin_signal_wait_bind(RRuntimeDarwinSignalWait *wait,
                                       RRuntimeDarwinSignalWaitCompletionFn completion,
                                       void *completion_context);
void r_runtime_darwin_signal_wait_activate(RRuntimeDarwinSignalWait *wait);
/*
 * The slot forms read and clear the wait under the registry lock: a cancellation that races with
 * the completion of the same wait finds either the live wait or an empty slot.
 */
void r_runtime_darwin_signal_wait_cancel(RRuntimeDarwinSignalWait *const *wait_slot,
                                         uint64_t cancellation_sequence);
RRuntimeDarwinSignalWaitResult r_runtime_darwin_signal_wait_result(RRuntimeDarwinSignalWait *wait);
void r_runtime_darwin_signal_wait_return_count(RRuntimeDarwinSignalWait *wait);
void r_runtime_darwin_signal_wait_release(RRuntimeDarwinSignalWait **wait_slot);
void r_runtime_darwin_signal_wait_abort(RRuntimeDarwinSignalWait **wait_slot);

#endif

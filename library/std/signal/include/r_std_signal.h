#ifndef R_STD_SIGNAL_H
#define R_STD_SIGNAL_H

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_process.h"

#include <stdint.h>

struct RRuntimeDarwinSignalListener;

/* Library R-SLIB-SIGNAL-0001: the process signals a program may wait for. */
typedef enum RStdSignalKind {
    R_STD_SIGNAL_KIND_TERMINATE = 0,
    R_STD_SIGNAL_KIND_INTERRUPT = 1,
    R_STD_SIGNAL_KIND_HANGUP = 2,
    R_STD_SIGNAL_KIND_USER1 = 3,
    R_STD_SIGNAL_KIND_USER2 = 4
} RStdSignalKind;

/*
 * Opaque Move-only, Send+Sync listener handle. storage retains one runtime listener, which counts
 * the deliveries of its signal from its creation until the last retain ends.
 */
typedef struct RStdSignalListener {
    struct RRuntimeDarwinSignalListener *storage;
} RStdSignalListener;

typedef struct RStdSignalListenerResult {
    RStdProcessCallStatus status;
    RStdProcessError error;
    RStdSignalListener value;
} RStdSignalListenerResult;

/* The checked result of `next`: r_tag 0 carries the count, 1 the process error. */
typedef struct RStdSignalNextResult {
    uint32_t r_tag;
    union {
        uint64_t r_ok;
        RStdProcessError r_error_00000001;
    } r_payload;
} RStdSignalNextResult;

/*
 * Ownership: success returns the sole owner of a new listener retained by allocator. Failure
 * reports resource_exhausted, for allocation or native registration, with the native code.
 */
RStdSignalListenerResult r_std_signal_listen(RRuntimeAllocator *allocator, RStdSignalKind kind);

/*
 * Ownership: listener is borrowed for the call; a started task holds its own retain until its
 * terminal event. The task result is RStdSignalNextResult.
 */
RStdProcessTaskStartResult r_std_signal_next(const RStdSignalListener *listener,
                                             RStdProcessDeadline deadline);

/* Sends the signal of kind to this process. */
RStdProcessVoidResult r_std_signal_raise(RStdSignalKind kind);

/* Private compiler move/drop ABI used by the inline typed glue below. */
void r_library_internal_signal_listener_move(RStdSignalListener *destination,
                                             RStdSignalListener *source);
void r_library_internal_signal_listener_destroy(RStdSignalListener *listener);

static inline void r_std_signal_listener_move_initialize(RStdSignalListener *destination,
                                                         RStdSignalListener *source) {
    r_library_internal_signal_listener_move(destination, source);
}

static inline void r_std_signal_listener_destroy(RStdSignalListener *listener) {
    r_library_internal_signal_listener_destroy(listener);
}

#endif

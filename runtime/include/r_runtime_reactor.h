#ifndef R_RUNTIME_REACTOR_H
#define R_RUNTIME_REACTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A source of the reactor thread (runtime/r/reactor.r): a one-shot alarm of the monotonic clock,
 * or a watch of the read or write readiness of a descriptor. Its owner keeps the storage live
 * until the source settles, or, for an alarm without a settle callback, while it is armed. Every
 * change of its native registration and every call of its callbacks happens on the reactor
 * thread; other threads only post commands. Members are runtime-private.
 */
typedef struct RRuntimeReactorSource {
    struct RRuntimeReactorSource *next;
    /* Both run on the reactor thread with the source, whose context names its owner. */
    void (*fire)(struct RRuntimeReactorSource *source);
    void (*settle)(struct RRuntimeReactorSource *source);
    void *context;
    uint64_t deadline;
    uintptr_t ident;
    int filter;
    uint32_t command;
    uint32_t state;
} RRuntimeReactorSource;

/*
 * A job of the worker pool (runtime/r/task.r): run is called once, on a worker, with the job,
 * whose context names its owner; run may release the storage of the job.
 */
typedef struct RRuntimeJob {
    struct RRuntimeJob *next;
    void (*run)(struct RRuntimeJob *job);
    void *context;
} RRuntimeJob;

#ifdef __cplusplus
}
#endif

#endif

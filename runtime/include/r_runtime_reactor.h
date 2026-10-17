#ifndef R_RUNTIME_REACTOR_H
#define R_RUNTIME_REACTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A one-shot alarm of the reactor thread (runtime/r/reactor.r). Its owner keeps the storage live
 * until the alarm settles, or, for an alarm without a settle callback, while it is armed. Every
 * change of its native registration and every call of its callbacks happens on the reactor
 * thread; other threads only post commands. Members are runtime-private.
 */
typedef struct RRuntimeReactorAlarm {
    struct RRuntimeReactorAlarm *next;
    /* Both run on the reactor thread with the alarm, whose context names its owner. */
    void (*fire)(struct RRuntimeReactorAlarm *alarm);
    void (*settle)(struct RRuntimeReactorAlarm *alarm);
    void *context;
    uint64_t deadline;
    uint32_t command;
    uint32_t state;
} RRuntimeReactorAlarm;

#ifdef __cplusplus
}
#endif

#endif

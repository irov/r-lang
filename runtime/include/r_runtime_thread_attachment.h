#ifndef R_RUNTIME_THREAD_ATTACHMENT_H
#define R_RUNTIME_THREAD_ATTACHMENT_H

#include <fenv.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RRuntimeThreadAttachStatus {
    R_RUNTIME_THREAD_ATTACH_OK = 0,
    R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING = 1,
    R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED = 2,
    R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE = 3
} RRuntimeThreadAttachStatus;

typedef struct RRuntimeThreadAttachment {
    fenv_t caller_environment;
    uint64_t generation;
    _Bool active;
} RRuntimeThreadAttachment;

typedef struct RRuntimeThreadAttachResult {
    RRuntimeThreadAttachStatus status;
    RRuntimeThreadAttachment attachment;
} RRuntimeThreadAttachResult;

/* Stack-owned internal guards; neither changes the public std.c attachment ABI. */
typedef struct RRuntimeCEnvironment {
    fenv_t environment;
} RRuntimeCEnvironment;

typedef struct RRuntimeCEntryGuard {
    RRuntimeThreadAttachment attachment;
    fenv_t environment;
    _Bool owns_attachment;
    _Bool active;
    struct RRuntimeCEntryGuard *previous;
} RRuntimeCEntryGuard;

/* Hosted lifecycle hooks. Start must precede attachment; stop drains every live attachment. */
_Bool r_runtime_thread_lifecycle_start(void);
void r_runtime_thread_lifecycle_stop(void);

RRuntimeThreadAttachResult r_runtime_thread_attach(void);
void r_runtime_thread_detach(RRuntimeThreadAttachment *attachment);

/* Begin initializes fresh stack storage; end must match the active entry on this thread. */
void r_runtime_c_entry_begin(RRuntimeCEntryGuard *guard);
void r_runtime_c_entry_end(RRuntimeCEntryGuard *guard);

/* Outbound calls isolate fenv without creating or consuming a thread attachment. */
void r_runtime_c_call_begin(RRuntimeCEnvironment *environment);
void r_runtime_c_call_end(RRuntimeCEnvironment *environment);

#ifdef __cplusplus
}
#endif

#endif

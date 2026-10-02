#ifndef R_RUNTIME_CORE_H
#define R_RUNTIME_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The profile-independent runtime surface of generated C17: panic categories, source spans, the
 * entry stack preflight and the thread-local destruction hook. Every declaration compiles in a
 * freestanding C17 environment; the hosted runtime extends this surface in r_runtime_0_1.h.
 */

typedef enum RRuntimePanicCategory {
    R_RUNTIME_PANIC_EXPLICIT = 1,
    R_RUNTIME_PANIC_BOUNDS,
    R_RUNTIME_PANIC_INTEGER_OVERFLOW,
    R_RUNTIME_PANIC_DIVISION_BY_ZERO,
    R_RUNTIME_PANIC_INVALID_SHIFT,
    R_RUNTIME_PANIC_INVALID_CONVERSION,
    R_RUNTIME_PANIC_ALLOCATION_FAILURE,
    R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
    R_RUNTIME_PANIC_SCOPED_THREAD_PANIC,
    R_RUNTIME_PANIC_ONCE_POISONED,
    R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME,
    R_RUNTIME_PANIC_STACK_EXHAUSTION,
    R_RUNTIME_PANIC_CONTRACT_VIOLATION
} RRuntimePanicCategory;

typedef struct RRuntimeSourceSpan {
    uint32_t module;
    uint32_t start;
    uint32_t end;
} RRuntimeSourceSpan;

typedef void (*RRuntimeThreadLocalCleanupFn)(void);

/*
 * Begins the panic of one R-ERR-0004 category and never returns to generated code. The hosted
 * runtime emits one bounded diagnostic and aborts; the freestanding runtime hands the panic to the
 * environment handler.
 */
_Noreturn void r_runtime_panic(RRuntimePanicCategory category, RRuntimeSourceSpan span);

/* Returns one stable program-lifetime lowercase R-ERR-0004 category name. */
const char *r_runtime_panic_category_name(RRuntimePanicCategory category);

/*
 * Entry stack preflight (R-FUNC-0004). Each participating thread establishes its own immutable
 * bounds before its first require. Without bounds, require reports stack exhaustion. frame_bytes
 * is the static bound of the entered R code, checked once at the entry.
 */
_Bool r_runtime_stack_initialize_current_thread(void);
void r_runtime_stack_require(size_t frame_bytes, RRuntimeSourceSpan span);
/* Reports whether r_runtime_stack_require(frame_bytes) would succeed on this thread now. */
_Bool r_runtime_stack_can_require(size_t frame_bytes);

/*
 * Installs the generated program's thread-local destruction entry. Runtime-managed R entry
 * points call the entry before a thread leaves R code for the last time.
 */
void r_runtime_thread_local_cleanup_install(RRuntimeThreadLocalCleanupFn cleanup);

#ifdef __cplusplus
}
#endif

#endif

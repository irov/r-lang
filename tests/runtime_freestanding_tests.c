#include "r_runtime_freestanding.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The test process plays the environment of the freestanding profile: it supplies the panic
 * handler, adopts the pthread stack of the current thread and observes every panic through a
 * jump back into the test.
 */

static jmp_buf r_test_panic_jump;
static RRuntimePanicCategory r_test_panic_category;
static RRuntimeSourceSpan r_test_panic_span;
static int r_test_panic_armed;
static int r_test_thread_exit_calls;

static void r_test_require(_Bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "runtime freestanding test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

_Noreturn void r_runtime_environment_panic(RRuntimePanicCategory category,
                                           RRuntimeSourceSpan span) {
    if (!r_test_panic_armed) {
        (void)fprintf(
            stderr, "runtime freestanding test failed: unexpected panic %d\n", (int)category);
        exit(EXIT_FAILURE);
    }
    r_test_panic_category = category;
    r_test_panic_span = span;
    r_test_panic_armed = 0;
    longjmp(r_test_panic_jump, 1);
}

static void r_test_thread_exit_hook(void) {
    r_test_thread_exit_calls += 1;
}

static _Bool r_test_adopt_current_thread(void) {
    void *address = pthread_get_stackaddr_np(pthread_self());
    size_t size = pthread_get_stacksize_np(pthread_self());
    uintptr_t high = (uintptr_t)address;

    return (address != NULL) && (size != 0U) && (high >= size) &&
           r_runtime_freestanding_stack_adopt(high - size, high);
}

static void r_test_category_names(void) {
    static const struct {
        RRuntimePanicCategory category;
        const char *name;
    } expected[] = {
        {R_RUNTIME_PANIC_EXPLICIT, "explicit"},
        {R_RUNTIME_PANIC_BOUNDS, "bounds"},
        {R_RUNTIME_PANIC_INTEGER_OVERFLOW, "integer_overflow"},
        {R_RUNTIME_PANIC_DIVISION_BY_ZERO, "division_by_zero"},
        {R_RUNTIME_PANIC_INVALID_SHIFT, "invalid_shift"},
        {R_RUNTIME_PANIC_INVALID_CONVERSION, "invalid_conversion"},
        {R_RUNTIME_PANIC_ALLOCATION_FAILURE, "allocation_failure"},
        {R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW, "reference_count_overflow"},
        {R_RUNTIME_PANIC_SCOPED_THREAD_PANIC, "scoped_thread_panic"},
        {R_RUNTIME_PANIC_ONCE_POISONED, "once_poisoned"},
        {R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME, "thread_local_lifetime"},
        {R_RUNTIME_PANIC_STACK_EXHAUSTION, "stack_exhaustion"},
        {R_RUNTIME_PANIC_CONTRACT_VIOLATION, "contract_violation"},
    };
    size_t index;

    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        r_test_require(strcmp(r_runtime_panic_category_name(expected[index].category),
                              expected[index].name) == 0,
                       "panic category name mismatch");
    }
    r_test_require(
        strcmp(r_runtime_panic_category_name((RRuntimePanicCategory)0), "contract_violation") == 0,
        "unknown category shall name contract_violation");
}

static void r_test_panic_reaches_environment(void) {
    const RRuntimeSourceSpan span = {UINT32_C(3), UINT32_C(10), UINT32_C(20)};

    r_test_panic_armed = 1;
    if (setjmp(r_test_panic_jump) == 0) {
        r_runtime_panic(R_RUNTIME_PANIC_EXPLICIT, span);
    }
    r_test_require(r_test_panic_category == R_RUNTIME_PANIC_EXPLICIT, "explicit category");
    r_test_require((r_test_panic_span.module == UINT32_C(3)) &&
                       (r_test_panic_span.start == UINT32_C(10)) &&
                       (r_test_panic_span.end == UINT32_C(20)),
                   "panic span forwarded");
}

static void r_test_require_without_bounds(void) {
    const RRuntimeSourceSpan span = {UINT32_C(1), UINT32_C(0), UINT32_C(1)};

    r_test_require(!r_runtime_stack_initialize_current_thread(), "no bounds before adoption");
    r_test_panic_armed = 1;
    if (setjmp(r_test_panic_jump) == 0) {
        r_runtime_stack_require(0U, span);
    }
    r_test_require(r_test_panic_category == R_RUNTIME_PANIC_STACK_EXHAUSTION,
                   "require without bounds is stack exhaustion");
}

static void r_test_adopted_bounds(void) {
    const RRuntimeSourceSpan span = {UINT32_C(1), UINT32_C(2), UINT32_C(3)};

    r_test_require(r_test_adopt_current_thread(), "adopt the pthread stack");
    r_test_require(r_runtime_stack_initialize_current_thread(), "bounds adopted");
    r_test_panic_armed = 0;
    r_runtime_stack_require(0U, span);
    r_runtime_stack_require(4096U, span);
    r_test_panic_armed = 1;
    if (setjmp(r_test_panic_jump) == 0) {
        r_runtime_stack_require(SIZE_MAX, span);
    }
    r_test_require(r_test_panic_category == R_RUNTIME_PANIC_STACK_EXHAUSTION,
                   "unrepresentable frame is stack exhaustion");
    r_test_panic_armed = 1;
    if (setjmp(r_test_panic_jump) == 0) {
        r_runtime_stack_require((size_t)1 << 40, span);
    }
    r_test_require(r_test_panic_category == R_RUNTIME_PANIC_STACK_EXHAUSTION,
                   "frame beyond the adopted stack is stack exhaustion");
    r_runtime_freestanding_stack_release();
    r_test_require(!r_runtime_stack_initialize_current_thread(), "release forgets the bounds");
    r_test_panic_armed = 1;
    if (setjmp(r_test_panic_jump) == 0) {
        r_runtime_stack_require(0U, span);
    }
    r_test_require(r_test_panic_category == R_RUNTIME_PANIC_STACK_EXHAUSTION,
                   "require after release is stack exhaustion");
}

static void r_test_rejected_ranges(void) {
    unsigned char marker;
    const uintptr_t current = (uintptr_t)&marker;

    r_test_require(!r_runtime_freestanding_stack_adopt((uintptr_t)0, (uintptr_t)0),
                   "empty range is rejected");
    r_test_require(!r_runtime_freestanding_stack_adopt((uintptr_t)64, (uintptr_t)32),
                   "inverted range is rejected");
    r_test_require(!r_runtime_freestanding_stack_adopt(UINTPTR_MAX - (uintptr_t)16, UINTPTR_MAX),
                   "range without a protected band is rejected");
    r_test_require(!r_runtime_freestanding_stack_adopt(current + (uintptr_t)1048576,
                                                       current + (uintptr_t)2097152),
                   "range that excludes the current position is rejected");
    r_test_require(!r_runtime_stack_initialize_current_thread(),
                   "a rejected range installs no bounds");
}

static void r_test_thread_exit_hook_runs(void) {
    r_runtime_freestanding_thread_exit();
    r_test_require(r_test_thread_exit_calls == 0, "no hook before install");
    r_runtime_thread_local_cleanup_install(r_test_thread_exit_hook);
    r_runtime_freestanding_thread_exit();
    r_runtime_freestanding_thread_exit();
    r_test_require(r_test_thread_exit_calls == 2, "installed hook runs on every thread exit");
    r_runtime_thread_local_cleanup_install(NULL);
    r_runtime_freestanding_thread_exit();
    r_test_require(r_test_thread_exit_calls == 2, "uninstalled hook does not run");
}

int main(void) {
    r_test_category_names();
    r_test_panic_reaches_environment();
    r_test_require_without_bounds();
    r_test_adopted_bounds();
    r_test_rejected_ranges();
    r_test_thread_exit_hook_runs();
    (void)puts("runtime freestanding tests passed");
    return EXIT_SUCCESS;
}

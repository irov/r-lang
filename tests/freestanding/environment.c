/*
 * Hosted test environment for the freestanding profile: adopts the pthread stack of the calling
 * thread, supplies the panic handler and drives the entries of
 * tests/fixtures/freestanding_program.r.
 */
#include "r_runtime_freestanding.h"

#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int twice(int value);
int select_byte(size_t index);
int entries(void);
int dropped(void);

static jmp_buf r_environment_panic_jump;
static RRuntimePanicCategory r_environment_panic_category;
static int r_environment_panic_armed;

static void r_environment_require(_Bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "freestanding environment failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

_Noreturn void r_runtime_environment_panic(RRuntimePanicCategory category,
                                           RRuntimeSourceSpan span) {
    (void)span;
    if (!r_environment_panic_armed) {
        (void)fprintf(stderr,
                      "freestanding environment failed: unexpected panic %s\n",
                      r_runtime_panic_category_name(category));
        exit(EXIT_FAILURE);
    }
    r_environment_panic_category = category;
    r_environment_panic_armed = 0;
    longjmp(r_environment_panic_jump, 1);
}

static _Bool r_environment_adopt_current_thread(void) {
    void *address = pthread_get_stackaddr_np(pthread_self());
    size_t size = pthread_get_stacksize_np(pthread_self());
    uintptr_t high = (uintptr_t)address;

    return (address != NULL) && (size != 0U) && (high >= size) &&
           r_runtime_freestanding_stack_adopt(high - size, high);
}

int main(void) {
    int32_t status;

    r_environment_require(r_environment_adopt_current_thread(), "adopt the pthread stack");
    status = r_freestanding_main();
    r_environment_require(status == 0, "r_freestanding_main status");
    r_environment_require(dropped() == 1, "static object dropped after main");
    r_environment_require(twice(21) == 42, "twice");
    r_environment_require(select_byte(2U) == 30, "select_byte");
    r_environment_require(entries() == 1, "thread-local entry count");
    r_environment_panic_armed = 1;
    if (setjmp(r_environment_panic_jump) == 0) {
        (void)select_byte(4U);
        r_environment_require(0, "out-of-bounds index shall panic");
    }
    r_environment_require(r_environment_panic_category == R_RUNTIME_PANIC_BOUNDS,
                          "bounds panic category");
    r_runtime_freestanding_stack_release();
    r_environment_panic_armed = 1;
    if (setjmp(r_environment_panic_jump) == 0) {
        (void)twice(1);
        r_environment_require(0, "entry without stack bounds shall panic");
    }
    r_environment_require(r_environment_panic_category == R_RUNTIME_PANIC_STACK_EXHAUSTION,
                          "stack exhaustion category");
    r_runtime_freestanding_thread_exit();
    (void)puts("freestanding program passed");
    return EXIT_SUCCESS;
}

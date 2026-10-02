#include "r_runtime_0_1.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    R_TEST_SMALL_STACK_SIZE = 256 * 1024,
    R_TEST_PRESSURE_FRAME_SIZE = 112 * 1024,
    R_TEST_LARGE_CALLEE_FRAME_SIZE = 96 * 1024,
    R_TEST_LARGE_CALLEE_BOUND = 100 * 1024
};

#if defined(__clang__)
#define R_TEST_NOINLINE __attribute__((noinline, optnone))
#elif defined(__GNUC__)
#define R_TEST_NOINLINE __attribute__((noinline, optimize("O0")))
#else
#error "runtime Darwin stack test requires a noinline attribute"
#endif

typedef void (*RTestChildAction)(void);

typedef struct RTestSafeStackContext {
    _Bool initialized;
} RTestSafeStackContext;

static void r_test_require(_Bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "runtime Darwin stack test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void *r_test_safe_small_stack(void *raw_context) {
    RTestSafeStackContext *context = raw_context;

    context->initialized = r_runtime_stack_initialize_current_thread();
    if (context->initialized) {
        r_runtime_stack_require(4096U, (RRuntimeSourceSpan){1U, 2U, 3U});
    }
    return NULL;
}

static void r_test_uninitialized_require(void) {
    r_runtime_stack_require(0U, (RRuntimeSourceSpan){3U, 5U, 8U});
}

static void *r_test_oversized_small_stack(void *raw_context) {
    (void)raw_context;
    if (!r_runtime_stack_initialize_current_thread()) {
        _exit(121);
    }
    r_runtime_stack_require((size_t)R_TEST_SMALL_STACK_SIZE * 2U,
                            (RRuntimeSourceSpan){9U, 13U, 21U});
    return NULL;
}

static void r_test_small_stack_require(void) {
    pthread_attr_t attributes;
    pthread_t thread;

    if (pthread_attr_init(&attributes) != 0) {
        _exit(122);
    }
    if (pthread_attr_setstacksize(&attributes, (size_t)R_TEST_SMALL_STACK_SIZE) != 0) {
        (void)pthread_attr_destroy(&attributes);
        _exit(123);
    }
    if (pthread_create(&thread, &attributes, r_test_oversized_small_stack, NULL) != 0) {
        (void)pthread_attr_destroy(&attributes);
        _exit(124);
    }
    (void)pthread_attr_destroy(&attributes);
    if (pthread_join(thread, NULL) != 0) {
        _exit(125);
    }
    _exit(126);
}

static void r_test_overflow_require(void) {
    if (!r_runtime_stack_initialize_current_thread()) {
        _exit(127);
    }
    r_runtime_stack_require(SIZE_MAX, (RRuntimeSourceSpan){11U, 34U, 55U});
}

static R_TEST_NOINLINE void r_test_large_callee(void) {
    volatile unsigned char frame[R_TEST_LARGE_CALLEE_FRAME_SIZE];
    size_t offset;

    for (offset = 0U; offset < sizeof(frame); offset += 4096U) {
        frame[offset] = (unsigned char)(offset / 4096U);
    }
    frame[sizeof(frame) - 1U] = 1U;
    _exit(130);
}

static R_TEST_NOINLINE void r_test_preflight_under_pressure(void) {
    volatile unsigned char pressure[R_TEST_PRESSURE_FRAME_SIZE];
    size_t offset;

    for (offset = 0U; offset < sizeof(pressure); offset += 4096U) {
        pressure[offset] = (unsigned char)(offset / 4096U);
    }
    r_runtime_stack_require((size_t)R_TEST_LARGE_CALLEE_BOUND, (RRuntimeSourceSpan){14U, 22U, 35U});
    r_test_large_callee();
    if (pressure[0] != 0U) {
        _exit(131);
    }
}

static void *r_test_real_large_callee_thread(void *raw_context) {
    (void)raw_context;
    if (!r_runtime_stack_initialize_current_thread()) {
        _exit(132);
    }
    r_test_preflight_under_pressure();
    return NULL;
}

static void r_test_real_large_callee_gate(void) {
    pthread_attr_t attributes;
    pthread_t thread;

    if (pthread_attr_init(&attributes) != 0) {
        _exit(133);
    }
    if (pthread_attr_setstacksize(&attributes, (size_t)R_TEST_SMALL_STACK_SIZE) != 0) {
        (void)pthread_attr_destroy(&attributes);
        _exit(134);
    }
    if (pthread_create(&thread, &attributes, r_test_real_large_callee_thread, NULL) != 0) {
        (void)pthread_attr_destroy(&attributes);
        _exit(135);
    }
    (void)pthread_attr_destroy(&attributes);
    if (pthread_join(thread, NULL) != 0) {
        _exit(136);
    }
    _exit(137);
}

static void r_test_expect_stack_panic(RTestChildAction action, const char *expected) {
    char output[256];
    size_t output_length = 0U;
    int descriptors[2];
    pid_t child;
    int status;

    r_test_require(pipe(descriptors) == 0, "create diagnostic pipe");
    child = fork();
    r_test_require(child >= (pid_t)0, "fork diagnostic child");
    if (child == (pid_t)0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDERR_FILENO) != STDERR_FILENO) {
            _exit(128);
        }
        (void)close(descriptors[1]);
        action();
        _exit(129);
    }

    r_test_require(close(descriptors[1]) == 0, "close parent diagnostic writer");
    while (output_length < sizeof(output)) {
        ssize_t read_length =
            read(descriptors[0], output + output_length, sizeof(output) - output_length);

        if (read_length > 0) {
            output_length += (size_t)read_length;
        } else if (read_length == 0) {
            break;
        } else if (errno != EINTR) {
            r_test_require(0, "read stack panic diagnostic");
        }
    }
    r_test_require(close(descriptors[0]) == 0, "close parent diagnostic reader");
    r_test_require(waitpid(child, &status, 0) == child, "wait for diagnostic child");
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
        (void)fprintf(stderr,
                      "runtime Darwin stack test observed wait status %d for diagnostic '%s'\n",
                      status,
                      expected);
    }
    r_test_require(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
                   "stack preflight must abort with SIGABRT");
    r_test_require(output_length == strlen(expected), "stack panic diagnostic length mismatch");
    r_test_require(memcmp(output, expected, output_length) == 0,
                   "stack panic diagnostic text mismatch");
}

static void r_test_safe_small_stack_thread(void) {
    RTestSafeStackContext context = {0};
    pthread_attr_t attributes;
    pthread_t thread;

    r_test_require(pthread_attr_init(&attributes) == 0, "initialize small-stack attributes");
    r_test_require(pthread_attr_setstacksize(&attributes, (size_t)R_TEST_SMALL_STACK_SIZE) == 0,
                   "set small stack size");
    r_test_require(pthread_create(&thread, &attributes, r_test_safe_small_stack, &context) == 0,
                   "create safe small-stack thread");
    r_test_require(pthread_attr_destroy(&attributes) == 0, "destroy safe small-stack attributes");
    r_test_require(pthread_join(thread, NULL) == 0, "join safe small-stack thread");
    r_test_require(context.initialized, "initialize safe small-stack bounds");
}

int main(void) {
    r_test_expect_stack_panic(r_test_uninitialized_require,
                              "R panic: stack_exhaustion at module 3 bytes [5,8)\n");
    r_test_require(r_runtime_stack_initialize_current_thread(), "initialize main-thread bounds");
    r_runtime_stack_require(0U, (RRuntimeSourceSpan){0U, 0U, 0U});
    r_runtime_stack_require(4096U, (RRuntimeSourceSpan){0U, 0U, 0U});
    r_test_safe_small_stack_thread();
    r_test_expect_stack_panic(r_test_small_stack_require,
                              "R panic: stack_exhaustion at module 9 bytes [13,21)\n");
    r_test_expect_stack_panic(r_test_real_large_callee_gate,
                              "R panic: stack_exhaustion at module 14 bytes [22,35)\n");
    r_test_expect_stack_panic(r_test_overflow_require,
                              "R panic: stack_exhaustion at module 11 bytes [34,55)\n");
    (void)fprintf(stdout, "runtime_darwin_stack_tests: ok\n");
    return EXIT_SUCCESS;
}

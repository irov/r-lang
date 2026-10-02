#include "r_runtime_0_1.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct PanicNameCase {
    RRuntimePanicCategory category;
    const char *name;
} PanicNameCase;

static void require(_Bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "runtime panic test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void require_panic_output(RRuntimePanicCategory category,
                                 RRuntimeSourceSpan span,
                                 const char *expected) {
    char output[256];
    size_t output_length = 0U;
    int descriptors[2];
    pid_t child;
    int status;

    require(pipe(descriptors) == 0, "create panic diagnostic pipe");
    child = fork();
    require(child >= (pid_t)0, "fork panic diagnostic child");
    if (child == (pid_t)0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDERR_FILENO) != STDERR_FILENO) {
            _exit(120);
        }
        (void)close(descriptors[1]);
        r_runtime_panic(category, span);
    }

    require(close(descriptors[1]) == 0, "close parent panic diagnostic writer");
    while (output_length < sizeof(output)) {
        ssize_t read_length =
            read(descriptors[0], output + output_length, sizeof(output) - output_length);

        if (read_length > 0) {
            output_length += (size_t)read_length;
        } else if (read_length == 0) {
            break;
        } else if (errno != EINTR) {
            require(0, "read panic diagnostic");
        }
    }
    require(close(descriptors[0]) == 0, "close parent panic diagnostic reader");
    require(waitpid(child, &status, 0) == child, "wait for panic diagnostic child");
    require(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
            "panic diagnostic child must abort");
    require(output_length == strlen(expected), "panic diagnostic length mismatch");
    require(memcmp(output, expected, output_length) == 0, "panic diagnostic text mismatch");
}

int main(void) {
    static const PanicNameCase cases[] = {
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

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        require(strcmp(r_runtime_panic_category_name(cases[index].category), cases[index].name) ==
                    0,
                "stable category name mismatch");
    }
    require(strcmp(r_runtime_panic_category_name((RRuntimePanicCategory)0), "contract_violation") ==
                0,
            "invalid runtime category must be diagnosed as contract violation");
    require_panic_output(R_RUNTIME_PANIC_BOUNDS,
                         (RRuntimeSourceSpan){7U, 11U, 29U},
                         "R panic: bounds at module 7 bytes [11,29)\n");
    require_panic_output((RRuntimePanicCategory)0,
                         (RRuntimeSourceSpan){UINT32_MAX, UINT32_MAX, UINT32_MAX},
                         "R panic: contract_violation at module 4294967295 bytes "
                         "[4294967295,4294967295)\n");
    (void)fprintf(stdout, "runtime_panic_tests: ok\n");
    return EXIT_SUCCESS;
}

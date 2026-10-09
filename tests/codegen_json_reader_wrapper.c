#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static RRuntimeAllocator r_json_test_allocator;
RRuntimeAllocator *r_json_test_hosted_allocator(void);
RRuntimeAllocator *r_json_test_hosted_allocator(void) {
    return &r_json_test_allocator;
}
int r_json_generated_main(int argc, char *argv[]);
#define r_runtime_hosted_allocator r_json_test_hosted_allocator
#define main r_json_generated_main
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_hosted_allocator

static int run_program(int argc, char *argv[]) {
    const char source[] =
        "{\"id\":\"18446744073709551615\",\"Display-Name\":\"Madrid\"} -42 \"tail\"";
    int descriptors[2];
    if (pipe(descriptors) != 0)
        return 103;
    int saved = dup(STDIN_FILENO);
    if (saved < 0 || dup2(descriptors[0], STDIN_FILENO) != STDIN_FILENO)
        return 104;
    (void)close(descriptors[0]);
    if (write(descriptors[1], source, sizeof(source) - 1U) != (ssize_t)(sizeof(source) - 1U))
        return 105;
    (void)close(descriptors[1]);
    int status = r_json_generated_main(argc, argv);
    if (dup2(saved, STDIN_FILENO) != STDIN_FILENO)
        return 106;
    (void)close(saved);
    return status;
}
/* Fixtures map allocation/start failures to 99/97; examples leave them to the implicit main
 * boundary of R-FUNC-0008, which reports 112 (allocation) or 116 (async runtime). */
static int r_json_allocation_status(int status) {
    return status == 99 || status == 97 || status == 112 || status == 116;
}

int main(int argc, char *argv[]) {
    r_runtime_allocator_initialize(&r_json_test_allocator);
    int status = run_program(argc, argv);
    uint64_t attempts = r_runtime_allocator_attempt_count(&r_json_test_allocator);
    if (status != 0 || attempts == 0U) {
        (void)fprintf(stderr,
                      "JSON reader baseline status=%d allocations=%llu\n",
                      status,
                      (unsigned long long)attempts);
        return 101;
    }
    for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
        r_runtime_allocator_set_failure(&r_json_test_allocator, failure);
        status = run_program(argc, argv);
        if ((failure > attempts && status != 0) ||
            (status != 0 && !r_json_allocation_status(status))) {
            (void)fprintf(stderr,
                          "JSON reader allocation failure %llu/%llu: status=%d\n",
                          (unsigned long long)failure,
                          (unsigned long long)attempts,
                          status);
            return 102;
        }
    }
    return 0;
}

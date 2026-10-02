#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <stdio.h>

static RRuntimeAllocator r_json_test_allocator;
static RRuntimeAllocator *r_json_test_hosted_allocator(void) {
    return &r_json_test_allocator;
}

int r_json_generated_main(int argc, char *argv[]);

#define r_runtime_hosted_allocator r_json_test_hosted_allocator
#define main r_json_generated_main
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_hosted_allocator

/* Fixtures map allocation/start failures to 99/97; examples leave them to the implicit main
 * boundary of R-FUNC-0008, which reports 112 (allocation) or 116 (async runtime). */
static int r_json_allocation_status(int status) {
    return status == 99 || status == 97 || status == 112 || status == 116;
}

int main(int argc, char *argv[]) {
    r_runtime_allocator_initialize(&r_json_test_allocator);
    int status = r_json_generated_main(argc, argv);
    uint64_t attempts = r_runtime_allocator_attempt_count(&r_json_test_allocator);
    if (status != 0 || attempts == 0U) {
        (void)fprintf(stderr,
                      "JSON baseline status=%d allocations=%llu\n",
                      status,
                      (unsigned long long)attempts);
        return 101;
    }
    for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
        r_runtime_allocator_set_failure(&r_json_test_allocator, failure);
        status = r_json_generated_main(argc, argv);
        if ((failure > attempts && status != 0) ||
            (failure <= attempts && status != 0 && !r_json_allocation_status(status))) {
            (void)fprintf(stderr,
                          "JSON allocation failure %llu/%llu: status=%d\n",
                          (unsigned long long)failure,
                          (unsigned long long)attempts,
                          status);
            return 102;
        }
    }
    return 0;
}

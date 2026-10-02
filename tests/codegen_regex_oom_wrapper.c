#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"

#include <stdio.h>

static RRuntimeAllocator r_regex_test_allocator;

static RRuntimeAllocator *r_regex_test_hosted_allocator(void) {
    return &r_regex_test_allocator;
}

int r_regex_generated_main(int argc, char *argv[]);

#define r_runtime_hosted_allocator r_regex_test_hosted_allocator
#define main r_regex_generated_main
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_hosted_allocator

int main(int argc, char *argv[]) {
    r_runtime_allocator_initialize(&r_regex_test_allocator);
    int status = r_regex_generated_main(argc, argv);
    uint64_t attempts = r_runtime_allocator_attempt_count(&r_regex_test_allocator);
    if (status != 0 || attempts == 0U) {
        return 101;
    }
    for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
        r_runtime_allocator_set_failure(&r_regex_test_allocator, failure);
        status = r_regex_generated_main(argc, argv);
        if (status != (failure <= attempts ? 99 : 0)) {
            (void)fprintf(stderr,
                          "regex allocation failure %llu/%llu: status=%d\n",
                          (unsigned long long)failure,
                          (unsigned long long)attempts,
                          status);
            return 102;
        }
    }
    (void)printf("regex allocation failures checked: %llu\n", (unsigned long long)attempts);
    return 0;
}

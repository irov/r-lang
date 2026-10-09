#include "r_runtime_0_1.h"
#include "r_std_format.h"
#include "r_std_string.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* R-INIT-0004, R-OWN-0019 (L33): the program initializes a struct whose field initializers allocate
 * and takes one. The k-th attempt fails the k-th allocation; each attempt shall throw
 * or complete, and every string the program produced shall be destroyed exactly once. */

static RRuntimeAllocator r_test_allocator;
static void *r_test_owned[256];
static size_t r_test_owned_count;

RRuntimeAllocator *r_test_hosted_allocator(void);
RRuntimeAllocator *r_test_hosted_allocator(void) {
    return &r_test_allocator;
}

static void r_test_record(void *data) {
    if (data == NULL) {
        return;
    }
    if (r_test_owned_count == sizeof(r_test_owned) / sizeof(r_test_owned[0])) {
        abort();
    }
    for (size_t index = 0U; index < r_test_owned_count; ++index) {
        if (r_test_owned[index] == data) {
            abort();
        }
    }
    r_test_owned[r_test_owned_count++] = data;
}

static void r_test_release(void *data) {
    for (size_t index = 0U; index < r_test_owned_count; ++index) {
        if (r_test_owned[index] == data) {
            r_test_owned[index] = r_test_owned[--r_test_owned_count];
            return;
        }
    }
}

RStdStringAllocValueResult r_test_from_str(RRuntimeAllocator *allocator, RStdStringView view);
RStdStringAllocValueResult r_test_from_str(RRuntimeAllocator *allocator, RStdStringView view) {
    RStdStringAllocValueResult result = r_std_string_from_str(allocator, view);
    if (result.status == R_STD_STRING_CALL_SUCCESS) {
        r_test_record(result.value.bytes.data);
    }
    return result;
}

RStdString r_test_finish(RStdFormatBuilder *builder);
RStdString r_test_finish(RStdFormatBuilder *builder) {
    RStdString result = r_std_format_finish(builder);
    r_test_record(result.bytes.data);
    return result;
}

void r_test_string_destroy(RStdString *source);
void r_test_string_destroy(RStdString *source) {
    r_test_release(source->bytes.data);
    r_std_string_destroy(source);
}

#define r_runtime_hosted_allocator r_test_hosted_allocator
#define r_std_string_from_str r_test_from_str
#define r_std_format_finish r_test_finish
#define r_std_string_destroy r_test_string_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_string_destroy
#undef r_std_format_finish
#undef r_std_string_from_str
#undef r_runtime_hosted_allocator

int main(int argc, char *argv[]) {
    unsigned long thrown = 0U;
    r_runtime_allocator_initialize(&r_test_allocator);
    int status = r_generated_main(argc, argv);
    const uint64_t attempts = r_runtime_allocator_attempt_count(&r_test_allocator);
    if (status != 0 || attempts == 0U || r_test_owned_count != 0U) {
        (void)fprintf(stderr,
                      "format baseline status=%d attempts=%llu live=%zu\n",
                      status,
                      (unsigned long long)attempts,
                      r_test_owned_count);
        return 101;
    }
    for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
        r_runtime_allocator_set_failure(&r_test_allocator, failure);
        status = r_generated_main(argc, argv);
        if ((status != 99 && status != 0) || (failure > attempts && status != 0) ||
            r_test_owned_count != 0U) {
            (void)fprintf(stderr,
                          "format failure %llu/%llu: status=%d live=%zu\n",
                          (unsigned long long)failure,
                          (unsigned long long)attempts,
                          status,
                          r_test_owned_count);
            return 102;
        }
        thrown += status == 99 ? 1U : 0U;
    }
    /* Every attempt before the last reaches an allocation that fails. */
    return thrown == (unsigned long)attempts ? 0 : 103;
}

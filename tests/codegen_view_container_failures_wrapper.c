#include "r_runtime_allocator.h"
#include "r_runtime_list.h"

#include <stdbool.h>
#include <stdint.h>

static RRuntimeAllocationStatus r_test_allocator_allocate(RRuntimeAllocator *allocator,
                                                          size_t size,
                                                          size_t alignment,
                                                          void **result);

#define r_runtime_allocator_allocate r_test_allocator_allocate
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_allocator_allocate

static bool borrow_failed;
static bool value_failed;

/* The generated list helper allocates one node with the element inline. The first node for a
 * `const Point*` and the first node for an `i32` fail, so that each R catch clause receives its
 * staged element back and checks it. */
static RRuntimeAllocationStatus r_test_allocator_allocate(RRuntimeAllocator *allocator,
                                                          size_t size,
                                                          size_t alignment,
                                                          void **result) {
    if (!borrow_failed && (size == sizeof(RRuntimeListNode) + sizeof(void *))) {
        borrow_failed = true;
        *result = NULL;
        return R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (!value_failed && (size == sizeof(RRuntimeListNode) + sizeof(int32_t))) {
        value_failed = true;
        *result = NULL;
        return R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    return r_runtime_allocator_allocate(allocator, size, alignment, result);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    return (status == 0) && borrow_failed && value_failed ? 0 : 1;
}

/* Library R-SLIB-ALLOC-0004 (M31): the native provider of std.alloc::budget_usage reads the
 * budget that the runtime installed for the calling thread. */
#include "r_runtime_budget.h"
#include "r_std_alloc_native.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int check(_Bool condition, const char *what) {
    if (!condition) {
        (void)fprintf(stderr, "library_alloc_native: %s\n", what);
        return 1U;
    }
    return 0U;
}

int main(void) {
    unsigned int failures = 0U;
    uint64_t bytes = UINT64_C(7);
    uint64_t byte_limit = UINT64_C(7);
    uint64_t tasks = UINT64_C(7);
    uint64_t task_limit = UINT64_C(7);
    uint64_t available = UINT64_C(7);
    RRuntimeBudget *outer = r_runtime_budget_create(NULL, 1, UINT64_C(100), 0, UINT64_C(0));
    RRuntimeBudget *inner = r_runtime_budget_create(outer, 0, UINT64_C(0), 1, UINT64_C(2));
    RRuntimeBudget *previous;
    int32_t status;

    if (outer == NULL || inner == NULL) {
        (void)fprintf(stderr, "library_alloc_native: budgets not created\n");
        return 1;
    }
    /* Outside every budget the provider reports none and leaves the outputs alone. */
    status = r_std_alloc_native_usage(&bytes, &byte_limit, &tasks, &task_limit, &available);
    failures += check(status == 0 && bytes == UINT64_C(7) && available == UINT64_C(7),
                      "no budget outside every budget");
    previous = r_runtime_budget_swap_current(inner);
    failures += check(r_runtime_budget_charge_task(inner), "a task under the limit");
    status = r_std_alloc_native_usage(&bytes, &byte_limit, &tasks, &task_limit, &available);
    failures += check(status == 1, "a budget inside the block");
    failures += check(bytes == UINT64_C(0) && tasks == UINT64_C(1), "the counters of the budget");
    failures += check(byte_limit == UINT64_MAX && task_limit == UINT64_C(2), "the limits of the budget");
    /* The room comes from the enclosing budget, the only one that limits bytes. */
    failures += check(available == UINT64_C(100), "the room of the enclosing budget");
    r_runtime_budget_return_task(inner);
    (void)r_runtime_budget_swap_current(previous);
    status = r_std_alloc_native_usage(&bytes, &byte_limit, &tasks, &task_limit, &available);
    failures += check(status == 0, "no budget after the block");
    r_runtime_budget_release(inner);
    r_runtime_budget_release(outer);
    if (failures != 0U) {
        return 1;
    }
    (void)printf("library_alloc_native_ok\n");
    return 0;
}

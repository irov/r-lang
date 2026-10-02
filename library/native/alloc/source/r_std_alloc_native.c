#include "r_std_alloc_native.h"

#include "r_runtime_budget.h"

#include <stddef.h>

int32_t r_std_alloc_native_usage(uint64_t *bytes,
                                 uint64_t *byte_limit,
                                 uint64_t *tasks,
                                 uint64_t *task_limit,
                                 uint64_t *bytes_available) {
    const RRuntimeBudget *budget = r_runtime_budget_current();
    if (budget == NULL) {
        return 0;
    }
    *bytes = r_runtime_budget_bytes_used(budget);
    *byte_limit = r_runtime_budget_bytes_limit(budget);
    *tasks = r_runtime_budget_tasks_used(budget);
    *task_limit = r_runtime_budget_tasks_limit(budget);
    *bytes_available = r_runtime_budget_bytes_available(budget);
    return 1;
}

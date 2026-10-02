#ifndef R_RUNTIME_BUDGET_H
#define R_RUNTIME_BUDGET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Core R-STMT-0020: a budget bounds the bytes that the allocations charged to it hold at once
 * and the tasks counted under it at once. A budget made inside another narrows it: a charge
 * goes to the budget and every ancestor, and fails when any of them would exceed its limit.
 * A budget lives while the block that made it executes, while an allocation stays charged to
 * it and while a task holds it; the references are counted.
 */
typedef struct RRuntimeBudget RRuntimeBudget;

/* A budget under parent, which it retains; a limit applies only when its flag is set. NULL
 * when the record of the budget cannot be allocated. */
RRuntimeBudget *r_runtime_budget_create(RRuntimeBudget *parent,
                                        _Bool has_bytes,
                                        uint64_t bytes,
                                        _Bool has_tasks,
                                        uint64_t tasks);
/* The budget that refuses every charge: the budget of a block whose budget could not be
 * recorded. It is never released. */
RRuntimeBudget *r_runtime_budget_refusing(void);
void r_runtime_budget_retain(RRuntimeBudget *budget);
void r_runtime_budget_release(RRuntimeBudget *budget);

/* The budget charged for the allocations of the calling thread, NULL for none; the task
 * runtime installs the budget of each task for its steps. swap returns the previous one. */
RRuntimeBudget *r_runtime_budget_current(void);
RRuntimeBudget *r_runtime_budget_swap_current(RRuntimeBudget *budget);

/* Counts one task under the budget and its ancestors; false, and a noted refusal, when one of
 * them is at its limit. return gives the task back. */
_Bool r_runtime_budget_charge_task(RRuntimeBudget *budget);
void r_runtime_budget_return_task(RRuntimeBudget *budget);

/* Whether the last refusal of an allocation or a task start on the calling thread came from a
 * budget; the allocator clears it at each attempt. note sets it. */
_Bool r_runtime_allocation_refused_by_budget(void);
void r_runtime_budget_note_refusal(void);

/* The bytes and tasks charged to a budget now, for observation and tests. */
uint64_t r_runtime_budget_bytes_used(const RRuntimeBudget *budget);
uint64_t r_runtime_budget_tasks_used(const RRuntimeBudget *budget);

/* The limits of a budget, UINT64_MAX for none (Library R-SLIB-ALLOC-0004). */
uint64_t r_runtime_budget_bytes_limit(const RRuntimeBudget *budget);
uint64_t r_runtime_budget_tasks_limit(const RRuntimeBudget *budget);

/* The bytes that a budget and every budget it narrows still admit now: the least room left
 * under their byte limits, UINT64_MAX when none of them limits bytes. */
uint64_t r_runtime_budget_bytes_available(const RRuntimeBudget *budget);

#ifdef __cplusplus
}
#endif

#endif

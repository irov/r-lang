/* Core R-STMT-0020: budgets of tasks, their byte and task limits, nesting, returns on release and
 * inheritance at task start. */

/* Counts a failed check and names its line. */
#define BUDGET_CHECK(failed)                                                                   \
    do {                                                                                       \
        if (failed) {                                                                          \
            (void)fprintf(stderr, "budget check failed at line %d\n", __LINE__);              \
            failures += 1U;                                                                    \
        }                                                                                      \
    } while (0)

typedef struct RTaskBudgetTestPayload {
    RRuntimeAllocator *allocator;
    RRuntimeBudget **seen;
    RRuntimeBudget **child_seen;
} RTaskBudgetTestPayload;

typedef struct RTaskBudgetTestResult {
    RRuntimeTask *child;
    unsigned int failures;
} RTaskBudgetTestResult;

static void budget_payload_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTaskBudgetTestPayload));
}

static void budget_result_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTaskBudgetTestResult));
}

static void *budget_test_allocate(RRuntimeAllocator *allocator, size_t size) {
    void *allocation = NULL;

    if (r_runtime_allocator_allocate(allocator, size, 1U, &allocation) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    return allocation;
}

/* The child records the budget it received at start and charges one allocation to it. */
static void budget_child_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    RTaskBudgetTestPayload *child = payload;
    void *allocation = budget_test_allocate(child->allocator, 8U);

    (void)execution;
    (void)result;
    *child->seen = r_runtime_budget_current();
    r_runtime_allocator_deallocate(allocation, 1U);
}

static unsigned int budget_test_bytes(RRuntimeAllocator *allocator) {
    unsigned int failures = 0U;
    RRuntimeBudget *outer_previous = r_runtime_task_budget_enter(1, 100U, 0, 0U);
    RRuntimeBudget *outer = r_runtime_budget_current();
    RRuntimeBudget *inner_previous;
    RRuntimeBudget *inner;
    void *first;
    void *second;
    void *third;
    void *nested;
    void *grown = NULL;
    void *outside_charge;

    BUDGET_CHECK(outer_previous != NULL || outer == NULL);
    first = budget_test_allocate(allocator, 60U);
    BUDGET_CHECK(first == NULL || r_runtime_budget_bytes_used(outer) != 60U);
    second = budget_test_allocate(allocator, 50U);
    BUDGET_CHECK(second != NULL || !r_runtime_allocation_refused_by_budget());
    second = budget_test_allocate(allocator, 40U);
    BUDGET_CHECK(second == NULL || r_runtime_allocation_refused_by_budget());
    BUDGET_CHECK(r_runtime_budget_bytes_used(outer) != 100U);
    r_runtime_allocator_deallocate(first, 1U);
    BUDGET_CHECK(r_runtime_budget_bytes_used(outer) != 40U);
    third = budget_test_allocate(allocator, 50U);
    BUDGET_CHECK(third == NULL || r_runtime_budget_bytes_used(outer) != 90U);
    /* The limits of a budget and the room left under it (Library R-SLIB-ALLOC-0004). */
    BUDGET_CHECK(r_runtime_budget_bytes_limit(outer) != 100U ||
                r_runtime_budget_tasks_limit(outer) != UINT64_MAX ||
                r_runtime_budget_bytes_available(outer) != 10U);
    BUDGET_CHECK(r_runtime_budget_bytes_limit(NULL) != UINT64_MAX ||
                r_runtime_budget_bytes_available(NULL) != UINT64_MAX);
    /* A nested budget narrows its parent and charges both. */
    inner_previous = r_runtime_task_budget_enter(1, 20U, 0, 0U);
    inner = r_runtime_budget_current();
    BUDGET_CHECK(inner_previous != outer || inner == outer);
    BUDGET_CHECK(r_runtime_budget_bytes_limit(inner) != 20U ||
                r_runtime_budget_bytes_available(inner) != 10U);
    nested = budget_test_allocate(allocator, 30U);
    BUDGET_CHECK(nested != NULL || !r_runtime_allocation_refused_by_budget());
    nested = budget_test_allocate(allocator, 10U);
    BUDGET_CHECK(nested == NULL || r_runtime_budget_bytes_used(inner) != 10U ||
                r_runtime_budget_bytes_used(outer) != 100U);
    /* The room of a nested budget is the least room of its chain. */
    BUDGET_CHECK(r_runtime_budget_bytes_available(inner) != 0U);
    /* The parent at its limit refuses although the nested budget has room. */
    grown = budget_test_allocate(allocator, 1U);
    BUDGET_CHECK(grown != NULL || !r_runtime_allocation_refused_by_budget());
    r_runtime_budget_retain(inner);
    r_runtime_task_budget_leave(inner_previous);
    BUDGET_CHECK(r_runtime_budget_current() != outer);
    /* An allocation keeps its budget after the block and returns its bytes when released. */
    r_runtime_allocator_deallocate(nested, 1U);
    BUDGET_CHECK(r_runtime_budget_bytes_used(inner) != 0U || r_runtime_budget_bytes_used(outer) != 90U);
    r_runtime_budget_release(inner);
    /* A reallocation pays its growth to the budget of the allocation. */
    r_runtime_allocator_deallocate(third, 1U);
    third = budget_test_allocate(allocator, 10U);
    BUDGET_CHECK(third == NULL ||
                r_runtime_allocator_reallocate(allocator, third, 10U, 60U, 1U, &grown) !=
                    R_RUNTIME_ALLOCATION_OK ||
                r_runtime_budget_bytes_used(outer) != 100U);
    BUDGET_CHECK(r_runtime_allocator_reallocate(allocator, grown, 60U, 61U, 1U, &third) !=
                    R_RUNTIME_ALLOCATION_EXHAUSTED ||
                third != grown || r_runtime_budget_bytes_used(outer) != 100U);
    BUDGET_CHECK(r_runtime_allocator_reallocate(allocator, grown, 60U, 5U, 1U, &third) !=
                    R_RUNTIME_ALLOCATION_OK ||
                r_runtime_budget_bytes_used(outer) != 45U);
    r_runtime_task_budget_leave(outer_previous);
    BUDGET_CHECK(r_runtime_budget_current() != NULL);
    /* Outside the block nothing is charged; releasing charged allocations still returns them. */
    outside_charge = budget_test_allocate(allocator, 1000U);
    BUDGET_CHECK(outside_charge == NULL);
    r_runtime_allocator_deallocate(outside_charge, 1U);
    r_runtime_allocator_deallocate(second, 1U);
    r_runtime_allocator_deallocate(third, 1U);
    return failures;
}

static void budget_outer_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    const RRuntimeTypeInfo child_payload_type = {
        sizeof(RTaskBudgetTestPayload), _Alignof(RTaskBudgetTestPayload), budget_payload_move,
        NULL};
    RTaskBudgetTestPayload *outer = payload;
    RTaskBudgetTestResult *report = result;
    RTaskBudgetTestPayload child_payload = {outer->allocator, outer->child_seen, NULL};
    RRuntimeTaskPrepareResult first, second;
    RRuntimeTaskStartResult started;
    RRuntimeBudget *previous;
    unsigned int failures = 0U;

    (void)execution;
    report->child = NULL;
    BUDGET_CHECK(r_runtime_budget_current() != NULL);
    BUDGET_CHECK(budget_test_bytes(outer->allocator));
    /* The task limit counts R tasks under the budget until they are destroyed. */
    previous = r_runtime_task_budget_enter(0, 0U, 1, 1U);
    first = r_runtime_task_start_prepare(child_payload_type, void_type(), budget_child_body);
    BUDGET_CHECK(first.status != R_RUNTIME_TASK_START_OK);
    second = r_runtime_task_start_prepare(child_payload_type, void_type(), budget_child_body);
    BUDGET_CHECK(second.status != R_RUNTIME_TASK_START_ALLOCATION_FAILED ||
                !r_runtime_allocation_refused_by_budget());
    r_runtime_task_start_abort(&first.transaction);
    BUDGET_CHECK(r_runtime_budget_tasks_used(r_runtime_budget_current()) != 0U);
    first = r_runtime_task_start_prepare(child_payload_type, void_type(), budget_child_body);
    if (first.status == R_RUNTIME_TASK_START_OK) {
        started = r_runtime_task_start_commit(&first.transaction, &child_payload);
        report->child = started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
    }
    BUDGET_CHECK(report->child == NULL);
    *outer->seen = r_runtime_budget_current();
    r_runtime_budget_retain(*outer->seen);
    r_runtime_task_budget_leave(previous);
    report->failures = failures;
}

/* The worker that ran the budgeted child waits here after the end of the child is published,
   before the executor releases the child, until the test has looked at the budget (M42-3). */
static pthread_mutex_t budget_end_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t budget_end_condition = PTHREAD_COND_INITIALIZER;
static _Bool budget_end_reached;
static _Bool budget_end_released;

static void budget_end_pause(void) {
    if (pthread_mutex_lock(&budget_end_mutex) != 0) {
        abort();
    }
    budget_end_reached = 1;
    (void)pthread_cond_broadcast(&budget_end_condition);
    while (!budget_end_released) {
        if (pthread_cond_wait(&budget_end_condition, &budget_end_mutex) != 0) {
            abort();
        }
    }
    (void)pthread_mutex_unlock(&budget_end_mutex);
}

static void budget_end_release(void) {
    r_runtime_darwin_task_testing_after_end(NULL);
    if (pthread_mutex_lock(&budget_end_mutex) != 0) {
        abort();
    }
    budget_end_released = 1;
    (void)pthread_cond_broadcast(&budget_end_condition);
    (void)pthread_mutex_unlock(&budget_end_mutex);
}

static _Bool test_task_budget(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {sizeof(RTaskBudgetTestPayload),
                                           _Alignof(RTaskBudgetTestPayload),
                                           budget_payload_move,
                                           NULL};
    const RRuntimeTypeInfo result_type = {sizeof(RTaskBudgetTestResult),
                                          _Alignof(RTaskBudgetTestResult),
                                          budget_result_move,
                                          NULL};
    RRuntimeBudget *budget_of_block = NULL;
    RRuntimeBudget *budget_of_child = NULL;
    RTaskBudgetTestPayload payload = {allocator, &budget_of_block, &budget_of_child};
    RTaskBudgetTestResult report = {NULL, 0U};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    /* Outside a task the budget API has no effect. */
    R_TASK_TEST_REQUIRE(r_runtime_task_budget_enter(1, 1U, 1, 1U) == NULL &&
                            r_runtime_budget_current() == NULL,
                        "a budget outside a task took effect");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "budget executor start failed");
    budget_end_reached = 0;
    budget_end_released = 0;
    r_runtime_darwin_task_testing_after_end(budget_end_pause);
    prepared = r_runtime_task_start_prepare(payload_type, result_type, budget_outer_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "budget prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "budget commit failed");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, &report) == R_RUNTIME_TASK_AWAIT_OK,
                        "budget outer await failed");
    R_TASK_TEST_REQUIRE(report.failures == 0U, "budgets charged or returned wrongly");
    R_TASK_TEST_REQUIRE(report.child != NULL, "budget child did not start");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&report.child, NULL) == R_RUNTIME_TASK_AWAIT_OK,
                        "budget child await failed");
    R_TASK_TEST_REQUIRE(budget_of_block != NULL && budget_of_child == budget_of_block,
                        "a child did not receive the budget of its starting task");
    /* A task counts until its await completes, also while the worker that ran it still holds it
       (M42-3); its bytes stay charged until its storage is released, by the awaiter or by that
       worker, which the executor stop waits for. */
    R_TASK_TEST_REQUIRE(r_runtime_budget_tasks_used(budget_of_block) == 0U,
                        "an awaited child still counted under its budget");
    budget_end_release();
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "budget executor did not stop");
    R_TASK_TEST_REQUIRE(r_runtime_budget_bytes_used(budget_of_block) == 0U,
                        "a released child kept its bytes charged");
    r_runtime_budget_release(budget_of_block);
    return 1;
}

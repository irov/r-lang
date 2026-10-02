/* Core R-STMT-0019: the deadline of a task, deadline blocks and inheritance at task start. */

typedef struct RTaskDeadlineTestObservation {
    _Bool active;
    int64_t seconds;
    uint32_t nanoseconds;
} RTaskDeadlineTestObservation;

typedef struct RTaskDeadlineTestPayload {
    RTaskDeadlineTestObservation *observation;
} RTaskDeadlineTestPayload;

typedef struct RTaskDeadlineTestOuterResult {
    RRuntimeTask *child;
    unsigned int failures;
} RTaskDeadlineTestOuterResult;

static void deadline_payload_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTaskDeadlineTestPayload));
}

static void deadline_outer_result_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTaskDeadlineTestOuterResult));
}

static RTaskDeadlineTestObservation deadline_test_observe(RTaskDeadlineTestObservation operation) {
    r_runtime_task_deadline_narrow(&operation.active, &operation.seconds, &operation.nanoseconds);
    return operation;
}

static _Bool deadline_test_equal(RTaskDeadlineTestObservation observed,
                                 _Bool active,
                                 int64_t seconds,
                                 uint32_t nanoseconds) {
    return observed.active == active &&
           (!active || (observed.seconds == seconds && observed.nanoseconds == nanoseconds));
}

/* The child records the deadline it inherited at start. */
static void deadline_child_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    RTaskDeadlineTestPayload *child = payload;

    (void)execution;
    (void)result;
    *child->observation = deadline_test_observe((RTaskDeadlineTestObservation){0, 0, 0U});
}

static void deadline_outer_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    const RRuntimeTypeInfo child_payload_type = {sizeof(RTaskDeadlineTestPayload),
                                                 _Alignof(RTaskDeadlineTestPayload),
                                                 deadline_payload_move,
                                                 NULL};
    RTaskDeadlineTestPayload *outer = payload;
    RTaskDeadlineTestOuterResult *report = result;
    const RTaskDeadlineTestObservation none = {0, 0, 0U};
    RRuntimeTaskDeadline first, second, third, previous;
    RTaskDeadlineTestPayload child_payload = {outer->observation};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    unsigned int failures = 0U;

    (void)execution;
    report->child = NULL;
    failures += !deadline_test_equal(deadline_test_observe(none), 0, 0, 0U);
    failures += !deadline_test_equal(
        deadline_test_observe((RTaskDeadlineTestObservation){1, 7, 3U}), 1, 7, 3U);
    first = r_runtime_task_deadline_enter((RRuntimeTaskDeadline){1, 100, 5U});
    failures += first.active;
    failures += !deadline_test_equal(deadline_test_observe(none), 1, 100, 5U);
    failures += !deadline_test_equal(
        deadline_test_observe((RTaskDeadlineTestObservation){1, 200, 0U}), 1, 100, 5U);
    failures += !deadline_test_equal(
        deadline_test_observe((RTaskDeadlineTestObservation){1, 100, 4U}), 1, 100, 4U);
    /* A later or absent deadline cannot extend the current one. */
    second = r_runtime_task_deadline_enter((RRuntimeTaskDeadline){1, 150, 0U});
    failures += !(second.active && second.seconds == 100 && second.nanoseconds == 5U);
    failures += !deadline_test_equal(deadline_test_observe(none), 1, 100, 5U);
    previous = r_runtime_task_deadline_enter((RRuntimeTaskDeadline){0, 0, 0U});
    failures += !deadline_test_equal(deadline_test_observe(none), 1, 100, 5U);
    r_runtime_task_deadline_leave(previous);
    third = r_runtime_task_deadline_enter((RRuntimeTaskDeadline){1, 100, 1U});
    failures += !deadline_test_equal(deadline_test_observe(none), 1, 100, 1U);
    prepared = r_runtime_task_start_prepare(child_payload_type, void_type(), deadline_child_body);
    if (prepared.status == R_RUNTIME_TASK_START_OK) {
        started = r_runtime_task_start_commit(&prepared.transaction, &child_payload);
        report->child = started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
    }
    failures += report->child == NULL;
    r_runtime_task_deadline_leave(third);
    failures += !deadline_test_equal(deadline_test_observe(none), 1, 100, 5U);
    r_runtime_task_deadline_leave(second);
    r_runtime_task_deadline_leave(first);
    failures += !deadline_test_equal(deadline_test_observe(none), 0, 0, 0U);
    report->failures = failures;
}

static _Bool test_task_deadline(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {sizeof(RTaskDeadlineTestPayload),
                                           _Alignof(RTaskDeadlineTestPayload),
                                           deadline_payload_move,
                                           NULL};
    const RRuntimeTypeInfo result_type = {sizeof(RTaskDeadlineTestOuterResult),
                                          _Alignof(RTaskDeadlineTestOuterResult),
                                          deadline_outer_result_move,
                                          NULL};
    RTaskDeadlineTestObservation observation = {0, 0, 0U};
    RTaskDeadlineTestPayload payload = {&observation};
    RTaskDeadlineTestOuterResult report = {NULL, 0U};
    RRuntimeTaskDeadline outside;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    /* Outside a task the deadline API has no effect. */
    outside = r_runtime_task_deadline_enter((RRuntimeTaskDeadline){1, 1, 0U});
    R_TASK_TEST_REQUIRE(!outside.active, "deadline outside a task was reported active");
    R_TASK_TEST_REQUIRE(
        deadline_test_equal(
            deadline_test_observe((RTaskDeadlineTestObservation){0, 0, 0U}), 0, 0, 0U),
        "deadline outside a task narrowed an operation");
    r_runtime_task_deadline_leave(outside);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "deadline executor start failed");
    prepared = r_runtime_task_start_prepare(payload_type, result_type, deadline_outer_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "deadline prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "deadline commit failed");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, &report) == R_RUNTIME_TASK_AWAIT_OK,
                        "deadline outer await failed");
    R_TASK_TEST_REQUIRE(report.failures == 0U, "deadline blocks narrowed or restored wrongly");
    R_TASK_TEST_REQUIRE(report.child != NULL, "deadline child did not start");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&report.child, NULL) == R_RUNTIME_TASK_AWAIT_OK,
                        "deadline child await failed");
    R_TASK_TEST_REQUIRE(deadline_test_equal(observation, 1, 100, 1U),
                        "a child did not inherit the deadline of its starting task");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "deadline executor did not stop");
    return 1;
}

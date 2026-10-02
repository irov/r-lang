typedef struct RTaskScopeTestContext RTaskScopeTestContext;

typedef struct RTaskScopeTestChild {
    RTaskScopeTestContext *context;
    size_t index;
    const int *borrowed;
} RTaskScopeTestChild;

struct RTaskScopeTestContext {
    RRuntimeTaskExternalExecution *execution[2];
    RTaskScopeTestChild *payload[2];
    int *result[2];
    _Atomic _Bool ready;
    _Atomic _Bool first_ready;
    _Atomic _Bool cancelled[2];
    _Atomic size_t payload_drops[2];
    _Atomic size_t parent_drops;
    _Atomic size_t steps;
    size_t first_index;
    size_t until_index;
    _Bool until_checked;
    _Bool until_expired;
    _Bool inline_completion;
};

typedef struct RTaskScopeTestFrame {
    RTaskScopeTestContext *context;
    RRuntimeTaskScope outer;
    RRuntimeTaskScopeEntry outer_entry[1];
    RRuntimeTaskScope scope;
    RRuntimeTaskScopeEntry entries[2];
    RRuntimeTask *children[2];
    int borrowed[2];
    unsigned int state;
} RTaskScopeTestFrame;

static void scope_test_complete(RTaskScopeTestContext *context, size_t index) {
    const int value = *context->payload[index]->borrowed;
    if (r_runtime_task_external_try_select_completion(context->execution[index])) {
        *context->result[index] = value;
    }
    r_runtime_task_external_acknowledge(context->execution[index]);
}

static void scope_test_child_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result) {
    RTaskScopeTestChild *payload = payload_pointer;
    RTaskScopeTestContext *context = payload->context;
    context->execution[payload->index] = execution;
    context->payload[payload->index] = payload;
    context->result[payload->index] = result;
    r_runtime_task_external_start_ready(execution);
    if (context->inline_completion) {
        scope_test_complete(context, payload->index);
    }
}

static void scope_test_child_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RTaskScopeTestChild *payload = payload_pointer;
    (void)execution;
    atomic_store_explicit(&payload->context->cancelled[payload->index], 1, memory_order_release);
}

static void scope_test_child_drop(void *payload_pointer) {
    RTaskScopeTestChild *payload = payload_pointer;
    (void)atomic_fetch_add_explicit(
        &payload->context->payload_drops[payload->index], 1U, memory_order_release);
}

static void scope_test_frame_drop(void *payload_pointer) {
    RTaskScopeTestFrame *frame = payload_pointer;
    if (frame->scope.active || frame->outer.active || frame->children[0] != NULL ||
        frame->children[1] != NULL) {
        abort();
    }
    (void)atomic_fetch_add_explicit(&frame->context->parent_drops, 1U, memory_order_release);
}

static RRuntimeTaskStepStatus
scope_test_step(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskScopeTestFrame *frame = payload_pointer;
    RTaskScopeTestContext *context = frame->context;
    RRuntimeTaskExecutionAwaitStatus status;
    (void)atomic_fetch_add_explicit(&context->steps, 1U, memory_order_relaxed);
    if (frame->state == 0U) {
        if (!r_runtime_task_scope_open(execution, &frame->outer, frame->outer_entry, 1U) ||
            !r_runtime_task_scope_open(execution, &frame->scope, frame->entries, 2U)) {
            abort();
        }
        const size_t first = r_runtime_task_scope_reserve(&frame->scope);
        const size_t second = r_runtime_task_scope_reserve(&frame->scope);
        if (first != 1U || second != 2U || r_runtime_task_scope_reserve(&frame->scope) != 0U ||
            r_runtime_task_scope_close(&frame->scope)) {
            abort();
        }
        r_runtime_task_scope_abandon(&frame->scope, first);
        if (r_runtime_task_scope_reserve(&frame->scope) != first) {
            abort();
        }
        for (size_t i = 0U; i < 2U; ++i) {
            RTaskScopeTestChild child = {context, i, &frame->borrowed[i]};
            const RRuntimeTypeInfo payload_type = {
                sizeof(child), _Alignof(RTaskScopeTestChild), NULL, scope_test_child_drop};
            const RRuntimeTypeInfo result_type = {sizeof(int), _Alignof(int), NULL, NULL};
            RRuntimeTaskPrepareResult prepared = r_runtime_task_external_start_prepare(
                payload_type, result_type, scope_test_child_start, scope_test_child_cancel);
            if (prepared.status != R_RUNTIME_TASK_START_OK) {
                abort();
            }
            RRuntimeTaskStartResult started =
                r_runtime_task_start_commit(&prepared.transaction, &child);
            if (started.status != R_RUNTIME_TASK_START_OK) {
                abort();
            }
            frame->children[i] = started.task;
            r_runtime_task_scope_bind(&frame->scope, i + 1U, started.task);
        }
        /* L25.3: an empty group has a vacancy at once; a full group whose members are not yet
           observed has none, completed or not, so a passed deadline ends the wait. */
        _Bool no_vacancy = 0;
        if (r_runtime_task_scope_wait_vacancy(execution, &frame->outer) !=
                R_RUNTIME_TASK_EXECUTION_AWAIT_OK ||
            r_runtime_task_scope_wait_vacancy_until(execution, &frame->scope, 0, 0U, &no_vacancy) !=
                R_RUNTIME_TASK_EXECUTION_AWAIT_OK ||
            !no_vacancy) {
            abort();
        }
        frame->state = 1U;
        atomic_store_explicit(&context->ready, 1, memory_order_release);
    }
    if (r_runtime_task_execution_cancel_requested(execution) && frame->state != 3U) {
        r_runtime_task_scope_cancel(&frame->scope);
        r_runtime_task_destroy(&frame->children[0]);
        r_runtime_task_destroy(&frame->children[1]);
        frame->state = 3U;
    }
    if (frame->state == 1U && !context->until_checked) {
        /* R-STMT-0018: readiness precedes a deadline that has already passed, which returns
           at once instead of suspending. */
        RRuntimeTask *selection[2] = {frame->children[1], frame->children[0]};
        _Bool expired = 0;
        status = r_runtime_task_scope_wait_until(
            execution, &frame->scope, selection, 2U, &context->until_index, 0, 0U, &expired);
        context->until_checked = 1;
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
            context->until_expired = expired;
        } else if (status != R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
            abort();
        }
    }
    if (frame->state == 1U) {
        RRuntimeTask *selection[2] = {frame->children[1], frame->children[0]};
        status = r_runtime_task_scope_wait(
            execution, &frame->scope, selection, 2U, &context->first_index);
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
            return R_RUNTIME_TASK_STEP_SUSPENDED;
        }
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
            r_runtime_task_scope_cancel(&frame->scope);
            r_runtime_task_destroy(&frame->children[0]);
            r_runtime_task_destroy(&frame->children[1]);
            frame->state = 3U;
        } else if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
            atomic_store_explicit(&context->first_ready, 1, memory_order_release);
            frame->state = 2U;
        } else {
            abort();
        }
    }
    status = r_runtime_task_scope_wait(execution, &frame->scope, NULL, 0U, NULL);
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        return R_RUNTIME_TASK_STEP_SUSPENDED;
    }
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
        r_runtime_task_scope_cancel(&frame->scope);
        r_runtime_task_destroy(&frame->children[0]);
        r_runtime_task_destroy(&frame->children[1]);
        frame->state = 3U;
        status = r_runtime_task_scope_wait(execution, &frame->scope, NULL, 0U, NULL);
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
            return R_RUNTIME_TASK_STEP_SUSPENDED;
        }
    }
    if (status != R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
        abort();
    }
    if (frame->state == 2U) {
        int sum = 0;
        for (size_t i = 0U; i < 2U; ++i) {
            int value = 0;
            status = r_runtime_task_execution_await(execution, &frame->children[i], &value);
            if (status != R_RUNTIME_TASK_EXECUTION_AWAIT_OK &&
                status != R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
                abort();
            }
            sum += value;
        }
        *(int *)result_pointer = sum;
        /* Observed members free their slots. */
        if (r_runtime_task_scope_wait_vacancy(execution, &frame->scope) !=
            R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
            abort();
        }
        const size_t reused = r_runtime_task_scope_reserve(&frame->scope);
        if (reused == 0U) {
            abort();
        }
        r_runtime_task_scope_abandon(&frame->scope, reused);
    }
    if (!r_runtime_task_scope_close(&frame->scope) ||
        r_runtime_task_scope_wait(execution, &frame->outer, NULL, 0U, NULL) !=
            (r_runtime_task_execution_cancel_requested(execution)
                 ? R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED
                 : R_RUNTIME_TASK_EXECUTION_AWAIT_OK) ||
        !r_runtime_task_scope_close(&frame->outer)) {
        abort();
    }
    return frame->state == 3U ? R_RUNTIME_TASK_STEP_CANCELLED : R_RUNTIME_TASK_STEP_COMPLETED;
}

static _Bool test_task_scope(RRuntimeAllocator *allocator, _Bool inline_completion, _Bool cancel) {
    RTaskScopeTestContext context = {0};
    RTaskScopeTestFrame frame = {0};
    const RRuntimeTypeInfo frame_type = {
        sizeof(frame), _Alignof(RTaskScopeTestFrame), NULL, scope_test_frame_drop};
    const RRuntimeTypeInfo result_type = {sizeof(int), _Alignof(int), NULL, NULL};
    context.inline_completion = inline_completion;
    frame.context = &context;
    frame.borrowed[0] = 19;
    frame.borrowed[1] = 23;
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "scope executor failed to start");
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_resumable_start_prepare(frame_type, result_type, scope_test_step);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "scope parent prepare failed");
    RRuntimeTaskStartResult started = r_runtime_task_start_commit(&prepared.transaction, &frame);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "scope parent commit failed");
    while (!atomic_load_explicit(&context.ready, memory_order_acquire)) {
        (void)sched_yield();
    }
    if (!inline_completion) {
        while (r_runtime_task_state(started.task) != R_RUNTIME_TASK_SUSPENDED) {
            (void)sched_yield();
        }
    }
    if (cancel) {
        RRuntimeTask *parent = started.task;
        r_runtime_task_cancel(&started.task);
        for (size_t i = 0U; i < 2U; ++i) {
            while (!atomic_load_explicit(&context.cancelled[i], memory_order_acquire)) {
                (void)sched_yield();
            }
        }
        /* Supervision retains this borrowed parent until both acknowledgements below. */
        while (r_runtime_task_state(parent) != R_RUNTIME_TASK_SUSPENDED) {
            (void)sched_yield();
        }
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.parent_drops, memory_order_acquire) == 0U,
                            "scope cancellation released borrowed frame before acknowledgement");
        scope_test_complete(&context, 0U);
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.parent_drops, memory_order_acquire) == 0U,
                            "scope cancellation ignored the second outstanding child");
        scope_test_complete(&context, 1U);
    } else if (!inline_completion) {
        scope_test_complete(&context, 0U);
        while (!atomic_load_explicit(&context.first_ready, memory_order_acquire)) {
            (void)sched_yield();
        }
        R_TASK_TEST_REQUIRE(context.first_index == 1U, "scope first ignored selection order");
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.parent_drops, memory_order_acquire) == 0U,
                            "scope all did not wait for the remaining child");
        scope_test_complete(&context, 1U);
    }
    if (!cancel) {
        int result = 0;
        R_TASK_TEST_REQUIRE(
            r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK && result == 42,
            "scope result transfer failed");
        R_TASK_TEST_REQUIRE(!inline_completion || context.first_index == 0U,
                            "scope simultaneous completion did not select the first argument");
        R_TASK_TEST_REQUIRE(context.until_checked && context.until_expired == !inline_completion &&
                                (!inline_completion || context.until_index == 0U),
                            "scope bounded wait mixed readiness with a passed deadline");
    }
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "scope executor failed to drain");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.parent_drops, memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.payload_drops[0], memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.payload_drops[1], memory_order_relaxed) == 1U,
        "scope payloads were not destroyed exactly once");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.steps, memory_order_relaxed) < 32U,
                        "scope cancellation drain spun instead of suspending");
    return 1;
}

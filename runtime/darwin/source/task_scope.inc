/* Scope links are protected by the executor lock, always acquired before a task lock. */

static RRuntimeTask *task_scope_disarm_locked(RRuntimeTaskScope *scope, _Bool *dispatch_resume) {
    RRuntimeTask *waiter = scope->waiter;
    *dispatch_resume = 0;
    if (waiter == NULL) {
        return NULL;
    }
    scope->waiter = NULL;
    task_lock(waiter);
    if (waiter->waiting_scope != scope) {
        abort();
    }
    waiter->waiting_scope = NULL;
    *dispatch_resume = task_request_resume_locked(waiter);
    task_unlock(waiter);
    return waiter;
}

static void task_scope_publish(RRuntimeTask *task) {
    RRuntimeTask *waiter = NULL;
    _Bool dispatch_resume = 0;
    _Bool release_member = 0;

    executor_lock();
    /* Load the frame-owned scope only while close is excluded by the same lock. */
    if (task->scope != NULL &&
        atomic_load_explicit(&task->terminal_published, memory_order_acquire)) {
        RRuntimeTaskScope *scope = task->scope;
        task_lock(task);
        if (!task->observer_attached && !task->observer_cleanup_active) {
            if (!scope->active || scope->entries[task->scope_index].task != task) {
                abort();
            }
            scope->entries[task->scope_index].task = NULL;
            task->scope = NULL;
            release_member = 1;
        }
        task_unlock(task);
        waiter = task_scope_disarm_locked(scope, &dispatch_resume);
    }
    executor_unlock();
    if (waiter != NULL) {
        if (dispatch_resume) {
            task_dispatch(waiter);
        }
        task_release_reference(waiter);
    }
    if (release_member) {
        task_release_reference(task);
    }
}

_Bool r_runtime_task_scope_open(RRuntimeTaskExecution *execution,
                                RRuntimeTaskScope *scope,
                                RRuntimeTaskScopeEntry *entries,
                                size_t capacity) {
    RRuntimeTask *owner;
    if (execution == NULL || execution->task == NULL || scope == NULL || entries == NULL ||
        capacity == 0U || capacity > SIZE_MAX / sizeof(*entries)) {
        return 0;
    }
    owner = execution->task;
    executor_lock();
    task_lock(owner);
    if (owner->kind != R_RUNTIME_TASK_KIND_RESUMABLE || owner->state != R_RUNTIME_TASK_RUNNING ||
        owner->terminal_decided || owner->references == SIZE_MAX ||
        owner->open_scope_count == SIZE_MAX) {
        task_unlock(owner);
        executor_unlock();
        return 0;
    }
    owner->references += 1U;
    owner->open_scope_count += 1U;
    task_unlock(owner);
    (void)memset(entries, 0, capacity * sizeof(*entries));
    *scope = (RRuntimeTaskScope){entries, capacity, owner, NULL, 1, 0};
    executor_unlock();
    return 1;
}

size_t r_runtime_task_scope_reserve(RRuntimeTaskScope *scope) {
    size_t reservation = 0U;
    executor_lock();
    if (scope != NULL && scope->active && !scope->closing) {
        for (size_t i = 0U; i < scope->capacity; ++i) {
            if (scope->entries[i].task == NULL && !scope->entries[i].reserved) {
                scope->entries[i].reserved = 1;
                reservation = i + 1U;
                break;
            }
        }
    }
    executor_unlock();
    return reservation;
}

void r_runtime_task_scope_abandon(RRuntimeTaskScope *scope, size_t reservation) {
    executor_lock();
    if (scope == NULL || !scope->active || reservation == 0U || reservation > scope->capacity ||
        !scope->entries[reservation - 1U].reserved ||
        scope->entries[reservation - 1U].task != NULL) {
        abort();
    }
    scope->entries[reservation - 1U].reserved = 0;
    executor_unlock();
}

void r_runtime_task_scope_bind(RRuntimeTaskScope *scope, size_t reservation, RRuntimeTask *task) {
    executor_lock();
    if (scope == NULL || !scope->active || scope->closing || task == NULL || task == scope->owner ||
        reservation == 0U || reservation > scope->capacity ||
        !scope->entries[reservation - 1U].reserved ||
        scope->entries[reservation - 1U].task != NULL) {
        abort();
    }
    task_lock(task);
    if (!atomic_load_explicit(&task->committed, memory_order_acquire) || task->scope != NULL ||
        !task->observer_attached || task->references == SIZE_MAX) {
        abort();
    }
    task->references += 1U;
    task->scope = scope;
    task->scope_index = reservation - 1U;
    scope->entries[reservation - 1U].task = task;
    scope->entries[reservation - 1U].reserved = 0;
    task_unlock(task);
    executor_unlock();
}

/* The monotonic clock of std.time::instant: continuous Mach time in nanoseconds. */
static uint64_t task_scope_monotonic_now(void) {
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0U && mach_timebase_info(&timebase) != KERN_SUCCESS) {
        abort();
    }
    const unsigned __int128 nanoseconds =
        (unsigned __int128)mach_continuous_time() * timebase.numer / timebase.denom;
    return nanoseconds > UINT64_MAX ? UINT64_MAX : (uint64_t)nanoseconds;
}

/* A one-shot wakeup armed by a deadline-bounded wait. It resumes the owner only while that
 * owner still waits on a scope with a reached deadline, so a stale wakeup has no effect. */
static void task_scope_deadline_fired(void *context) {
    RRuntimeTask *owner = context;
    RRuntimeTask *waiter = NULL;
    _Bool dispatch_resume = 0;
    executor_lock();
    task_lock(owner);
    RRuntimeTaskScope *scope = owner->waiting_scope;
    const _Bool due = scope != NULL && scope->waiter == owner && owner->scope_deadline_active &&
                      task_scope_monotonic_now() >= owner->scope_deadline;
    task_unlock(owner);
    if (due) {
        waiter = task_scope_disarm_locked(scope, &dispatch_resume);
    }
    executor_unlock();
    if (waiter != NULL) {
        if (dispatch_resume) {
            task_dispatch(waiter);
        }
        task_release_reference(waiter);
    }
    task_release_reference(owner);
}

/* Waits for all members (empty selection), for the first terminal member of a selection, or,
 * with vacancy, until a slot of the group is free (L25). */
static RRuntimeTaskExecutionAwaitStatus task_scope_wait(RRuntimeTaskExecution *execution,
                                                        RRuntimeTaskScope *scope,
                                                        RRuntimeTask *const *selection,
                                                        size_t count,
                                                        size_t *ready_index,
                                                        _Bool vacancy,
                                                        const uint64_t *deadline,
                                                        _Bool *expired) {
    RRuntimeTask *owner;
    RRuntimeTask *previous_waiter = NULL;
    RRuntimeTaskExecutionAwaitStatus status = R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    _Bool ready = count == 0U && !vacancy;
    _Bool armed = 0;
    size_t selected = 0U;
    uint64_t remaining = 0U;
    if (execution == NULL || execution->task == NULL || scope == NULL ||
        (count != 0U && (vacancy || selection == NULL || ready_index == NULL)) ||
        (deadline != NULL && expired == NULL)) {
        return status;
    }
    if (expired != NULL) {
        *expired = 0;
    }
    owner = execution->task;
    /* M26-6: a task cancelled while it awaited another task can reach this wait still registered
       as the waiter of that task. When the awaited task has published its end, its wakeup is on
       its way: the wait suspends until it arrives. When the awaited task is a running member of
       this scope, the barrier, which waits for every member, takes the wait over. */
    {
        RRuntimeTask *awaited;
        _Bool released = 0;
        _Bool in_flight = 0;
        task_lock(owner);
        awaited = owner->waiting_on;
        task_unlock(owner);
        if (awaited != NULL) {
            task_lock(awaited);
            if (awaited->waiter == owner) {
                if (count == 0U && !vacancy && awaited->scope == scope) {
                    awaited->waiter = NULL;
                    released = 1;
                }
            } else if (atomic_load_explicit(&awaited->terminal_published, memory_order_acquire)) {
                in_flight = 1;
            }
            task_unlock(awaited);
            if (in_flight) {
                return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
            }
            if (released) {
                task_lock(owner);
                if (owner->waiting_on == awaited) {
                    owner->waiting_on = NULL;
                }
                task_unlock(owner);
                task_release_reference(owner);
            }
        }
    }
    executor_lock();
    task_lock(owner);
    if (!scope->active || scope->owner != owner || owner->kind != R_RUNTIME_TASK_KIND_RESUMABLE ||
        owner->state != R_RUNTIME_TASK_RUNNING || owner->terminal_decided ||
        owner->waiting_on != NULL || (scope->closing && count != 0U)) {
        task_unlock(owner);
        executor_unlock();
        return status;
    }
    if (owner->waiting_scope != NULL) {
        if (owner->waiting_scope->waiter != owner) {
            abort();
        }
        owner->waiting_scope->waiter = NULL;
        owner->waiting_scope = NULL;
        previous_waiter = owner;
    }
    task_unlock(owner);

    if (!scope->closing && atomic_load_explicit(&owner->cancel_requested, memory_order_acquire)) {
        status = R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED;
        goto done;
    }
    for (size_t i = 0U; i < scope->capacity; ++i) {
        if (scope->entries[i].reserved) {
            goto done;
        }
        if (vacancy) {
            /* A slot is free once no member occupies it; a detached member releases its slot
               when its publication removes the scope link. */
            if (scope->entries[i].task == NULL) {
                ready = 1;
            }
            continue;
        }
        if (count == 0U && scope->entries[i].task != NULL) {
            RRuntimeTask *task = scope->entries[i].task;
            task_lock(task);
            /* A detached observation is ready only after publication removes its scope link. */
            if (!task->observer_attached ||
                !atomic_load_explicit(&task->terminal_published, memory_order_acquire)) {
                ready = 0;
            }
            task_unlock(task);
        }
    }
    for (size_t i = 0U; i < count; ++i) {
        RRuntimeTask *task = selection[i];
        if (task == NULL || task->scope != scope) {
            goto done;
        }
        task_lock(task);
        const _Bool observed = task->observer_attached;
        task_unlock(task);
        if (!observed) {
            goto done;
        }
        if (!ready && atomic_load_explicit(&task->terminal_published, memory_order_acquire)) {
            selected = i;
            ready = 1;
        }
    }
    if (ready) {
        if (count != 0U) {
            *ready_index = selected;
        }
        status = R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
        goto done;
    }
    if (deadline != NULL) {
        const uint64_t now = task_scope_monotonic_now();
        if (now >= *deadline) {
            *expired = 1;
            status = R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
            goto done;
        }
        remaining = *deadline - now;
    }
    task_lock(owner);
    if (owner->references > SIZE_MAX - 2U || scope->waiter != NULL) {
        abort();
    }
    owner->references += 1U;
    owner->waiting_scope = scope;
    owner->scope_deadline_active = deadline != NULL;
    owner->scope_deadline = deadline != NULL ? *deadline : 0U;
    if (deadline != NULL) {
        /* The wakeup retains the owner until it has run. */
        owner->references += 1U;
        armed = 1;
    }
    scope->waiter = owner;
    task_unlock(owner);
    status = R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
done:
    executor_unlock();
    if (armed) {
        dispatch_after_f(
            dispatch_time(DISPATCH_TIME_NOW,
                          remaining > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)remaining),
            dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
            owner,
            task_scope_deadline_fired);
    }
    if (previous_waiter != NULL) {
        task_release_reference(previous_waiter);
    }
    return status;
}

RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait(RRuntimeTaskExecution *execution,
                                                           RRuntimeTaskScope *scope,
                                                           RRuntimeTask *const *selection,
                                                           size_t count,
                                                           size_t *ready_index) {
    return task_scope_wait(execution, scope, selection, count, ready_index, 0, NULL, NULL);
}

/* The deadline of a bounded wait: the earlier of the given instant and the deadline of the
   waiting task (Core R-STMT-0018/0019), in nanoseconds of the monotonic clock. An instant before
   the clock's origin has passed; one beyond its range never arrives. */
static uint64_t task_scope_bounded_deadline(const RRuntimeTaskExecution *execution,
                                            int64_t deadline_seconds,
                                            uint32_t deadline_nanoseconds) {
    uint64_t deadline = 0U;
    if (execution != NULL && execution->task != NULL && execution->task->deadline.active) {
        const RRuntimeTaskDeadline own = execution->task->deadline;
        if (own.seconds < deadline_seconds ||
            (own.seconds == deadline_seconds && own.nanoseconds < deadline_nanoseconds)) {
            deadline_seconds = own.seconds;
            deadline_nanoseconds = own.nanoseconds;
        }
    }
    if (deadline_seconds > 0) {
        const unsigned __int128 total =
            (unsigned __int128)(uint64_t)deadline_seconds * UINT64_C(1000000000) +
            deadline_nanoseconds;
        deadline = total > UINT64_MAX ? UINT64_MAX : (uint64_t)total;
    } else if (deadline_seconds == 0) {
        deadline = deadline_nanoseconds;
    }
    return deadline;
}

RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait_until(RRuntimeTaskExecution *execution,
                                                                 RRuntimeTaskScope *scope,
                                                                 RRuntimeTask *const *selection,
                                                                 size_t count,
                                                                 size_t *ready_index,
                                                                 int64_t deadline_seconds,
                                                                 uint32_t deadline_nanoseconds,
                                                                 _Bool *expired) {
    const uint64_t deadline =
        task_scope_bounded_deadline(execution, deadline_seconds, deadline_nanoseconds);
    return task_scope_wait(execution, scope, selection, count, ready_index, 0, &deadline, expired);
}

RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait_vacancy(RRuntimeTaskExecution *execution,
                                                                   RRuntimeTaskScope *scope) {
    return task_scope_wait(execution, scope, NULL, 0U, NULL, 1, NULL, NULL);
}

RRuntimeTaskExecutionAwaitStatus
r_runtime_task_scope_wait_vacancy_until(RRuntimeTaskExecution *execution,
                                        RRuntimeTaskScope *scope,
                                        int64_t deadline_seconds,
                                        uint32_t deadline_nanoseconds,
                                        _Bool *expired) {
    const uint64_t deadline =
        task_scope_bounded_deadline(execution, deadline_seconds, deadline_nanoseconds);
    return task_scope_wait(execution, scope, NULL, 0U, NULL, 1, &deadline, expired);
}

void r_runtime_task_scope_cancel(RRuntimeTaskScope *scope) {
    executor_lock();
    if (scope == NULL || !scope->active) {
        abort();
    }
    scope->closing = 1;
    for (size_t i = 0U; i < scope->capacity; ++i) {
        if (scope->entries[i].task != NULL) {
            request_cancellation(scope->entries[i].task);
        }
    }
    executor_unlock();
}

_Bool r_runtime_task_scope_close(RRuntimeTaskScope *scope) {
    RRuntimeTask *owner;
    executor_lock();
    if (scope == NULL || !scope->active || scope->waiter != NULL) {
        executor_unlock();
        return 0;
    }
    for (size_t i = 0U; i < scope->capacity; ++i) {
        if (scope->entries[i].task != NULL || scope->entries[i].reserved) {
            executor_unlock();
            return 0;
        }
    }
    owner = scope->owner;
    task_lock(owner);
    if (owner->open_scope_count == 0U) {
        abort();
    }
    owner->open_scope_count -= 1U;
    task_unlock(owner);
    scope->active = 0;
    scope->owner = NULL;
    executor_unlock();
    task_release_reference(owner);
    return 1;
}

_Bool r_runtime_task_scope_drop(RRuntimeTaskScope *scope, RRuntimeTask **slot) {
    if (scope == NULL || slot == NULL || *slot == NULL)
        return 0;
    executor_lock();
    const _Bool member = scope->active && (*slot)->scope == scope;
    executor_unlock();
    if (!member)
        return 0;
    /* The observation keeps the member alive after releasing the executor lock. */
    r_runtime_task_destroy(slot);
    return 1;
}

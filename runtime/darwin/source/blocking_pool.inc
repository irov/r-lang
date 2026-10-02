/*
 * Blocking call pool (Core R-TERM-0015, Library R-SLIB-ASYNC-0017). The jobs form one
 * doubly-linked FIFO, so withdrawing a waiting job costs no search. A job holds no pool state
 * after run returns: its producer acknowledges its task from run, and a cancellation that arrives
 * later finds the job taken and leaves it alone.
 */

enum {
    R_RUNTIME_BLOCKING_JOB_QUEUED = 1U,
    R_RUNTIME_BLOCKING_JOB_TAKEN = 2U
};

static pthread_mutex_t r_runtime_blocking_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t r_runtime_blocking_condition = PTHREAD_COND_INITIALIZER;

static struct {
    pthread_t workers[R_RUNTIME_BLOCKING_THREAD_COUNT];
    unsigned int created;
    unsigned int waiting;
    RRuntimeBlockingJob *head;
    RRuntimeBlockingJob *tail;
    size_t queued;
    size_t reserved;
    size_t capacity;
    _Bool accepting;
} r_runtime_blocking;

static void blocking_lock(void) {
    if (pthread_mutex_lock(&r_runtime_blocking_mutex) != 0) {
        abort();
    }
}

static void blocking_unlock(void) {
    if (pthread_mutex_unlock(&r_runtime_blocking_mutex) != 0) {
        abort();
    }
}

static void blocking_unlink_locked(RRuntimeBlockingJob *job) {
    if (job->previous == NULL) {
        r_runtime_blocking.head = job->next;
    } else {
        job->previous->next = job->next;
    }
    if (job->next == NULL) {
        r_runtime_blocking.tail = job->previous;
    } else {
        job->next->previous = job->previous;
    }
    job->previous = NULL;
    job->next = NULL;
    r_runtime_blocking.queued -= 1U;
}

/* A pool thread runs R entries, so it establishes its stack bounds once and cleans its
   thread-local state after every job. */
static void *blocking_worker(void *context) {
    (void)context;
    r_runtime_blocking_worker_thread = 1;
    (void)pthread_setname_np("r.blocking");
    if (!r_runtime_stack_initialize_current_thread()) {
        r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    blocking_lock();
    for (;;) {
        RRuntimeBlockingJob *job;

        while (r_runtime_blocking.head == NULL && r_runtime_blocking.accepting) {
            r_runtime_blocking.waiting += 1U;
            if (pthread_cond_wait(&r_runtime_blocking_condition, &r_runtime_blocking_mutex) != 0) {
                abort();
            }
            r_runtime_blocking.waiting -= 1U;
        }
        job = r_runtime_blocking.head;
        if (job == NULL) {
            break;
        }
        blocking_unlink_locked(job);
        job->state = R_RUNTIME_BLOCKING_JOB_TAKEN;
        blocking_unlock();
        r_runtime_stack_require(R_RUNTIME_GENERATED_FRAME_MAX_BYTES,
                                (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
        r_runtime_blocking_running = job->execution;
        job->run(job);
        r_runtime_blocking_running = NULL;
        r_runtime_thread_local_cleanup_current();
        blocking_lock();
    }
    blocking_unlock();
    return NULL;
}

_Bool r_runtime_blocking_start(size_t capacity) {
    _Bool started = 0;

    blocking_lock();
    if (!r_runtime_blocking.accepting && r_runtime_blocking.created == 0U && capacity != 0U) {
        r_runtime_blocking.capacity = capacity;
        r_runtime_blocking.queued = 0U;
        r_runtime_blocking.reserved = 0U;
        r_runtime_blocking.accepting = 1;
        started = 1;
    }
    blocking_unlock();
    return started;
}

RRuntimeBlockingReserveStatus r_runtime_blocking_reserve(void) {
    RRuntimeBlockingReserveStatus status = R_RUNTIME_BLOCKING_RESERVED;

    blocking_lock();
    if (!r_runtime_blocking.accepting) {
        status = R_RUNTIME_BLOCKING_STOPPING;
    } else if (r_runtime_blocking.queued + r_runtime_blocking.reserved >=
               r_runtime_blocking.capacity) {
        status = R_RUNTIME_BLOCKING_QUEUE_FULL;
    } else {
        r_runtime_blocking.reserved += 1U;
        /* Every waiting or reserved job without an idle thread may add one more thread. */
        if (r_runtime_blocking.created < R_RUNTIME_BLOCKING_THREAD_COUNT &&
            r_runtime_blocking.queued + r_runtime_blocking.reserved > r_runtime_blocking.waiting) {
            if (pthread_create(&r_runtime_blocking.workers[r_runtime_blocking.created],
                               NULL,
                               blocking_worker,
                               NULL) == 0) {
                r_runtime_blocking.created += 1U;
            } else if (r_runtime_blocking.created == 0U) {
                r_runtime_blocking.reserved -= 1U;
                status = R_RUNTIME_BLOCKING_THREAD_UNAVAILABLE;
            }
        }
    }
    blocking_unlock();
    return status;
}

void r_runtime_blocking_unreserve(void) {
    blocking_lock();
    if (r_runtime_blocking.reserved == 0U) {
        blocking_unlock();
        abort();
    }
    r_runtime_blocking.reserved -= 1U;
    blocking_unlock();
}

void r_runtime_blocking_submit(RRuntimeBlockingJob *job,
                               RRuntimeBlockingRunFn run,
                               const RRuntimeTaskExternalExecution *execution) {
    blocking_lock();
    if (r_runtime_blocking.reserved == 0U || !r_runtime_blocking.accepting) {
        blocking_unlock();
        abort();
    }
    r_runtime_blocking.reserved -= 1U;
    r_runtime_blocking.queued += 1U;
    job->run = run;
    job->execution = execution;
    job->state = R_RUNTIME_BLOCKING_JOB_QUEUED;
    job->next = NULL;
    job->previous = r_runtime_blocking.tail;
    if (r_runtime_blocking.tail == NULL) {
        r_runtime_blocking.head = job;
    } else {
        r_runtime_blocking.tail->next = job;
    }
    r_runtime_blocking.tail = job;
    if (pthread_cond_signal(&r_runtime_blocking_condition) != 0) {
        blocking_unlock();
        abort();
    }
    blocking_unlock();
}

_Bool r_runtime_blocking_withdraw(RRuntimeBlockingJob *job) {
    _Bool withdrawn = 0;

    blocking_lock();
    if (job->state == R_RUNTIME_BLOCKING_JOB_QUEUED) {
        blocking_unlink_locked(job);
        job->state = 0U;
        withdrawn = 1;
    }
    blocking_unlock();
    return withdrawn;
}

void r_runtime_blocking_stop(void) {
    unsigned int created;

    blocking_lock();
    r_runtime_blocking.accepting = 0;
    created = r_runtime_blocking_worker_thread ? 0U : r_runtime_blocking.created;
    if (pthread_cond_broadcast(&r_runtime_blocking_condition) != 0) {
        blocking_unlock();
        abort();
    }
    blocking_unlock();
    for (unsigned int index = 0U; index < created; ++index) {
        if (pthread_join(r_runtime_blocking.workers[index], NULL) != 0) {
            abort();
        }
    }
    if (created != 0U) {
        blocking_lock();
        r_runtime_blocking.created = 0U;
        blocking_unlock();
    }
}

#include "r_runtime_darwin_signal.h"

#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

/* One registration of the Dispatch source of a signal; freed by the cancel handler of its source,
   after which no event handler of that source runs. */
typedef struct RRuntimeDarwinSignalRegistration {
    RRuntimeDarwinSignalKind kind;
    dispatch_source_t source;
    dispatch_semaphore_t registered;
} RRuntimeDarwinSignalRegistration;

typedef struct RRuntimeDarwinSignalEntry {
    int number;
    RRuntimeDarwinSignalRegistration *registration;
    struct sigaction kept;
    RRuntimeDarwinSignalListener *first;
    size_t listeners;
} RRuntimeDarwinSignalEntry;

struct RRuntimeDarwinSignalListener {
    RRuntimeDarwinSignalKind kind;
    size_t references;
    uint64_t pending;
    RRuntimeDarwinSignalListener *previous;
    RRuntimeDarwinSignalListener *next;
    RRuntimeDarwinSignalWait *head;
    RRuntimeDarwinSignalWait *tail;
};

struct RRuntimeDarwinSignalWait {
    RRuntimeDarwinSignalListener *listener;
    dispatch_source_t timer_source;
    RRuntimeDarwinSignalWait *next;
    RRuntimeDarwinSignalWaitCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinSignalWaitResult result;
    _Bool queued;
    _Bool timer_activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
    _Bool count_returned;
};

/* The bound of the wait for the kernel registration of a new signal source. */
#define R_RUNTIME_DARWIN_SIGNAL_REGISTRATION_NANOSECONDS UINT64_C(5000000000)

/* Every field of the entries, listeners and waits is guarded by this mutex. */
static pthread_mutex_t signal_mutex = PTHREAD_MUTEX_INITIALIZER;
static RRuntimeDarwinSignalEntry signal_entries[R_RUNTIME_DARWIN_SIGNAL_KIND_COUNT] = {
    {SIGTERM, NULL, {0}, NULL, 0U},
    {SIGINT, NULL, {0}, NULL, 0U},
    {SIGHUP, NULL, {0}, NULL, 0U},
    {SIGUSR1, NULL, {0}, NULL, 0U},
    {SIGUSR2, NULL, {0}, NULL, 0U},
};

static void signal_lock(void) {
    if (pthread_mutex_lock(&signal_mutex) != 0) {
        abort();
    }
}

static void signal_unlock(void) {
    if (pthread_mutex_unlock(&signal_mutex) != 0) {
        abort();
    }
}

static _Bool signal_kind_valid(RRuntimeDarwinSignalKind kind) {
    return (unsigned)kind < (unsigned)R_RUNTIME_DARWIN_SIGNAL_KIND_COUNT;
}

static uint64_t signal_saturating_add(uint64_t left, uint64_t right) {
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

int r_runtime_darwin_signal_number(RRuntimeDarwinSignalKind kind) {
    if (!signal_kind_valid(kind)) {
        abort();
    }
    return signal_entries[kind].number;
}

static void signal_wait_activate_timer_locked(RRuntimeDarwinSignalWait *wait) {
    if (wait->timer_source != NULL && !wait->timer_activated) {
        wait->timer_activated = 1;
        dispatch_activate(wait->timer_source);
    }
}

static void signal_dequeue_locked(RRuntimeDarwinSignalWait *wait) {
    RRuntimeDarwinSignalListener *listener = wait->listener;
    RRuntimeDarwinSignalWait *previous = NULL;
    RRuntimeDarwinSignalWait *current = listener->head;

    if (!wait->queued) {
        return;
    }
    while (current != NULL && current != wait) {
        previous = current;
        current = current->next;
    }
    if (current == NULL) {
        abort();
    }
    if (previous == NULL) {
        listener->head = wait->next;
    } else {
        previous->next = wait->next;
    }
    if (listener->tail == wait) {
        listener->tail = previous;
    }
    wait->next = NULL;
    wait->queued = 0;
}

static void signal_return_count_locked(RRuntimeDarwinSignalWait *wait) {
    if (wait->result.terminal_event != R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED ||
        wait->count_returned) {
        return;
    }
    wait->count_returned = 1;
    wait->listener->pending = signal_saturating_add(wait->listener->pending, wait->result.count);
}

/* Selects the terminal event of one wait unless an earlier one is selected; a selected delivery
   that an earlier event replaces returns its count. */
static _Bool signal_wait_select_locked(RRuntimeDarwinSignalWait *wait,
                                       RRuntimeDarwinSignalTerminalEvent event,
                                       uint64_t event_sequence,
                                       uint64_t count) {
    if (event_sequence == UINT64_C(0)) {
        abort();
    }
    if (wait->completion_delivered) {
        return 0;
    }
    if (wait->terminal_selected && event_sequence >= wait->result.terminal_event_sequence) {
        return 0;
    }
    if (wait->terminal_selected) {
        signal_return_count_locked(wait);
    }
    signal_dequeue_locked(wait);
    wait->terminal_selected = 1;
    wait->count_returned = 0;
    wait->result.terminal_event = event;
    wait->result.terminal_event_sequence = event_sequence;
    wait->result.count = count;
    signal_wait_activate_timer_locked(wait);
    if (wait->timer_source != NULL) {
        dispatch_source_cancel(wait->timer_source);
    }
    return 1;
}

/* Hands the counted deliveries of a listener to its oldest queued wait. */
static void signal_serve_locked(RRuntimeDarwinSignalListener *listener, uint64_t event_sequence) {
    while (listener->pending != UINT64_C(0) && listener->head != NULL) {
        RRuntimeDarwinSignalWait *wait = listener->head;

        if (signal_wait_select_locked(wait,
                                      R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED,
                                      event_sequence,
                                      listener->pending)) {
            listener->pending = UINT64_C(0);
        } else {
            signal_dequeue_locked(wait);
        }
    }
}

static void signal_event(void *context) {
    RRuntimeDarwinSignalRegistration *registration = context;
    const uint64_t delivered = (uint64_t)dispatch_source_get_data(registration->source);
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();
    RRuntimeDarwinSignalEntry *entry;
    RRuntimeDarwinSignalListener *listener;

    signal_lock();
    entry = &signal_entries[registration->kind];
    if (entry->registration == registration) {
        for (listener = entry->first; listener != NULL; listener = listener->next) {
            listener->pending =
                signal_saturating_add(listener->pending, delivered == 0U ? 1U : delivered);
            signal_serve_locked(listener, event_sequence);
        }
    }
    signal_unlock();
}

static void signal_registered(void *context) {
    RRuntimeDarwinSignalRegistration *registration = context;

    (void)dispatch_semaphore_signal(registration->registered);
}

/* The registering thread clears `registered` under the signal mutex; the cancel handler reads it
   under the same mutex, which orders the two for the thread sanitizer as well (M22-4). */
static void signal_registration_cancelled(void *context) {
    RRuntimeDarwinSignalRegistration *registration = context;
    dispatch_semaphore_t registered;

    signal_lock();
    registered = registration->registered;
    registration->registered = NULL;
    signal_unlock();
    if (registered != NULL) {
        dispatch_release(registered);
    }
    dispatch_release(registration->source);
    r_runtime_allocator_deallocate(registration, _Alignof(RRuntimeDarwinSignalRegistration));
}

static RRuntimeDarwinSignalStatus signal_register_locked(RRuntimeAllocator *allocator,
                                                         RRuntimeDarwinSignalKind kind,
                                                         int *native_error) {
    RRuntimeDarwinSignalEntry *entry = &signal_entries[kind];
    RRuntimeDarwinSignalRegistration *registration = NULL;
    struct sigaction ignore;
    dispatch_queue_t queue;
    void *allocation = NULL;

    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(RRuntimeDarwinSignalRegistration),
                                     _Alignof(RRuntimeDarwinSignalRegistration),
                                     &allocation) != R_RUNTIME_ALLOCATION_OK) {
        return R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED;
    }
    registration = allocation;
    registration->kind = kind;
    registration->registered = dispatch_semaphore_create(0);
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    registration->source =
        queue == NULL ? NULL
                      : dispatch_source_create(
                            DISPATCH_SOURCE_TYPE_SIGNAL, (uintptr_t)entry->number, 0U, queue);
    if (registration->registered == NULL || registration->source == NULL) {
        if (registration->registered != NULL) {
            dispatch_release(registration->registered);
        }
        if (registration->source != NULL) {
            dispatch_release(registration->source);
        }
        r_runtime_allocator_deallocate(registration, _Alignof(RRuntimeDarwinSignalRegistration));
        return R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED;
    }
    dispatch_set_context(registration->source, registration);
    dispatch_source_set_event_handler_f(registration->source, signal_event);
    dispatch_source_set_registration_handler_f(registration->source, signal_registered);
    dispatch_source_set_cancel_handler_f(registration->source, signal_registration_cancelled);
    dispatch_activate(registration->source);
    /* Deliveries are recorded once the kernel registration exists; only then is the default
       action replaced, so that no delivery after listen returns is lost or fatal. A source that
       the kernel does not register within the bound is cancelled; its cancel handler releases
       the semaphore. */
    if (dispatch_semaphore_wait(
            registration->registered,
            dispatch_time(DISPATCH_TIME_NOW,
                          (int64_t)R_RUNTIME_DARWIN_SIGNAL_REGISTRATION_NANOSECONDS)) != 0) {
        *native_error = ETIMEDOUT;
        dispatch_source_cancel(registration->source);
        return R_RUNTIME_DARWIN_SIGNAL_NATIVE_FAILED;
    }
    dispatch_release(registration->registered);
    registration->registered = NULL;
    ignore.sa_handler = SIG_IGN;
    ignore.sa_flags = 0;
    (void)sigemptyset(&ignore.sa_mask);
    if (sigaction(entry->number, &ignore, &entry->kept) != 0) {
        *native_error = errno;
        dispatch_source_cancel(registration->source);
        return R_RUNTIME_DARWIN_SIGNAL_NATIVE_FAILED;
    }
    entry->registration = registration;
    return R_RUNTIME_DARWIN_SIGNAL_OK;
}

static void signal_unregister_locked(RRuntimeDarwinSignalEntry *entry) {
    RRuntimeDarwinSignalRegistration *registration = entry->registration;

    if (registration == NULL) {
        abort();
    }
    entry->registration = NULL;
    (void)sigaction(entry->number, &entry->kept, NULL);
    dispatch_source_cancel(registration->source);
}

RRuntimeDarwinSignalListenResult r_runtime_darwin_signal_listen(RRuntimeAllocator *allocator,
                                                                RRuntimeDarwinSignalKind kind) {
    RRuntimeDarwinSignalListenResult result = {NULL, R_RUNTIME_DARWIN_SIGNAL_OK, 0};
    RRuntimeDarwinSignalListener *listener;
    RRuntimeDarwinSignalEntry *entry;
    void *allocation = NULL;

    if (allocator == NULL || !signal_kind_valid(kind)) {
        result.status = R_RUNTIME_DARWIN_SIGNAL_INVALID;
        return result;
    }
    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(RRuntimeDarwinSignalListener),
                                     _Alignof(RRuntimeDarwinSignalListener),
                                     &allocation) != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED;
        return result;
    }
    listener = allocation;
    listener->kind = kind;
    listener->references = 1U;
    listener->pending = UINT64_C(0);
    listener->previous = NULL;
    listener->next = NULL;
    listener->head = NULL;
    listener->tail = NULL;
    signal_lock();
    entry = &signal_entries[kind];
    if (entry->listeners == 0U) {
        result.status = signal_register_locked(allocator, kind, &result.native_error);
        if (result.status != R_RUNTIME_DARWIN_SIGNAL_OK) {
            signal_unlock();
            r_runtime_allocator_deallocate(listener, _Alignof(RRuntimeDarwinSignalListener));
            return result;
        }
    }
    listener->next = entry->first;
    if (entry->first != NULL) {
        entry->first->previous = listener;
    }
    entry->first = listener;
    entry->listeners += 1U;
    signal_unlock();
    result.listener = listener;
    return result;
}

void r_runtime_darwin_signal_listener_retain(RRuntimeDarwinSignalListener *listener) {
    if (listener == NULL) {
        abort();
    }
    signal_lock();
    if (listener->references == 0U || listener->references == SIZE_MAX) {
        abort();
    }
    listener->references += 1U;
    signal_unlock();
}

void r_runtime_darwin_signal_listener_release(RRuntimeDarwinSignalListener *listener) {
    RRuntimeDarwinSignalEntry *entry;

    if (listener == NULL) {
        return;
    }
    signal_lock();
    if (listener->references == 0U) {
        abort();
    }
    listener->references -= 1U;
    if (listener->references != 0U) {
        signal_unlock();
        return;
    }
    if (listener->head != NULL) {
        abort();
    }
    entry = &signal_entries[listener->kind];
    if (listener->previous == NULL) {
        entry->first = listener->next;
    } else {
        listener->previous->next = listener->next;
    }
    if (listener->next != NULL) {
        listener->next->previous = listener->previous;
    }
    entry->listeners -= 1U;
    if (entry->listeners == 0U) {
        signal_unregister_locked(entry);
    }
    signal_unlock();
    r_runtime_allocator_deallocate(listener, _Alignof(RRuntimeDarwinSignalListener));
}

int r_runtime_darwin_signal_raise(RRuntimeDarwinSignalKind kind) {
    if (!signal_kind_valid(kind)) {
        return EINVAL;
    }
    return kill(getpid(), signal_entries[kind].number) == 0 ? 0 : errno;
}

void r_runtime_darwin_signal_replaced_set(sigset_t *signals) {
    size_t index;

    if (signals == NULL) {
        abort();
    }
    (void)sigemptyset(signals);
    signal_lock();
    for (index = 0U; index < (size_t)R_RUNTIME_DARWIN_SIGNAL_KIND_COUNT; ++index) {
        const RRuntimeDarwinSignalEntry *entry = &signal_entries[index];

        if (entry->registration != NULL && entry->kept.sa_handler != SIG_IGN) {
            (void)sigaddset(signals, entry->number);
        }
    }
    signal_unlock();
}

static void signal_wait_timer(void *context) {
    RRuntimeDarwinSignalWait *wait = context;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    signal_lock();
    (void)signal_wait_select_locked(
        wait, R_RUNTIME_DARWIN_SIGNAL_TERMINAL_TIMED_OUT, event_sequence, UINT64_C(0));
    signal_unlock();
}

static void signal_wait_timer_cancelled(void *context) {
    RRuntimeDarwinSignalWait *wait = context;
    RRuntimeDarwinSignalWaitCompletionFn completion;
    void *completion_context;
    dispatch_source_t source;

    signal_lock();
    source = wait->timer_source;
    wait->timer_source = NULL;
    if (source == NULL || !wait->terminal_selected || wait->completion == NULL ||
        wait->completion_delivered) {
        signal_unlock();
        abort();
    }
    wait->completion_delivered = 1;
    completion = wait->completion;
    completion_context = wait->completion_context;
    signal_unlock();
    dispatch_release(source);
    completion(wait, completion_context);
}

RRuntimeDarwinSignalWaitPrepareResult
r_runtime_darwin_signal_wait_prepare(RRuntimeAllocator *allocator,
                                     RRuntimeDarwinSignalListener *listener,
                                     _Bool has_timeout,
                                     uint64_t timeout_nanoseconds) {
    RRuntimeDarwinSignalWaitPrepareResult result = {NULL, R_RUNTIME_DARWIN_SIGNAL_OK};
    RRuntimeDarwinSignalWait *wait;
    dispatch_queue_t queue;
    dispatch_time_t deadline = DISPATCH_TIME_FOREVER;
    void *allocation = NULL;

    if (allocator == NULL || listener == NULL) {
        result.status = R_RUNTIME_DARWIN_SIGNAL_INVALID;
        return result;
    }
    if (has_timeout) {
        if (timeout_nanoseconds > (uint64_t)INT64_MAX) {
            timeout_nanoseconds = (uint64_t)INT64_MAX;
        }
        deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
    }
    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(RRuntimeDarwinSignalWait),
                                     _Alignof(RRuntimeDarwinSignalWait),
                                     &allocation) != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED;
        return result;
    }
    wait = allocation;
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    wait->timer_source =
        queue == NULL
            ? NULL
            : dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
    if (wait->timer_source == NULL) {
        r_runtime_allocator_deallocate(wait, _Alignof(RRuntimeDarwinSignalWait));
        result.status = R_RUNTIME_DARWIN_SIGNAL_ALLOCATION_FAILED;
        return result;
    }
    dispatch_source_set_timer(wait->timer_source, deadline, DISPATCH_TIME_FOREVER, 0U);
    dispatch_set_context(wait->timer_source, wait);
    dispatch_source_set_event_handler_f(wait->timer_source, signal_wait_timer);
    dispatch_source_set_cancel_handler_f(wait->timer_source, signal_wait_timer_cancelled);
    wait->listener = listener;
    wait->next = NULL;
    wait->completion = NULL;
    wait->completion_context = NULL;
    wait->result = (RRuntimeDarwinSignalWaitResult){
        R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED, UINT64_C(0), UINT64_C(0)};
    wait->queued = 0;
    wait->timer_activated = 0;
    wait->terminal_selected = 0;
    wait->completion_delivered = 0;
    wait->count_returned = 0;
    r_runtime_darwin_signal_listener_retain(listener);
    result.wait = wait;
    return result;
}

void r_runtime_darwin_signal_wait_bind(RRuntimeDarwinSignalWait *wait,
                                       RRuntimeDarwinSignalWaitCompletionFn completion,
                                       void *completion_context) {
    if (wait == NULL || completion == NULL) {
        abort();
    }
    signal_lock();
    if (wait->completion != NULL) {
        signal_unlock();
        abort();
    }
    wait->completion = completion;
    wait->completion_context = completion_context;
    signal_unlock();
}

void r_runtime_darwin_signal_wait_activate(RRuntimeDarwinSignalWait *wait) {
    RRuntimeDarwinSignalListener *listener;

    if (wait == NULL) {
        abort();
    }
    signal_lock();
    if (wait->completion == NULL) {
        signal_unlock();
        abort();
    }
    if (!wait->terminal_selected && !wait->queued) {
        listener = wait->listener;
        if (listener->pending != UINT64_C(0)) {
            const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

            if (signal_wait_select_locked(wait,
                                          R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED,
                                          event_sequence,
                                          listener->pending)) {
                listener->pending = UINT64_C(0);
            }
        } else {
            wait->queued = 1;
            if (listener->tail == NULL) {
                listener->head = wait;
            } else {
                listener->tail->next = wait;
            }
            listener->tail = wait;
            signal_wait_activate_timer_locked(wait);
        }
    }
    signal_unlock();
}

void r_runtime_darwin_signal_wait_cancel(RRuntimeDarwinSignalWait *const *wait_slot,
                                         uint64_t cancellation_sequence) {
    RRuntimeDarwinSignalWait *wait;

    if (wait_slot == NULL) {
        abort();
    }
    signal_lock();
    wait = *wait_slot;
    if (wait != NULL &&
        signal_wait_select_locked(
            wait, R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED, cancellation_sequence, UINT64_C(0))) {
        signal_serve_locked(wait->listener, r_runtime_darwin_event_sequence_next());
    }
    signal_unlock();
}

RRuntimeDarwinSignalWaitResult r_runtime_darwin_signal_wait_result(RRuntimeDarwinSignalWait *wait) {
    RRuntimeDarwinSignalWaitResult result;

    if (wait == NULL) {
        abort();
    }
    signal_lock();
    if (!wait->terminal_selected) {
        signal_unlock();
        abort();
    }
    result = wait->result;
    signal_unlock();
    return result;
}

void r_runtime_darwin_signal_wait_return_count(RRuntimeDarwinSignalWait *wait) {
    if (wait == NULL) {
        abort();
    }
    signal_lock();
    if (wait->terminal_selected && !wait->count_returned &&
        wait->result.terminal_event == R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED) {
        signal_return_count_locked(wait);
        signal_serve_locked(wait->listener, r_runtime_darwin_event_sequence_next());
    }
    signal_unlock();
}

void r_runtime_darwin_signal_wait_release(RRuntimeDarwinSignalWait **wait_slot) {
    RRuntimeDarwinSignalListener *listener;
    RRuntimeDarwinSignalWait *wait;

    if (wait_slot == NULL) {
        abort();
    }
    signal_lock();
    wait = *wait_slot;
    *wait_slot = NULL;
    if (wait == NULL) {
        signal_unlock();
        return;
    }
    if (!wait->completion_delivered || wait->queued || wait->timer_source != NULL) {
        signal_unlock();
        abort();
    }
    listener = wait->listener;
    signal_unlock();
    r_runtime_allocator_deallocate(wait, _Alignof(RRuntimeDarwinSignalWait));
    r_runtime_darwin_signal_listener_release(listener);
}

void r_runtime_darwin_signal_wait_abort(RRuntimeDarwinSignalWait **wait_slot) {
    RRuntimeDarwinSignalWait *wait;
    dispatch_source_t source;
    RRuntimeDarwinSignalListener *listener;

    if (wait_slot == NULL) {
        abort();
    }
    signal_lock();
    wait = *wait_slot;
    *wait_slot = NULL;
    if (wait == NULL) {
        signal_unlock();
        return;
    }
    if (wait->completion != NULL || wait->queued || wait->terminal_selected) {
        signal_unlock();
        abort();
    }
    source = wait->timer_source;
    wait->timer_source = NULL;
    listener = wait->listener;
    signal_unlock();
    if (source != NULL) {
        dispatch_source_set_event_handler_f(source, NULL);
        dispatch_source_set_cancel_handler_f(source, NULL);
        dispatch_set_context(source, NULL);
        dispatch_source_cancel(source);
        dispatch_activate(source);
        dispatch_release(source);
    }
    r_runtime_allocator_deallocate(wait, _Alignof(RRuntimeDarwinSignalWait));
    r_runtime_darwin_signal_listener_release(listener);
}

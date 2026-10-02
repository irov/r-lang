#include "r_library_thread_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_task.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct RStdThreadDescriptor {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo payload_type;
    RStdThreadCompletionTypeInfo completion_type;
    RStdThreadEntryFn entry;
    void *payload;
    void *result;
    pthread_t native_thread;
    size_t references;
    _Bool dynamic_storage;
    _Bool native_thread_initialized;
    _Bool start_committed;
    _Bool exited;
    _Bool park_token;
    _Bool observer_attached;
    _Bool scoped;
    _Bool payload_initialized;
    _Bool result_initialized;
};

static RStdThreadDescriptor r_library_thread_main_descriptor = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
    .references = 1U,
    .start_committed = 1,
};

static _Thread_local RStdThreadDescriptor *r_library_thread_current_descriptor;
static pthread_key_t r_library_thread_executor_descriptor_key;
static pthread_once_t r_library_thread_executor_descriptor_key_once = PTHREAD_ONCE_INIT;

#if defined(R_STD_THREAD_TESTING)
static _Atomic int r_library_thread_testing_create_error;
#endif

_Noreturn void r_library_internal_thread_contract_violation(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void descriptor_lock(RStdThreadDescriptor *descriptor) {
    if (pthread_mutex_lock(&descriptor->mutex) != 0) {
        r_library_internal_thread_contract_violation();
    }
}

static void descriptor_unlock(RStdThreadDescriptor *descriptor) {
    if (pthread_mutex_unlock(&descriptor->mutex) != 0) {
        r_library_internal_thread_contract_violation();
    }
}

static void move_initialize(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.size == 0U) {
        return;
    }
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

static void drop_value(RRuntimeTypeInfo type, void *value) {
    if ((type.size != 0U) && (type.drop != NULL)) {
        type.drop(value);
    }
}

static RStdThreadError native_error(int error) {
    if (error == EPERM) {
        return R_STD_THREAD_ERROR_PERMISSION_DENIED;
    }
    if ((error == EAGAIN) || (error == ENOMEM)) {
        return R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED;
    }
    return R_STD_THREAD_ERROR_UNAVAILABLE;
}

static void destroy_unstarted_descriptor(RStdThreadDescriptor *descriptor,
                                         _Bool mutex_initialized,
                                         _Bool condition_initialized) {
    size_t payload_alignment;
    size_t result_alignment;

    payload_alignment = descriptor->payload_type.alignment;
    result_alignment = descriptor->completion_type.storage_type.alignment;

    if (condition_initialized && (pthread_cond_destroy(&descriptor->condition) != 0)) {
        r_library_internal_thread_contract_violation();
    }
    if (mutex_initialized && (pthread_mutex_destroy(&descriptor->mutex) != 0)) {
        r_library_internal_thread_contract_violation();
    }
    r_runtime_allocator_deallocate(descriptor->result, result_alignment);
    r_runtime_allocator_deallocate(descriptor->payload, payload_alignment);
    r_runtime_allocator_deallocate(descriptor, _Alignof(RStdThreadDescriptor));
}

static void descriptor_destroy(RStdThreadDescriptor *descriptor) {
    size_t payload_alignment;
    size_t result_alignment;

    payload_alignment = descriptor->payload_type.alignment;
    result_alignment = descriptor->completion_type.storage_type.alignment;
    if (descriptor->result_initialized) {
        descriptor->result_initialized = 0;
        drop_value(descriptor->completion_type.storage_type, descriptor->result);
    }
    r_runtime_allocator_deallocate(descriptor->result, result_alignment);
    descriptor->result = NULL;
    r_runtime_allocator_deallocate(descriptor->payload, payload_alignment);
    descriptor->payload = NULL;
    if (pthread_cond_destroy(&descriptor->condition) != 0) {
        r_library_internal_thread_contract_violation();
    }
    if (pthread_mutex_destroy(&descriptor->mutex) != 0) {
        r_library_internal_thread_contract_violation();
    }
    r_runtime_allocator_deallocate(descriptor, _Alignof(RStdThreadDescriptor));
}

static void descriptor_add_reference(RStdThreadDescriptor *descriptor) {
    _Bool overflow;

    descriptor_lock(descriptor);
    overflow = descriptor->references >= (SIZE_MAX / 2U);
    if (!overflow) {
        descriptor->references += 1U;
    }
    descriptor_unlock(descriptor);
    if (overflow) {
        r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
}

static void descriptor_release_reference(RStdThreadDescriptor *descriptor) {
    _Bool destroy;

    descriptor_lock(descriptor);
    descriptor->references -= 1U;
    destroy = descriptor->references == 0U;
    descriptor_unlock(descriptor);
    if (destroy) {
        descriptor_destroy(descriptor);
    }
}

static void executor_descriptor_destroy(void *value) {
    RStdThreadDescriptor *descriptor = value;

    if (descriptor == NULL) {
        return;
    }
    descriptor_lock(descriptor);
    descriptor->exited = 1;
    if (pthread_cond_broadcast(&descriptor->condition) != 0) {
        descriptor_unlock(descriptor);
        r_library_internal_thread_contract_violation();
    }
    descriptor_unlock(descriptor);
    if (r_library_thread_current_descriptor == descriptor) {
        r_library_thread_current_descriptor = NULL;
    }
    descriptor_release_reference(descriptor);
}

static void executor_descriptor_key_initialize(void) {
    if (pthread_key_create(&r_library_thread_executor_descriptor_key,
                           executor_descriptor_destroy) != 0) {
        r_library_internal_thread_contract_violation();
    }
}

static RStdThreadDescriptor *executor_descriptor_create(void) {
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    RStdThreadDescriptor *descriptor = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*descriptor), _Alignof(RStdThreadDescriptor), (void **)&descriptor) !=
            R_RUNTIME_ALLOCATION_OK) {
        r_runtime_panic(R_RUNTIME_PANIC_ALLOCATION_FAILURE,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    (void)memset(descriptor, 0, sizeof(*descriptor));
    descriptor->allocator = allocator;
    descriptor->dynamic_storage = 1;
    descriptor->native_thread = pthread_self();
    descriptor->native_thread_initialized = 1;
    descriptor->start_committed = 1;
    descriptor->references = 1U;
    if (pthread_mutex_init(&descriptor->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(descriptor, _Alignof(RStdThreadDescriptor));
        r_runtime_panic(R_RUNTIME_PANIC_ALLOCATION_FAILURE,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    if (pthread_cond_init(&descriptor->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&descriptor->mutex);
        r_runtime_allocator_deallocate(descriptor, _Alignof(RStdThreadDescriptor));
        r_runtime_panic(R_RUNTIME_PANIC_ALLOCATION_FAILURE,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    if (pthread_once(&r_library_thread_executor_descriptor_key_once,
                     executor_descriptor_key_initialize) != 0 ||
        pthread_setspecific(r_library_thread_executor_descriptor_key, descriptor) != 0) {
        if (pthread_cond_destroy(&descriptor->condition) != 0 ||
            pthread_mutex_destroy(&descriptor->mutex) != 0) {
            r_library_internal_thread_contract_violation();
        }
        r_runtime_allocator_deallocate(descriptor, _Alignof(RStdThreadDescriptor));
        r_runtime_panic(R_RUNTIME_PANIC_ALLOCATION_FAILURE,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    r_library_thread_current_descriptor = descriptor;
    return descriptor;
}

static RStdThreadDescriptor *current_descriptor(void) {
    if (r_library_thread_current_descriptor != NULL) {
        return r_library_thread_current_descriptor;
    }
#if defined(__APPLE__)
    if (pthread_main_np() == 0) {
        return executor_descriptor_create();
    }
#else
    return executor_descriptor_create();
#endif
    descriptor_lock(&r_library_thread_main_descriptor);
    if (!r_library_thread_main_descriptor.native_thread_initialized) {
        r_library_thread_main_descriptor.native_thread = pthread_self();
        r_library_thread_main_descriptor.native_thread_initialized = 1;
    } else if (!pthread_equal(r_library_thread_main_descriptor.native_thread, pthread_self())) {
        descriptor_unlock(&r_library_thread_main_descriptor);
        r_library_internal_thread_contract_violation();
    }
    descriptor_unlock(&r_library_thread_main_descriptor);
    r_library_thread_current_descriptor = &r_library_thread_main_descriptor;
    return r_library_thread_current_descriptor;
}

static void *thread_entry(void *context) {
    RStdThreadDescriptor *descriptor = context;

    if (!r_runtime_stack_initialize_current_thread()) {
        r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    descriptor_lock(descriptor);
    while (!descriptor->start_committed) {
        if (pthread_cond_wait(&descriptor->condition, &descriptor->mutex) != 0) {
            descriptor_unlock(descriptor);
            r_library_internal_thread_contract_violation();
        }
    }
    descriptor_unlock(descriptor);

    r_library_thread_current_descriptor = descriptor;
    r_runtime_stack_require(R_RUNTIME_GENERATED_FRAME_MAX_BYTES,
                            (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    descriptor->entry(descriptor->payload, descriptor->result);
    r_runtime_thread_local_cleanup_current();
    if (descriptor->payload_initialized) {
        descriptor->payload_initialized = 0;
        drop_value(descriptor->payload_type, descriptor->payload);
    }

    descriptor_lock(descriptor);
    descriptor->result_initialized = descriptor->completion_type.storage_type.size != 0U;
    descriptor->exited = 1;
    if (pthread_cond_broadcast(&descriptor->condition) != 0) {
        descriptor_unlock(descriptor);
        r_library_internal_thread_contract_violation();
    }
    descriptor_unlock(descriptor);
    r_library_thread_current_descriptor = NULL;
    descriptor_release_reference(descriptor);
    r_runtime_thread_local_cleanup_current();
    r_runtime_hosted_work_end();
    return NULL;
}

RStdThreadError
r_library_internal_thread_spawn_with_completion(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo payload_type,
                                                RStdThreadCompletionTypeInfo completion_type,
                                                RStdThreadEntryFn entry,
                                                void *staged_payload,
                                                _Bool scoped,
                                                RStdThreadJoinHandle *result,
                                                _Bool *started) {
    RStdThreadDescriptor *descriptor = NULL;
    RRuntimeAllocationStatus allocation_status;
    pthread_attr_t attributes;
    _Bool mutex_initialized = 0;
    _Bool condition_initialized = 0;
    int native_status;

    result->descriptor = NULL;
    *started = 0;

    allocation_status = r_runtime_allocator_allocate(
        allocator, sizeof(*descriptor), _Alignof(RStdThreadDescriptor), (void **)&descriptor);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED;
    }
    (void)memset(descriptor, 0, sizeof(*descriptor));
    descriptor->allocator = allocator;
    descriptor->payload_type = payload_type;
    descriptor->completion_type = completion_type;
    descriptor->entry = entry;
    descriptor->dynamic_storage = 1;
    descriptor->scoped = scoped;

    if (payload_type.size != 0U) {
        allocation_status = r_runtime_allocator_allocate(
            allocator, payload_type.size, payload_type.alignment, &descriptor->payload);
        if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
            destroy_unstarted_descriptor(descriptor, 0, 0);
            return R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED;
        }
    }
    if (completion_type.storage_type.size != 0U) {
        allocation_status = r_runtime_allocator_allocate(allocator,
                                                         completion_type.storage_type.size,
                                                         completion_type.storage_type.alignment,
                                                         &descriptor->result);
        if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
            destroy_unstarted_descriptor(descriptor, 0, 0);
            return R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED;
        }
    }

    native_status = pthread_mutex_init(&descriptor->mutex, NULL);
    if (native_status != 0) {
        destroy_unstarted_descriptor(descriptor, 0, 0);
        return native_error(native_status);
    }
    mutex_initialized = 1;
    native_status = pthread_cond_init(&descriptor->condition, NULL);
    if (native_status != 0) {
        destroy_unstarted_descriptor(descriptor, mutex_initialized, 0);
        return native_error(native_status);
    }
    condition_initialized = 1;
    native_status = pthread_attr_init(&attributes);
    if (native_status != 0) {
        destroy_unstarted_descriptor(descriptor, mutex_initialized, condition_initialized);
        return native_error(native_status);
    }
    native_status = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (native_status == 0) {
#if defined(R_STD_THREAD_TESTING)
        native_status = atomic_exchange_explicit(
            &r_library_thread_testing_create_error, 0, memory_order_relaxed);
#endif
        if (native_status == 0) {
            r_runtime_hosted_work_begin();
            native_status =
                pthread_create(&descriptor->native_thread, &attributes, thread_entry, descriptor);
            if (native_status != 0)
                r_runtime_hosted_work_end();
        }
    }
    if (pthread_attr_destroy(&attributes) != 0) {
        r_library_internal_thread_contract_violation();
    }
    if (native_status != 0) {
        destroy_unstarted_descriptor(descriptor, mutex_initialized, condition_initialized);
        return native_error(native_status);
    }

    descriptor->native_thread_initialized = 1;
    descriptor->references = 2U;
    descriptor->observer_attached = 1;
    descriptor_lock(descriptor);
    move_initialize(payload_type, descriptor->payload, staged_payload);
    descriptor->payload_initialized = payload_type.size != 0U;
    descriptor->start_committed = 1;
    if (pthread_cond_broadcast(&descriptor->condition) != 0) {
        descriptor_unlock(descriptor);
        r_library_internal_thread_contract_violation();
    }
    descriptor_unlock(descriptor);
    result->descriptor = descriptor;
    *started = 1;
    return R_STD_THREAD_ERROR_UNAVAILABLE;
}

RStdThreadError r_library_internal_thread_spawn(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo payload_type,
                                                RRuntimeTypeInfo result_type,
                                                RStdThreadEntryFn entry,
                                                void *staged_payload,
                                                _Bool scoped,
                                                RStdThreadJoinHandle *result,
                                                _Bool *started) {
    const RStdThreadCompletionTypeInfo completion_type = {
        .storage_type = result_type,
    };

    return r_library_internal_thread_spawn_with_completion(
        allocator, payload_type, completion_type, entry, staged_payload, scoped, result, started);
}

RStdThreadSpawnResult
r_library_internal_thread_spawn_checked(RRuntimeAllocator *allocator,
                                        RRuntimeTypeInfo payload_type,
                                        RStdThreadCompletionTypeInfo completion_type,
                                        RStdThreadEntryFn entry,
                                        void *staged_payload) {
    RStdThreadSpawnResult result = {0};

    result.error = r_library_internal_thread_spawn_with_completion(allocator,
                                                                   payload_type,
                                                                   completion_type,
                                                                   entry,
                                                                   staged_payload,
                                                                   0,
                                                                   &result.value,
                                                                   &result.is_ok);
    return result;
}

RStdThreadScopedSpawnResult
r_library_internal_thread_spawn_scoped_checked(RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo payload_type,
                                               RStdThreadCompletionTypeInfo completion_type,
                                               RStdThreadEntryFn entry,
                                               void *staged_payload) {
    RStdThreadScopedSpawnResult result = {0};

    result.error = r_library_internal_thread_spawn_with_completion(allocator,
                                                                   payload_type,
                                                                   completion_type,
                                                                   entry,
                                                                   staged_payload,
                                                                   1,
                                                                   &result.value,
                                                                   &result.is_ok);
    return result;
}

RStdThread r_library_internal_thread_current(void) {
    RStdThread result;
    result.descriptor = current_descriptor();
    descriptor_add_reference(result.descriptor);
    return result;
}

RStdThread r_library_internal_thread_clone(const RStdThread *source) {
    RStdThread result;

    result.descriptor = source->descriptor;
    descriptor_add_reference(result.descriptor);
    return result;
}

void r_library_internal_thread_unpark(const RStdThread *target) {
    RStdThreadDescriptor *descriptor;

    descriptor = target->descriptor;
    descriptor_lock(descriptor);
    if (!descriptor->exited) {
        descriptor->park_token = 1;
        if (pthread_cond_signal(&descriptor->condition) != 0) {
            descriptor_unlock(descriptor);
            r_library_internal_thread_contract_violation();
        }
    }
    descriptor_unlock(descriptor);
}

void r_library_internal_thread_park(void) {
    RStdThreadDescriptor *descriptor = current_descriptor();

    descriptor_lock(descriptor);
    while (!descriptor->park_token) {
        if (pthread_cond_wait(&descriptor->condition, &descriptor->mutex) != 0) {
            descriptor_unlock(descriptor);
            r_library_internal_thread_contract_violation();
        }
    }
    descriptor->park_token = 0;
    descriptor_unlock(descriptor);
}

RStdThreadJoinResult r_library_internal_thread_join(RStdThreadJoinHandle *handle) {
    RStdThreadJoinResult result = {0};
    RStdThreadDescriptor *descriptor;

    descriptor = handle->descriptor;
    descriptor_lock(descriptor);
    if (!descriptor->observer_attached) {
        descriptor_unlock(descriptor);
        r_library_internal_thread_contract_violation();
    }
    /* R-SLIB-PROC-0007: a task joining a thread that exits the process never resumes. */
    r_runtime_executor_join_begin(descriptor->native_thread);
    while (!descriptor->exited) {
        if (pthread_cond_wait(&descriptor->condition, &descriptor->mutex) != 0) {
            descriptor_unlock(descriptor);
            r_library_internal_thread_contract_violation();
        }
    }
    r_runtime_executor_join_end();
    descriptor->observer_attached = 0;
    handle->descriptor = NULL;
    result.completion_type = descriptor->completion_type;
    if (descriptor->completion_type.storage_type.size == 0U) {
        result.kind = R_STD_THREAD_JOIN_COMPLETED;
    } else {
        result.kind = R_STD_THREAD_JOIN_RETURNED;
        result.allocator = descriptor->allocator;
        result.value_type = descriptor->completion_type.storage_type;
        result.value = descriptor->result;
        descriptor->result = NULL;
        descriptor->result_initialized = 0;
    }
    descriptor_unlock(descriptor);
    descriptor_release_reference(descriptor);
    return result;
}

void r_library_internal_thread_detach(RStdThreadJoinHandle *handle) {
    RStdThreadDescriptor *descriptor;

    descriptor = handle->descriptor;
    descriptor_lock(descriptor);
    if (!descriptor->observer_attached) {
        descriptor_unlock(descriptor);
        r_library_internal_thread_contract_violation();
    }
    descriptor->observer_attached = 0;
    handle->descriptor = NULL;
    descriptor_unlock(descriptor);
    descriptor_release_reference(descriptor);
}

void r_library_internal_thread_identity_destroy(RStdThread *thread) {
    RStdThreadDescriptor *descriptor;

    if (thread->descriptor == NULL) {
        return;
    }
    descriptor = thread->descriptor;
    thread->descriptor = NULL;
    descriptor_release_reference(descriptor);
}

void r_library_internal_thread_handle_destroy(RStdThreadJoinHandle *handle) {
    RStdThreadDescriptor *descriptor;
    _Bool scoped;

    if (handle->descriptor == NULL) {
        return;
    }
    descriptor = handle->descriptor;
    descriptor_lock(descriptor);
    scoped = descriptor->scoped;
    descriptor_unlock(descriptor);
    if (scoped) {
        RStdThreadJoinResult result = r_library_internal_thread_join(handle);
        r_library_internal_thread_join_result_destroy(&result);
    } else {
        r_library_internal_thread_detach(handle);
    }
}

void r_library_internal_thread_join_result_destroy(RStdThreadJoinResult *result) {
    if (result->kind == R_STD_THREAD_JOIN_RETURNED) {
        void *value = result->value;
        const size_t alignment = result->value_type.alignment;

        result->value = NULL;
        drop_value(result->value_type, value);
        r_runtime_allocator_deallocate(value, alignment);
    } else if (result->kind == R_STD_THREAD_JOIN_PANICKED) {
        r_library_internal_thread_panic_report_destroy(&result->panic);
    }
    (void)memset(result, 0, sizeof(*result));
}

void r_library_internal_thread_panic_report_destroy(RStdThreadPanicReport *report) {
    r_runtime_string_destroy(&report->text);
    (void)memset(report, 0, sizeof(*report));
}

void r_library_internal_thread_join_result_move(RStdThreadJoinResult *result, void *destination) {
    void *value;
    size_t alignment;

    value = result->value;
    alignment = result->value_type.alignment;
    move_initialize(result->value_type, destination, value);
    result->value = NULL;
    r_runtime_allocator_deallocate(value, alignment);
    result->kind = R_STD_THREAD_JOIN_COMPLETED;
}

#if defined(R_STD_THREAD_TESTING)
void r_library_internal_thread_testing_fail_create(int native_error_value) {
    atomic_store_explicit(
        &r_library_thread_testing_create_error, native_error_value, memory_order_relaxed);
}
#endif

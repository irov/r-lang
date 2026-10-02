#include "r_library_fs_internal.h"

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static RLibraryFsHandleStorage *
reserve_storage(RRuntimeAllocator *allocator, size_t size, size_t alignment) {
    RLibraryFsHandleStorage *storage = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator, size, alignment, (void **)&storage) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(storage, 0, size);
    if (pthread_mutex_init(&storage->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(storage, alignment);
        return NULL;
    }
    atomic_init(&storage->references, 1U);
    storage->allocator = allocator;
    storage->allocation_alignment = alignment;
    storage->descriptor = -1;
    return storage;
}

RStdFsDirectoryStorage *r_library_internal_fs_directory_reserve(RRuntimeAllocator *allocator) {
    return (RStdFsDirectoryStorage *)reserve_storage(
        allocator, sizeof(RStdFsDirectoryStorage), _Alignof(RStdFsDirectoryStorage));
}

RStdFsFileStorage *r_library_internal_fs_file_reserve(RRuntimeAllocator *allocator) {
    return (RStdFsFileStorage *)reserve_storage(
        allocator, sizeof(RStdFsFileStorage), _Alignof(RStdFsFileStorage));
}

RStdFsDirectoryIterStorage *
r_library_internal_fs_directory_iter_reserve(RRuntimeAllocator *allocator) {
    RStdFsDirectoryIterStorage *storage = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*storage), _Alignof(RStdFsDirectoryIterStorage), (void **)&storage) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(storage, 0, sizeof(*storage));
    storage->allocator = allocator;
    storage->descriptor = -1;
    return storage;
}

static void publish_storage(RLibraryFsHandleStorage *storage, int descriptor) {
    if (storage == NULL || descriptor < 0 || pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->allocator == NULL || storage->descriptor >= 0 || storage->terminal ||
        atomic_load_explicit(&storage->references, memory_order_acquire) != 1U) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    storage->descriptor = descriptor;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
}

void r_library_internal_fs_directory_publish(RStdFsDirectoryStorage *storage, int descriptor) {
    publish_storage(storage == NULL ? NULL : &storage->handle, descriptor);
}

void r_library_internal_fs_file_publish(RStdFsFileStorage *storage,
                                        int descriptor,
                                        RStdFsOpenFileOptions options,
                                        RRuntimeDarwinIoHandle *payload_io) {
    if (storage == NULL || descriptor < 0 || payload_io == NULL ||
        pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    if (storage->handle.allocator == NULL || storage->handle.descriptor >= 0 ||
        storage->handle.payload_io != NULL || storage->handle.terminal ||
        atomic_load_explicit(&storage->handle.references, memory_order_acquire) != 1U) {
        (void)pthread_mutex_unlock(&storage->handle.mutex);
        abort();
    }
    storage->handle.descriptor = descriptor;
    storage->handle.access = options.access;
    storage->handle.append = options.append;
    storage->handle.payload_io = payload_io;
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
}

void r_library_internal_fs_directory_iter_publish(RStdFsDirectoryIterStorage *storage,
                                                  int descriptor) {
    if (storage == NULL || descriptor < 0 || storage->descriptor >= 0) {
        abort();
    }
    storage->descriptor = descriptor;
}

static int storage_descriptor(RLibraryFsHandleStorage *storage) {
    int descriptor;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        return -1;
    }
    descriptor = storage->terminal || storage->close_reserved ? -1 : storage->descriptor;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return descriptor;
}

RLibraryFsHandleStorage *
r_library_internal_fs_directory_handle_storage(const RStdFsDirectory *directory) {
    return directory == NULL || directory->storage == NULL ? NULL : &directory->storage->handle;
}

RLibraryFsHandleStorage *r_library_internal_fs_file_handle_storage(const RStdFsFile *file) {
    return file == NULL || file->storage == NULL ? NULL : &file->storage->handle;
}

int r_library_internal_fs_directory_descriptor(const RStdFsDirectory *directory) {
    return storage_descriptor(r_library_internal_fs_directory_handle_storage(directory));
}

int r_library_internal_fs_file_descriptor(const RStdFsFile *file) {
    return storage_descriptor(r_library_internal_fs_file_handle_storage(file));
}

RStdFsAccess r_library_internal_fs_file_access(const RStdFsFile *file) {
    RLibraryFsHandleStorage *storage = r_library_internal_fs_file_handle_storage(file);
    RStdFsAccess access;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    access = storage->access;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return access;
}

RRuntimeDarwinIoStartStatus r_library_internal_fs_payload_io_retain_locked(
    RLibraryFsHandleStorage *storage, RRuntimeDarwinIoHandle **payload_io, int *native_error) {
    int terminal_error = 0;

    if (storage == NULL || payload_io == NULL || native_error == NULL) {
        return R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT;
    }
    *payload_io = NULL;
    *native_error = 0;
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0 ||
        storage->payload_io == NULL) {
        return R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED;
    }
    if (!r_runtime_darwin_io_handle_retain_view(storage->payload_io)) {
        (void)r_runtime_darwin_io_handle_terminal_close_failure(storage->payload_io,
                                                                &terminal_error);
        *native_error = terminal_error;
        return R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED;
    }
    *payload_io = storage->payload_io;
    return R_RUNTIME_DARWIN_IO_START_OK;
}

_Bool r_library_internal_fs_handle_retain(RLibraryFsHandleStorage *storage) {
    _Bool retained = 0;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    if (!storage->terminal && !storage->close_reserved && storage->descriptor >= 0 &&
        atomic_load_explicit(&storage->references, memory_order_relaxed) != SIZE_MAX) {
        (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
        retained = 1;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return retained;
}

void r_library_internal_fs_handle_retain_registered(RLibraryFsHandleStorage *storage) {
    size_t previous;

    if (storage == NULL) {
        abort();
    }
    previous = atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
    if (previous == 0U || previous == SIZE_MAX) {
        abort();
    }
}

void r_library_internal_fs_handle_release(RLibraryFsHandleStorage *storage) {
    RRuntimeDarwinIoHandle *payload_io;
    size_t previous;

    if (storage == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&storage->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        abort();
    }
    if (previous != 1U) {
        return;
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->operations != NULL || storage->position_head != NULL ||
        storage->position_tail != NULL || storage->drain != NULL || storage->close_reserved ||
        storage->position_pumping) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    payload_io = storage->payload_io;
    storage->payload_io = NULL;
    if (storage->descriptor >= 0) {
        (void)close(storage->descriptor);
        storage->descriptor = -1;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&storage->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_io_handle_release(payload_io);
    r_runtime_allocator_deallocate(storage, storage->allocation_alignment);
}

void r_library_internal_fs_directory_storage_release(RStdFsDirectoryStorage *storage) {
    r_library_internal_fs_handle_release(storage == NULL ? NULL : &storage->handle);
}

void r_library_internal_fs_file_storage_release(RStdFsFileStorage *storage) {
    r_library_internal_fs_handle_release(storage == NULL ? NULL : &storage->handle);
}

void r_library_internal_fs_directory_iter_storage_release(RStdFsDirectoryIterStorage *storage) {
    if (storage == NULL) {
        return;
    }
    r_runtime_darwin_fs_enumeration_buffer_release(&storage->cache);
    if (storage->descriptor >= 0) {
        (void)close(storage->descriptor);
        storage->descriptor = -1;
    }
    r_runtime_allocator_deallocate(storage, _Alignof(RStdFsDirectoryIterStorage));
}

static void registration_initialize_callback(RLibraryFsHandleStorage *storage,
                                             RLibraryFsOperationRegistration *registration,
                                             RLibraryFsOperationCancelFn cancel,
                                             RLibraryFsOperationRetainFn retain_context,
                                             RLibraryFsOperationReleaseFn release_context,
                                             void *context);

static void fs_request_cancel(void *context) {
    (void)r_runtime_darwin_fs_request_cancel(context);
}

static void fs_request_retain(void *context) {
    r_runtime_darwin_fs_request_retain(context);
}

static void fs_request_release(void *context) {
    r_runtime_darwin_fs_request_release(context);
}

_Bool r_library_internal_fs_operation_register(RLibraryFsHandleStorage *storage,
                                               RLibraryFsOperationRegistration *registration,
                                               RRuntimeDarwinFsRequest *request) {
    if (storage == NULL || registration == NULL || request == NULL ||
        pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    if (storage->terminal || storage->close_reserved || registration->registered ||
        registration->storage != NULL || registration->position != NULL) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return 0;
    }
    registration_initialize_callback(
        storage, registration, fs_request_cancel, fs_request_retain, fs_request_release, request);
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return 1;
}

static RLibraryFsPositionNode *position_ready_head_locked(RLibraryFsHandleStorage *storage) {
    RLibraryFsPositionNode *node = storage->position_head;
    size_t references;

    if (storage->terminal || node == NULL || node->state != R_LIBRARY_FS_POSITION_READY) {
        return NULL;
    }
    if (node->retain_context == NULL || node->release_context == NULL || node->context == NULL) {
        abort();
    }
    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    if (references == SIZE_MAX) {
        abort();
    }
    (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
    node->retain_context(node->context);
    node->state = R_LIBRARY_FS_POSITION_ACTIVATING;
    return node;
}

static void position_activate_one(RLibraryFsPositionNode *node, uint64_t position) {
    RLibraryFsHandleStorage *storage = node->storage;
    RLibraryFsPositionActivateFn activate = node->activate;
    RLibraryFsPositionCancelFn cancel = node->cancel;
    RLibraryFsOperationReleaseFn release_context = node->release_context;
    RLibraryFsPositionCancelReason reason = R_LIBRARY_FS_POSITION_CANCEL_TASK;
    void *context = node->context;
    _Bool notify = 0;

    if (storage == NULL || activate == NULL || cancel == NULL || release_context == NULL ||
        context == NULL) {
        abort();
    }
    activate(context, position);
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state == R_LIBRARY_FS_POSITION_ACTIVATING) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    if (node->state == R_LIBRARY_FS_POSITION_FINISHED) {
        if (node->cancel_pending && !node->cancel_delivered) {
            node->cancel_delivered = 1;
            reason = node->cancel_reason;
            notify = 1;
        }
    } else if (node->state != R_LIBRARY_FS_POSITION_ACTIVE) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (notify) {
        cancel(context, reason, 0);
    }
    release_context(context);
    r_library_internal_fs_handle_release(storage);
}

static void position_pump(RLibraryFsHandleStorage *storage) {
    RLibraryFsPositionNode *node;
    size_t references;
    uint64_t position;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->position_pumping) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return;
    }
    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    if (references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
    storage->position_pumping = 1;
    for (;;) {
        node = position_ready_head_locked(storage);
        position = storage->position;
        if (node == NULL) {
            storage->position_pumping = 0;
            if (pthread_mutex_unlock(&storage->mutex) != 0) {
                abort();
            }
            r_library_internal_fs_handle_release(storage);
            return;
        }
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        position_activate_one(node, position);
        if (pthread_mutex_lock(&storage->mutex) != 0) {
            abort();
        }
    }
}

static void registration_initialize_callback(RLibraryFsHandleStorage *storage,
                                             RLibraryFsOperationRegistration *registration,
                                             RLibraryFsOperationCancelFn cancel,
                                             RLibraryFsOperationRetainFn retain_context,
                                             RLibraryFsOperationReleaseFn release_context,
                                             void *context) {
    retain_context(context);
    registration->storage = storage;
    registration->cancel = cancel;
    registration->retain_context = retain_context;
    registration->release_context = release_context;
    registration->cancel_context = context;
    registration->position = NULL;
    registration->registered = 1;
    registration->next = storage->operations;
    storage->operations = registration;
}

_Bool r_library_internal_fs_position_reserve_locked(RLibraryFsHandleStorage *storage,
                                                    RLibraryFsPositionNode *node,
                                                    RLibraryFsOperationRegistration *registration,
                                                    RLibraryFsPositionActivateFn activate,
                                                    RLibraryFsPositionCancelFn cancel,
                                                    void *context,
                                                    RLibraryFsOperationCancelFn close_cancel,
                                                    RLibraryFsOperationRetainFn retain_context,
                                                    RLibraryFsOperationReleaseFn release_context,
                                                    int *descriptor,
                                                    RStdFsAccess *access,
                                                    _Bool *append) {
    size_t references;

    if (storage == NULL || node == NULL || registration == NULL || activate == NULL ||
        cancel == NULL || context == NULL || close_cancel == NULL || retain_context == NULL ||
        release_context == NULL || descriptor == NULL || access == NULL || append == NULL) {
        return 0;
    }
    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0 ||
        references == SIZE_MAX || registration->registered || registration->storage != NULL ||
        registration->position != NULL || node->storage != NULL) {
        return 0;
    }
    (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
    registration_initialize_callback(
        storage, registration, close_cancel, retain_context, release_context, context);
    node->storage = storage;
    node->activate = activate;
    node->cancel = cancel;
    node->retain_context = retain_context;
    node->release_context = release_context;
    node->context = context;
    node->state = R_LIBRARY_FS_POSITION_RESERVED;
    node->cancel_reason = R_LIBRARY_FS_POSITION_CANCEL_TASK;
    node->activation_begun = 0;
    node->cancel_pending = 0;
    node->cancel_delivered = 0;
    registration->position = node;
    if (storage->position_tail == NULL) {
        storage->position_head = node;
    } else {
        storage->position_tail->next = node;
    }
    storage->position_tail = node;
    *descriptor = storage->descriptor;
    *access = storage->access;
    *append = storage->append;
    return 1;
}

_Bool r_library_internal_fs_position_reserve(RLibraryFsHandleStorage *storage,
                                             RLibraryFsPositionNode *node,
                                             RLibraryFsOperationRegistration *registration,
                                             RLibraryFsPositionActivateFn activate,
                                             RLibraryFsPositionCancelFn cancel,
                                             void *context,
                                             RLibraryFsOperationCancelFn close_cancel,
                                             RLibraryFsOperationRetainFn retain_context,
                                             RLibraryFsOperationReleaseFn release_context,
                                             int *descriptor,
                                             RStdFsAccess *access,
                                             _Bool *append) {
    _Bool reserved;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    reserved = r_library_internal_fs_position_reserve_locked(storage,
                                                             node,
                                                             registration,
                                                             activate,
                                                             cancel,
                                                             context,
                                                             close_cancel,
                                                             retain_context,
                                                             release_context,
                                                             descriptor,
                                                             access,
                                                             append);
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return reserved;
}

void r_library_internal_fs_position_publish(RLibraryFsPositionNode *node) {
    RLibraryFsHandleStorage *storage;

    if (node == NULL || node->storage == NULL) {
        abort();
    }
    storage = node->storage;
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state == R_LIBRARY_FS_POSITION_RESERVED) {
        node->state = R_LIBRARY_FS_POSITION_READY;
    } else if (node->state != R_LIBRARY_FS_POSITION_FINISHED) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    position_pump(storage);
}

static void position_remove_locked(RLibraryFsHandleStorage *storage, RLibraryFsPositionNode *node) {
    RLibraryFsPositionNode *previous = NULL;
    RLibraryFsPositionNode *cursor = storage->position_head;

    while (cursor != NULL && cursor != node) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (cursor == NULL) {
        abort();
    }
    if (previous == NULL) {
        storage->position_head = node->next;
    } else {
        previous->next = node->next;
    }
    if (storage->position_tail == node) {
        storage->position_tail = previous;
    }
    node->next = NULL;
    node->state = R_LIBRARY_FS_POSITION_FINISHED;
}

void r_library_internal_fs_position_abort(RLibraryFsPositionNode *node) {
    RLibraryFsHandleStorage *storage;

    if (node == NULL || node->storage == NULL) {
        return;
    }
    storage = node->storage;
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state == R_LIBRARY_FS_POSITION_FINISHED) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return;
    }
    if (node->state != R_LIBRARY_FS_POSITION_RESERVED) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    position_remove_locked(storage, node);
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    position_pump(storage);
}

_Bool r_library_internal_fs_position_activation_begin(RLibraryFsPositionNode *node) {
    RLibraryFsHandleStorage *storage;
    _Bool begun;

    if (node == NULL || node->storage == NULL) {
        abort();
    }
    storage = node->storage;
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state != R_LIBRARY_FS_POSITION_ACTIVATING) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    if (node->activation_begun) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    begun = !node->cancel_pending;
    if (begun) {
        node->activation_begun = 1;
    } else {
        position_remove_locked(storage, node);
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return begun;
}

void r_library_internal_fs_position_activation_commit(RLibraryFsPositionNode *node) {
    RLibraryFsHandleStorage *storage;
    RLibraryFsPositionCancelFn cancel = NULL;
    RLibraryFsOperationReleaseFn release_context = NULL;
    RLibraryFsPositionCancelReason reason = R_LIBRARY_FS_POSITION_CANCEL_TASK;
    void *context = NULL;

    if (node == NULL || node->storage == NULL) {
        abort();
    }
    storage = node->storage;
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state != R_LIBRARY_FS_POSITION_ACTIVATING || !node->activation_begun) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    node->state = R_LIBRARY_FS_POSITION_ACTIVE;
    if (node->cancel_pending && !node->cancel_delivered) {
        if (node->cancel == NULL || node->retain_context == NULL || node->release_context == NULL ||
            node->context == NULL) {
            (void)pthread_mutex_unlock(&storage->mutex);
            abort();
        }
        node->cancel_delivered = 1;
        node->retain_context(node->context);
        cancel = node->cancel;
        release_context = node->release_context;
        context = node->context;
        reason = node->cancel_reason;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (cancel != NULL) {
        cancel(context, reason, 1);
        release_context(context);
    }
}

/* Unregistration detaches the node under storage->mutex, so membership is decided there; the
   final pump runs on a storage that the caller keeps alive. */
static void position_cancel_in(RLibraryFsHandleStorage *storage,
                               RLibraryFsPositionNode *node,
                               RLibraryFsPositionCancelReason reason) {
    RLibraryFsPositionCancelFn cancel = NULL;
    RLibraryFsOperationReleaseFn release_context = NULL;
    void *context = NULL;
    _Bool active = 0;
    _Bool notify = 0;

    if (reason != R_LIBRARY_FS_POSITION_CANCEL_TASK &&
        reason != R_LIBRARY_FS_POSITION_CANCEL_CLOSE &&
        reason != R_LIBRARY_FS_POSITION_CANCEL_DEADLINE) {
        abort();
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->storage != storage) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return;
    }
    if (!node->cancel_pending) {
        node->cancel_pending = 1;
        node->cancel_reason = reason;
        active = node->state == R_LIBRARY_FS_POSITION_ACTIVE;
        if (node->state == R_LIBRARY_FS_POSITION_RESERVED ||
            node->state == R_LIBRARY_FS_POSITION_READY) {
            position_remove_locked(storage, node);
            notify = 1;
        } else if (node->state == R_LIBRARY_FS_POSITION_ACTIVATING) {
            notify = 0;
        } else if (active) {
            notify = 1;
        } else if (node->state != R_LIBRARY_FS_POSITION_FINISHED) {
            (void)pthread_mutex_unlock(&storage->mutex);
            abort();
        }
        if (notify) {
            if (node->cancel == NULL || node->retain_context == NULL ||
                node->release_context == NULL || node->context == NULL) {
                (void)pthread_mutex_unlock(&storage->mutex);
                abort();
            }
            node->cancel_delivered = 1;
            node->retain_context(node->context);
            cancel = node->cancel;
            release_context = node->release_context;
            context = node->context;
        }
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (notify) {
        cancel(context, reason, active);
        release_context(context);
    }
    position_pump(storage);
}

void r_library_internal_fs_position_cancel(RLibraryFsPositionNode *node,
                                           RLibraryFsPositionCancelReason reason) {
    if (node == NULL || node->storage == NULL) {
        return;
    }
    position_cancel_in(node->storage, node, reason);
}

void r_library_internal_fs_position_cancel_retained(RLibraryFsHandleStorage *storage,
                                                    RLibraryFsPositionNode *node,
                                                    RLibraryFsPositionCancelReason reason) {
    if (storage == NULL || node == NULL) {
        abort();
    }
    position_cancel_in(storage, node, reason);
}

void r_library_internal_fs_position_finish(RLibraryFsPositionNode *node,
                                           RLibraryFsPositionUpdate update,
                                           uint64_t value) {
    RLibraryFsHandleStorage *storage;

    if (node == NULL || node->storage == NULL) {
        abort();
    }
    storage = node->storage;
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (node->state != R_LIBRARY_FS_POSITION_ACTIVE || storage->position_head != node) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    if (update == R_LIBRARY_FS_POSITION_ADVANCE) {
        if (value > UINT64_MAX - storage->position) {
            (void)pthread_mutex_unlock(&storage->mutex);
            abort();
        }
        storage->position += value;
    } else if (update == R_LIBRARY_FS_POSITION_SET) {
        storage->position = value;
    } else if (update != R_LIBRARY_FS_POSITION_KEEP) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    position_remove_locked(storage, node);
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    position_pump(storage);
}

void r_library_internal_fs_operation_unregister(RLibraryFsHandleStorage *storage,
                                                RLibraryFsOperationRegistration *registration) {
    RLibraryFsOperationRegistration **cursor;
    RLibraryFsPositionNode *position = NULL;
    RLibraryFsDrainFn drain = NULL;
    RLibraryFsOperationReleaseFn release_context = NULL;
    void *drain_context = NULL;
    void *cancel_context = NULL;

    if (storage == NULL || registration == NULL) {
        return;
    }
    if (pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (!registration->registered || registration->storage != storage) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return;
    }
    position = registration->position;
    if (position != NULL &&
        (position->storage != storage || position->state != R_LIBRARY_FS_POSITION_FINISHED)) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    cursor = &storage->operations;
    while (*cursor != NULL && *cursor != registration) {
        cursor = &(*cursor)->next;
    }
    if (*cursor != registration) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    *cursor = registration->next;
    release_context = registration->release_context;
    cancel_context = registration->cancel_context;
    registration->next = NULL;
    registration->storage = NULL;
    registration->cancel = NULL;
    registration->retain_context = NULL;
    registration->release_context = NULL;
    registration->cancel_context = NULL;
    registration->position = NULL;
    registration->registered = 0;
    registration->cancel_requested = 0;
    if (position != NULL) {
        position->storage = NULL;
    }
    if (storage->terminal && storage->operations == NULL && storage->drain != NULL) {
        drain = storage->drain;
        drain_context = storage->drain_context;
        storage->drain = NULL;
        storage->drain_context = NULL;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    if (drain != NULL) {
        drain(drain_context);
    }
    if (release_context != NULL) {
        release_context(cancel_context);
    }
    r_library_internal_fs_handle_release(storage);
}

_Bool r_library_internal_fs_handle_reserve_close(RLibraryFsHandleStorage *storage,
                                                 int *descriptor,
                                                 RRuntimeDarwinIoHandle **payload_io) {
    if (storage == NULL || descriptor == NULL || payload_io == NULL ||
        pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0 ||
        storage->drain != NULL) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return 0;
    }
    storage->close_reserved = 1;
    *descriptor = storage->descriptor;
    *payload_io = storage->payload_io;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
    return 1;
}

void r_library_internal_fs_handle_abort_close(RLibraryFsHandleStorage *storage) {
    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        abort();
    }
    if (storage->terminal || !storage->close_reserved || storage->descriptor < 0 ||
        storage->drain != NULL) {
        (void)pthread_mutex_unlock(&storage->mutex);
        abort();
    }
    storage->close_reserved = 0;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_handle_begin_close(RLibraryFsHandleStorage *storage,
                                               RLibraryFsDrainFn drain,
                                               void *drain_context,
                                               int *descriptor,
                                               RRuntimeDarwinIoHandle **payload_io,
                                               _Bool *already_drained) {
    RLibraryFsOperationRegistration *operation;

    if (storage == NULL || drain == NULL || descriptor == NULL || payload_io == NULL ||
        already_drained == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        return 0;
    }
    if (storage->terminal || !storage->close_reserved || storage->descriptor < 0 ||
        storage->drain != NULL) {
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        return 0;
    }
    storage->close_reserved = 0;
    storage->terminal = 1;
    storage->drain = drain;
    storage->drain_context = drain_context;
    *descriptor = storage->descriptor;
    storage->descriptor = -1;
    *payload_io = storage->payload_io;
    storage->payload_io = NULL;
    *already_drained = storage->operations == NULL;
    if (*already_drained) {
        storage->drain = NULL;
        storage->drain_context = NULL;
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        abort();
    }

    for (;;) {
        RLibraryFsOperationCancelFn cancel = NULL;
        RLibraryFsOperationReleaseFn release_context = NULL;
        void *cancel_context = NULL;

        if (pthread_mutex_lock(&storage->mutex) != 0) {
            abort();
        }
        for (operation = storage->operations; operation != NULL; operation = operation->next) {
            if (operation->cancel_requested) {
                continue;
            }
            operation->cancel_requested = 1;
            if (operation->cancel == NULL || operation->retain_context == NULL ||
                operation->release_context == NULL || operation->cancel_context == NULL) {
                (void)pthread_mutex_unlock(&storage->mutex);
                abort();
            }
            operation->retain_context(operation->cancel_context);
            cancel = operation->cancel;
            release_context = operation->release_context;
            cancel_context = operation->cancel_context;
            break;
        }
        if (pthread_mutex_unlock(&storage->mutex) != 0) {
            abort();
        }
        if (operation == NULL) {
            break;
        }
        if (cancel != NULL) {
            cancel(cancel_context);
            release_context(cancel_context);
        }
    }
    return 1;
}

/* Move-initialize destinations do not yet contain an active R value and must not be inspected. */
void r_library_internal_fs_directory_move(RStdFsDirectory *destination, RStdFsDirectory *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_fs_directory_drop(RStdFsDirectory *directory) {
    RStdFsDirectoryStorage *storage;

    storage = directory->storage;
    directory->storage = NULL;
    r_library_internal_fs_directory_storage_release(storage);
}

void r_library_internal_fs_file_move(RStdFsFile *destination, RStdFsFile *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_fs_file_drop(RStdFsFile *file) {
    RStdFsFileStorage *storage;

    storage = file->storage;
    file->storage = NULL;
    r_library_internal_fs_file_storage_release(storage);
}

void r_library_internal_fs_directory_iter_move(RStdFsDirectoryIter *destination,
                                               RStdFsDirectoryIter *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_fs_directory_iter_drop(RStdFsDirectoryIter *iterator) {
    RStdFsDirectoryIterStorage *storage;

    storage = iterator->storage;
    iterator->storage = NULL;
    r_library_internal_fs_directory_iter_storage_release(storage);
}

void r_library_internal_fs_directory_entry_move(RStdFsDirectoryEntry *destination,
                                                RStdFsDirectoryEntry *source) {
    *destination = *source;
    source->name.storage = NULL;
}

void r_library_internal_fs_directory_entry_drop(RStdFsDirectoryEntry *entry) {
    if (entry->name.storage != NULL) {
        r_runtime_allocator_deallocate(entry->name.storage, _Alignof(max_align_t));
        entry->name.storage = NULL;
    }
}

void r_library_internal_fs_directory_next_result_move(RStdFsDirectoryNextResult *destination,
                                                      RStdFsDirectoryNextResult *source) {
    *destination = *source;
    source->iterator.storage = NULL;
    source->entry.name.storage = NULL;
}

void r_library_internal_fs_directory_next_result_drop(RStdFsDirectoryNextResult *result) {
    if (result->kind == R_STD_FS_DIRECTORY_NEXT_ENTRY) {
        r_library_internal_fs_directory_entry_drop(&result->entry);
        r_library_internal_fs_directory_iter_drop(&result->iterator);
    } else if (result->kind == R_STD_FS_DIRECTORY_NEXT_FAILED) {
        r_library_internal_fs_directory_iter_drop(&result->iterator);
    }
    (void)memset(result, 0, sizeof(*result));
    result->kind = R_STD_FS_DIRECTORY_NEXT_END;
}

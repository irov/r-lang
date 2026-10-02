#include "r_library_process_internal.h"

#include <stdlib.h>
#include <string.h>

RStdProcessChildStorage *r_library_internal_process_child_reserve(RRuntimeAllocator *allocator) {
    RStdProcessChildStorage *storage = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*storage), _Alignof(RStdProcessChildStorage), (void **)&storage) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(storage, 0, sizeof(*storage));
    storage->allocator = allocator;
    return storage;
}

void r_library_internal_process_child_publish(RStdProcessChildStorage *storage,
                                              RRuntimeDarwinProcessChild *native) {
    if (storage == NULL || storage->allocator == NULL || storage->native != NULL ||
        native == NULL) {
        abort();
    }
    storage->native = native;
}

void r_library_internal_process_child_storage_destroy(RStdProcessChildStorage *storage) {
    if (storage == NULL) {
        return;
    }
    if (storage->native != NULL) {
        r_runtime_darwin_process_child_release(storage->native);
        storage->native = NULL;
    }
    storage->allocator = NULL;
    r_runtime_allocator_deallocate(storage, _Alignof(RStdProcessChildStorage));
}

RRuntimeDarwinIoHandle *r_library_internal_process_child_take_pipe(RStdProcessChild *child,
                                                                   RRuntimeDarwinProcessPipe pipe) {
    if (child == NULL || child->storage == NULL || child->storage->native == NULL) {
        return NULL;
    }
    return r_runtime_darwin_process_child_take_pipe(child->storage->native, pipe);
}

void r_library_internal_process_child_move(RStdProcessChild *destination,
                                           RStdProcessChild *source) {
    *destination = *source;
    source->storage = NULL;
    source->identity = UINT64_C(0);
}

void r_library_internal_process_child_destroy(RStdProcessChild *child) {
    r_library_internal_process_child_storage_destroy(child->storage);
    child->storage = NULL;
    child->identity = UINT64_C(0);
}

#if defined(R_LIBRARY_PROCESS_TESTING)
RRuntimeDarwinProcessChild *
r_library_internal_process_child_testing_native(const RStdProcessChild *child) {
    return child == NULL || child->storage == NULL ? NULL : child->storage->native;
}
#endif

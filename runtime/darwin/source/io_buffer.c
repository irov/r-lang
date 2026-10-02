#include "r_runtime_darwin_io.h"

#include <stddef.h>
#include <string.h>

RRuntimeDarwinIoBufferResult r_runtime_darwin_io_buffer_allocate(RRuntimeAllocator *allocator,
                                                                 size_t capacity) {
    RRuntimeDarwinIoBufferResult result;
    void *allocation = NULL;
    RRuntimeAllocationStatus status;

    (void)memset(&result, 0, sizeof(result));
    result.status = R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT;
    if (capacity == 0U) {
        return result;
    }
    status =
        r_runtime_allocator_allocate(allocator, capacity, _Alignof(unsigned char), &allocation);
    if (status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED;
        return result;
    }
    result.buffer.allocator = allocator;
    result.buffer.data = allocation;
    result.buffer.capacity = capacity;
    result.status = R_RUNTIME_DARWIN_IO_START_OK;
    return result;
}

void r_runtime_darwin_io_buffer_release(RRuntimeDarwinIoBuffer *buffer) {
    if (buffer->data != NULL) {
        r_runtime_allocator_deallocate(buffer->data, _Alignof(unsigned char));
    }
    (void)memset(buffer, 0, sizeof(*buffer));
}

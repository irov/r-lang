#ifndef R_STD_C_H
#define R_STD_C_H

#include "r_std_convert.h"
#include "r_std_error_types.h"
#include "r_std_string.h"

#include "r_runtime_thread_attachment.h"

#include <stddef.h>
#include <stdint.h>

typedef RRuntimeArray RStdCString;

typedef struct RStdCCharSlice {
    const char *data;
    size_t length;
} RStdCCharSlice;

typedef enum RStdCStringErrorKind {
    R_STD_C_STRING_ERROR_MISSING_NUL = 0,
    R_STD_C_STRING_ERROR_EMBEDDED_NUL = 1,
    R_STD_C_STRING_ERROR_INVALID_UTF8 = 2,
    R_STD_C_STRING_ERROR_ALLOCATION_FAILED = 3
} RStdCStringErrorKind;

typedef struct RStdCStringError {
    RStdCStringErrorKind kind;
    size_t index;
    RStdAllocError allocation_error;
} RStdCStringError;

typedef enum RStdCCallStatus {
    R_STD_C_CALL_SUCCESS = 0,
    R_STD_C_CALL_ERROR = 1
} RStdCCallStatus;

typedef struct RStdCStringResult {
    RStdCCallStatus status;
    RStdCString value;
    RStdCStringError error;
} RStdCStringResult;

typedef struct RStdCValidateUtf8Result {
    RStdCCallStatus status;
    RStdStringView value;
    RStdCStringError error;
} RStdCValidateUtf8Result;

typedef struct RStdCCopyUtf8Result {
    RStdCCallStatus status;
    RStdString value;
    RStdCStringError error;
} RStdCCopyUtf8Result;

typedef RStdConvertCheckedResult RStdCCheckedResult;

/*
 * Type-erased implementation of std.c::checked_D. The destination tag must name one of the
 * closed C ABI numeric types; source may name any R or C numeric type.
 */
RStdCCheckedResult r_std_c_checked(RStdConvertNumericValue source,
                                   RStdConvertDestination destination);

RStdCStringResult r_std_c_string_from_str(RRuntimeAllocator *allocator, RStdStringView source);
RStdCCharSlice r_std_c_string_as_slice(const RStdCString *source);
const char *r_std_c_string_as_ptr(const RStdCString *source);
RStdCValidateUtf8Result r_std_c_validate_utf8(RStdCCharSlice storage);
RStdCCopyUtf8Result r_std_c_copy_utf8(RRuntimeAllocator *allocator, RStdCCharSlice storage);
RStdError r_std_c_string_as_error(RStdCStringError value);

static inline void r_std_c_string_destroy(RStdCString *string) {
    r_runtime_array_destroy(string);
}

static inline void r_std_c_string_move_initialize(RStdCString *destination, RStdCString *source) {
    *destination = *source;
    *source = (RStdCString){0};
}

typedef void (*RStdCHandleDestructor)(void *pointer);

typedef struct RStdCHandle {
    void *pointer;
    RStdCHandleDestructor destructor;
} RStdCHandle;

/* Unsafe adoption: the caller establishes the unique foreign ownership contract. */
RStdCHandle r_std_c_adopt_handle(void *pointer, RStdCHandleDestructor destructor);
void *r_std_c_handle_pointer(const RStdCHandle *handle);
void *r_std_c_release_handle(RStdCHandle *handle);

static inline void r_std_c_handle_move_initialize(RStdCHandle *destination, RStdCHandle *source) {
    *destination = *source;
    *source = (RStdCHandle){0};
}

static inline void r_std_c_handle_destroy(RStdCHandle *handle) {
    if (handle->pointer != NULL) {
        void *pointer = handle->pointer;
        RStdCHandleDestructor destructor = handle->destructor;
        RRuntimeCEnvironment environment;
        handle->pointer = NULL;
        handle->destructor = NULL;
        r_runtime_c_call_begin(&environment);
        destructor(pointer);
        r_runtime_c_call_end(&environment);
    }
}

typedef struct RStdCTargetInfo {
    uint32_t pointer_bits;
    _Bool c_wint_available;
    _Bool c_long_double_available;
    _Bool hosted_native_async;
} RStdCTargetInfo;

RStdCTargetInfo r_std_c_target(void);

typedef struct RStdCLinkManifestEntry {
    RStdStringView logical_name;
    _Bool available;
} RStdCLinkManifestEntry;

typedef struct RStdCLinkManifestView {
    const RStdCLinkManifestEntry *entries;
    size_t count;
} RStdCLinkManifestView;

/* The manifest view is an immutable hidden argument supplied by generated C17. */
_Bool r_std_c_link_available(RStdCLinkManifestView manifest, RStdStringView logical_name);

typedef enum RStdCRuntimeError {
    R_STD_C_RUNTIME_ERROR_RUNTIME_STOPPING = 0,
    R_STD_C_RUNTIME_ERROR_RESOURCE_EXHAUSTED = 1,
    R_STD_C_RUNTIME_ERROR_FLOATING_ENVIRONMENT_UNAVAILABLE = 2
} RStdCRuntimeError;

typedef RRuntimeThreadAttachment RStdCThreadAttachment;

typedef struct RStdCAttachThreadResult {
    RStdCCallStatus status;
    RStdCThreadAttachment value;
    RStdCRuntimeError error;
} RStdCAttachThreadResult;

RStdCAttachThreadResult r_std_c_attach_thread(void);
void r_std_c_detach_thread(RStdCThreadAttachment *attachment);
RStdError r_std_c_runtime_as_error(RStdCRuntimeError value);

static inline void r_std_c_thread_attachment_move_initialize(RStdCThreadAttachment *destination,
                                                             RStdCThreadAttachment *source) {
    *destination = *source;
    *source = (RStdCThreadAttachment){0};
}

static inline void r_std_c_thread_attachment_destroy(RStdCThreadAttachment *attachment) {
    if (attachment->active) {
        r_runtime_thread_detach(attachment);
    }
}

#endif

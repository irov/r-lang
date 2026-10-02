#ifndef R_STD_FS_H
#define R_STD_FS_H

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_alloc.h"
#include "r_std_async.h"
#include "r_std_error_types.h"
#include "r_std_io.h"
#include "r_std_string.h"
#include "r_std_time.h"

#include <stddef.h>
#include <stdint.h>

typedef struct RStdFsPathStorage RStdFsPathStorage;

/*
 * An opaque immutable Move-only owner. storage is implementation-private and shall be moved only
 * by compiler-generated move glue. A nonempty value owns exactly one allocation.
 */
typedef struct RStdFsPath {
    RStdFsPathStorage *storage;
} RStdFsPath;

typedef enum RStdFsPathErrorKind {
    R_STD_FS_PATH_ERROR_INVALID_UTF8 = 0,
    R_STD_FS_PATH_ERROR_EMBEDDED_NUL = 1,
    R_STD_FS_PATH_ERROR_NOT_REPRESENTABLE = 2,
    R_STD_FS_PATH_ERROR_ABSOLUTE_COMPONENT = 3,
    R_STD_FS_PATH_ERROR_ALLOCATION_FAILED = 4
} RStdFsPathErrorKind;

typedef struct RStdFsPathError {
    RStdFsPathErrorKind kind;
    size_t index;
    RStdAllocError allocation_error;
} RStdFsPathError;

typedef enum RStdFsErrorCode {
    R_STD_FS_ERROR_INVALID_PATH = 0,
    R_STD_FS_ERROR_INVALID_RELATIVE_PATH = 1,
    R_STD_FS_ERROR_INVALID_OPERATION = 2,
    R_STD_FS_ERROR_NOT_FOUND = 3,
    R_STD_FS_ERROR_ALREADY_EXISTS = 4,
    R_STD_FS_ERROR_NOT_DIRECTORY = 5,
    R_STD_FS_ERROR_IS_DIRECTORY = 6,
    R_STD_FS_ERROR_DIRECTORY_NOT_EMPTY = 7,
    R_STD_FS_ERROR_PERMISSION_DENIED = 8,
    R_STD_FS_ERROR_READ_ONLY = 9,
    R_STD_FS_ERROR_NAME_TOO_LONG = 10,
    R_STD_FS_ERROR_TOO_MANY_LINKS = 11,
    R_STD_FS_ERROR_NO_SPACE = 12,
    R_STD_FS_ERROR_FILE_TOO_LARGE = 13,
    R_STD_FS_ERROR_RESOURCE_EXHAUSTED = 14,
    R_STD_FS_ERROR_CANCELLED = 15,
    R_STD_FS_ERROR_TIMED_OUT = 16,
    R_STD_FS_ERROR_UNSUPPORTED = 17,
    R_STD_FS_ERROR_OTHER = 18,
    R_STD_FS_ERROR_CLOSED = 19
} RStdFsErrorCode;

typedef struct RStdFsError {
    RStdFsErrorCode code;
    int64_t native_code;
} RStdFsError;

typedef enum RStdFsAccess {
    R_STD_FS_ACCESS_READ = 0,
    R_STD_FS_ACCESS_WRITE = 1,
    R_STD_FS_ACCESS_READ_WRITE = 2
} RStdFsAccess;

typedef enum RStdFsCreateMode {
    R_STD_FS_CREATE_EXISTING = 0,
    R_STD_FS_CREATE_OPEN_OR_CREATE = 1,
    R_STD_FS_CREATE_NEW = 2
} RStdFsCreateMode;

typedef struct RStdFsOpenFileOptions {
    RStdFsAccess access;
    RStdFsCreateMode create;
    _Bool truncate;
    _Bool append;
    _Bool follow_final_symlink;
} RStdFsOpenFileOptions;

typedef enum RStdFsSeekOrigin {
    R_STD_FS_SEEK_ORIGIN_START = 0,
    R_STD_FS_SEEK_ORIGIN_CURRENT = 1,
    R_STD_FS_SEEK_ORIGIN_END = 2
} RStdFsSeekOrigin;

/* Library R-SLIB-FS-0016: the kind of an advisory lock. */
typedef enum RStdFsLockKind {
    R_STD_FS_LOCK_KIND_SHARED = 0,
    R_STD_FS_LOCK_KIND_EXCLUSIVE = 1
} RStdFsLockKind;

/* Library R-SLIB-FS-0014: the guarantee that std.fs::sync waits for. */
typedef enum RStdFsSyncLevel {
    R_STD_FS_SYNC_LEVEL_BARRIER = 0,
    R_STD_FS_SYNC_LEVEL_DEVICE = 1,
    R_STD_FS_SYNC_LEVEL_MEDIA = 2
} RStdFsSyncLevel;

typedef enum RStdFsFileKind {
    R_STD_FS_FILE_KIND_REGULAR = 0,
    R_STD_FS_FILE_KIND_DIRECTORY = 1,
    R_STD_FS_FILE_KIND_SYMLINK = 2,
    R_STD_FS_FILE_KIND_CHARACTER_DEVICE = 3,
    R_STD_FS_FILE_KIND_BLOCK_DEVICE = 4,
    R_STD_FS_FILE_KIND_FIFO = 5,
    R_STD_FS_FILE_KIND_SOCKET = 6,
    R_STD_FS_FILE_KIND_OTHER = 7
} RStdFsFileKind;

/* Canonical monomorphic C representation of o(std.time::system_time). */
typedef RStdTimeSystemTimeOption RStdFsSystemTimeOption;

typedef struct RStdFsMetadata {
    RStdFsFileKind kind;
    uint64_t size;
    RStdFsSystemTimeOption created;
    RStdFsSystemTimeOption modified;
    RStdFsSystemTimeOption accessed;
} RStdFsMetadata;

/*
 * Opaque Move-only handle shells. Their runtime-retained storage is constructed by the async open
 * bridge; no synchronous constructor or native-descriptor escape exists.
 */
typedef struct RStdFsDirectoryStorage RStdFsDirectoryStorage;
typedef struct RStdFsFileStorage RStdFsFileStorage;
typedef struct RStdFsDirectoryIterStorage RStdFsDirectoryIterStorage;

typedef struct RStdFsDirectory {
    RStdFsDirectoryStorage *storage;
} RStdFsDirectory;

typedef struct RStdFsFile {
    RStdFsFileStorage *storage;
} RStdFsFile;

typedef struct RStdFsDirectoryIter {
    RStdFsDirectoryIterStorage *storage;
} RStdFsDirectoryIter;

typedef struct RStdFsDirectoryEntry {
    RStdFsPath name;
    RStdFsFileKind kind;
} RStdFsDirectoryEntry;

typedef enum RStdFsDirectoryNextResultKind {
    R_STD_FS_DIRECTORY_NEXT_ENTRY = 0,
    R_STD_FS_DIRECTORY_NEXT_END = 1,
    R_STD_FS_DIRECTORY_NEXT_FAILED = 2
} RStdFsDirectoryNextResultKind;

typedef struct RStdFsDeadline {
    _Bool has_value;
    RStdTimeInstant value;
} RStdFsDeadline;

/* CONTRACT_VIOLATION is a compiler/runtime ABI state and is not an R error variant. */
typedef enum RStdFsCallStatus {
    R_STD_FS_CALL_SUCCESS = 0,
    R_STD_FS_CALL_ERROR = 1,
    R_STD_FS_CALL_CONTRACT_VIOLATION = 2
} RStdFsCallStatus;

typedef struct RStdFsPathResult {
    RStdFsCallStatus status;
    RStdFsPathError error;
    RStdFsPath value;
} RStdFsPathResult;

typedef struct RStdFsPathAllocResult {
    RStdFsCallStatus status;
    RStdAllocError error;
    RStdFsPath value;
} RStdFsPathAllocResult;

typedef struct RStdFsStringResult {
    RStdFsCallStatus status;
    RStdFsPathError error;
    RStdString value;
} RStdFsStringResult;

/* Canonical hidden checked carrier: r_tag is 0 for success and 1 for std.fs::fs_error. */
typedef struct RStdFsDirectoryResult {
    uint32_t r_tag;
    union {
        RStdFsDirectory r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsDirectoryResult;

typedef struct RStdFsFileResult {
    uint32_t r_tag;
    union {
        RStdFsFile r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsFileResult;

typedef struct RStdFsMetadataResult {
    uint32_t r_tag;
    union {
        RStdFsMetadata r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsMetadataResult;

/* Canonical hidden checked carrier for array(u8) throws std.fs::fs_error. */
typedef struct RStdFsArrayResult {
    uint32_t r_tag;
    union {
        RRuntimeArray r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsArrayResult;

typedef enum RStdFsWriteFileResultKind {
    R_STD_FS_WRITE_FILE_COMMITTED = 0,
    R_STD_FS_WRITE_FILE_FAILED = 1
} RStdFsWriteFileResultKind;

typedef struct RStdFsWriteFileResult {
    RStdFsWriteFileResultKind kind;
    RStdFsError error;
    RRuntimeArray data;
} RStdFsWriteFileResult;

/* Canonical hidden checked carrier for void throws std.fs::fs_error. */
typedef struct RStdFsVoidResult {
    uint32_t r_tag;
    union {
        RStdFsError r_err;
    } r_payload;
} RStdFsVoidResult;

typedef struct RStdFsU64Result {
    uint32_t r_tag;
    union {
        uint64_t r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsU64Result;

typedef struct RStdFsBoolResult {
    uint32_t r_tag;
    union {
        _Bool r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsBoolResult;

typedef struct RStdFsDirectoryIterResult {
    uint32_t r_tag;
    union {
        RStdFsDirectoryIter r_ok;
        RStdFsError r_err;
    } r_payload;
} RStdFsDirectoryIterResult;

typedef struct RStdFsDirectoryNextResult {
    RStdFsDirectoryNextResultKind kind;
    RStdFsError error;
    RStdFsDirectoryIter iterator;
    RStdFsDirectoryEntry entry;
} RStdFsDirectoryNextResult;

typedef struct RStdFsTaskStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdFsTaskStartResult;

/*
 * Borrowed byte views for the scoped operations. The caller keeps the storage alive and
 * unmoved until the completion result is published; the library never frees or retains it.
 */
typedef struct RStdFsMutableBytes {
    uint8_t *data;
    size_t length;
} RStdFsMutableBytes;

typedef struct RStdFsConstBytes {
    const uint8_t *data;
    size_t length;
} RStdFsConstBytes;

/*
 * Ownership: text is a valid UTF-8 call-bounded str borrow and is never retained. Success returns
 * the sole owner of an exact target-native copy. Validation completes before allocation.
 */
RStdFsPathResult r_std_fs_path_from_utf8(RRuntimeAllocator *allocator, RStdStringView text);

/*
 * Ownership: bytes is a call-bounded borrow and is never retained. UTF-8 and NUL validation
 * completes before allocation. Success returns the sole owner of an exact target-native copy.
 */
RStdFsPathResult r_std_fs_path_from_utf8_bytes(RRuntimeAllocator *allocator, RStdStringView bytes);

/*
 * Ownership: source is a shared call-bounded borrow and is never retained. Success returns a
 * distinct deep owner. Failure leaves source unchanged.
 */
RStdFsPathAllocResult r_std_fs_path_clone(const RStdFsPath *source);

/*
 * Ownership: source is a shared call-bounded borrow and is never retained. Success returns an
 * independently owned byte-for-byte UTF-8 string. Failure leaves source unchanged.
 */
RStdFsStringResult r_std_fs_path_to_utf8(const RStdFsPath *source);

/*
 * Ownership: base and component are shared call-bounded borrows and are never retained. Success
 * returns a distinct owner. Either failure leaves both operands unchanged.
 */
RStdFsPathResult r_std_fs_path_join(const RStdFsPath *base, const RStdFsPath *component);

/* Ownership: source is a shared call-bounded borrow and is never retained. */
_Bool r_std_fs_path_is_absolute(const RStdFsPath *source);

/* Exact non-allocating adaptation of std.fs::fs_error to std.error::error. */
RStdError r_std_fs_as_error(RStdFsError value);

/*
 * Ownership: path is copied before successful task publication. Completion returns one Move-only
 * runtime-retained directory owner or an fs_error. Start failure leaves path unchanged.
 */
RStdFsTaskStartResult r_std_fs_open_directory(const RStdFsPath *path, RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. The caller may move
 * or destroy both original values immediately after return.
 */
RStdFsTaskStartResult r_std_fs_open_directory_beneath(const RStdFsDirectory *root,
                                                      const RStdFsPath *relative,
                                                      RStdFsDeadline deadline);

/* Ownership: path and options are copied before successful task publication. */
RStdFsTaskStartResult
r_std_fs_open_file(const RStdFsPath *path, RStdFsOpenFileOptions options, RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative/options. No borrowed
 * argument is retained by the published task.
 */
RStdFsTaskStartResult r_std_fs_open_file_beneath(const RStdFsDirectory *root,
                                                 const RStdFsPath *relative,
                                                 RStdFsOpenFileOptions options,
                                                 RStdFsDeadline deadline);

/*
 * Ownership: path is copied before successful task publication. Recursive creation may retain
 * committed parent directories after a later failure, cancellation or deadline.
 */
RStdFsTaskStartResult
r_std_fs_create_directory(const RStdFsPath *path, _Bool recursive, RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. Private staging is
 * cleaned before any pre-commit terminal acknowledgement.
 */
RStdFsTaskStartResult r_std_fs_create_directory_beneath(const RStdFsDirectory *root,
                                                        const RStdFsPath *relative,
                                                        _Bool recursive,
                                                        RStdFsDeadline deadline);

/* Ownership: successful start duplicates the file descriptor before publication. */
RStdFsTaskStartResult r_std_fs_file_metadata(const RStdFsFile *file, RStdFsDeadline deadline);

/*
 * Ownership: path is copied before successful task publication. The final entry is inspected
 * without following a final symbolic link. Start failure leaves path unchanged.
 */
RStdFsTaskStartResult r_std_fs_metadata(const RStdFsPath *path, RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. A final symlink is
 * inspected as its own namespace entry and its target is never opened.
 */
RStdFsTaskStartResult r_std_fs_metadata_beneath(const RStdFsDirectory *root,
                                                const RStdFsPath *relative,
                                                RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. A final symlink is
 * removed as its own entry; its target remains unchanged.
 */
RStdFsTaskStartResult r_std_fs_remove_file_beneath(const RStdFsDirectory *root,
                                                   const RStdFsPath *relative,
                                                   RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. A final symlink is
 * a non-directory and is not followed.
 */
RStdFsTaskStartResult r_std_fs_remove_directory_beneath(const RStdFsDirectory *root,
                                                        const RStdFsPath *relative,
                                                        RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains both roots and copies both paths. A final
 * source symlink is moved as its own entry; neither endpoint follows symbolic links.
 */
RStdFsTaskStartResult r_std_fs_rename_beneath(const RStdFsDirectory *source_root,
                                              const RStdFsPath *source,
                                              const RStdFsDirectory *destination_root,
                                              const RStdFsPath *destination,
                                              RStdFsDeadline deadline);

/* Ownership: successful start independently retains file; failure leaves it unchanged. */
RStdFsTaskStartResult r_std_fs_seek(const RStdFsFile *file,
                                    RStdFsSeekOrigin origin,
                                    int64_t offset,
                                    RStdFsDeadline deadline);

/* Ownership: successful start independently retains a writable file identity. */
RStdFsTaskStartResult r_std_fs_flush(const RStdFsFile *file, RStdFsDeadline deadline);

/* Ownership: as flush; level selects the durability guarantee (R-SLIB-FS-0014). */
RStdFsTaskStartResult
r_std_fs_sync(const RStdFsFile *file, RStdFsSyncLevel level, RStdFsDeadline deadline);

/*
 * Advisory locks of the open file (R-SLIB-FS-0016) over length bytes from start, zero length
 * reaching past every end, in the order of the shared position. try_lock completes with false
 * while another open file holds a conflicting lock; lock retries until it sets the lock, the
 * deadline passes or the task is cancelled; unlock removes the locks of the file over the range.
 * Setting or removing a lock is the commit point.
 */
RStdFsTaskStartResult r_std_fs_try_lock(const RStdFsFile *file,
                                        RStdFsLockKind kind,
                                        uint64_t start,
                                        uint64_t length,
                                        RStdFsDeadline deadline);
RStdFsTaskStartResult r_std_fs_lock(const RStdFsFile *file,
                                    RStdFsLockKind kind,
                                    uint64_t start,
                                    uint64_t length,
                                    RStdFsDeadline deadline);
RStdFsTaskStartResult
r_std_fs_unlock(const RStdFsFile *file, uint64_t start, uint64_t length, RStdFsDeadline deadline);

/*
 * Ownership: success consumes buffer into the eager task and every terminal std.io outcome returns
 * that same owner. Start failure leaves the complete array descriptor and all bytes unchanged.
 */
RStdFsTaskStartResult
r_std_fs_read(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);

/*
 * Ownership: path is copied before successful task publication. Success owns an exact array(u8)
 * containing the file bytes. Every error releases any partially accumulated byte owner.
 */
RStdFsTaskStartResult
r_std_fs_read_file(const RStdFsPath *path, size_t limit, RStdFsDeadline deadline);

/*
 * Ownership: successful start independently retains root and copies relative. Success owns an
 * exact array(u8); every error releases partial bytes. A final symbolic link is never followed.
 */
RStdFsTaskStartResult r_std_fs_read_file_beneath(const RStdFsDirectory *root,
                                                 const RStdFsPath *relative,
                                                 size_t limit,
                                                 RStdFsDeadline deadline);

/*
 * Ownership: successful task start consumes data. Both committed and failed completion return the
 * same array owner with byte-identical contents. Start failure leaves data unchanged.
 */
RStdFsTaskStartResult r_std_fs_write_file_atomic_no_replace(const RStdFsPath *path,
                                                            RRuntimeArray *data,
                                                            RStdFsDeadline deadline);

/*
 * Ownership: successful task start independently retains root, copies relative and consumes data.
 * Both completion variants return the original byte owner. Start failure changes no operand.
 */
RStdFsTaskStartResult r_std_fs_write_file_atomic_no_replace_beneath(const RStdFsDirectory *root,
                                                                    const RStdFsPath *relative,
                                                                    RRuntimeArray *data,
                                                                    RStdFsDeadline deadline);
RStdFsTaskStartResult
r_std_fs_write(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_std_fs_write_all(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);

/*
 * Scoped forms borrow caller storage instead of consuming an owner. target/source stay borrowed,
 * unmoved and unfreed until the completion result is published (after native acknowledgement);
 * start failure leaves them untouched. read_into completes with RStdIoCountResult carrying the
 * bytes placed at the start of target (0 = end of file or empty target). write_from carries one
 * positive prefix count; write_all_from completes with RStdIoVoidResult only after every byte or
 * a terminal error. Zero-length views complete successfully without native submission. Failed
 * outcomes carry only the std.io error and never report partial progress.
 */
RStdFsTaskStartResult
r_std_fs_read_into(const RStdFsFile *file, RStdFsMutableBytes target, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_std_fs_write_from(const RStdFsFile *file, RStdFsConstBytes source, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_std_fs_write_all_from(const RStdFsFile *file, RStdFsConstBytes source, RStdFsDeadline deadline);

/*
 * Positional forms (R-SLIB-FS-0015): the same ownership, borrowing and outcomes as read, write_all,
 * read_into and write_all_from, at offset instead of the shared position, which they take their
 * turn in but never change. An offset above INT64_MAX and a write to an append-mode file complete
 * with invalid_operation without native submission.
 */
RStdFsTaskStartResult r_std_fs_read_at(const RStdFsFile *file,
                                       uint64_t offset,
                                       RRuntimeArray *buffer,
                                       RStdFsDeadline deadline);
RStdFsTaskStartResult r_std_fs_write_all_at(const RStdFsFile *file,
                                            uint64_t offset,
                                            RRuntimeArray *buffer,
                                            RStdFsDeadline deadline);
RStdFsTaskStartResult r_std_fs_read_at_into(const RStdFsFile *file,
                                            uint64_t offset,
                                            RStdFsMutableBytes target,
                                            RStdFsDeadline deadline);
RStdFsTaskStartResult r_std_fs_write_all_at_from(const RStdFsFile *file,
                                                 uint64_t offset,
                                                 RStdFsConstBytes source,
                                                 RStdFsDeadline deadline);

/*
 * Ownership: successful task start consumes the named file view. Start failure leaves it
 * unchanged. Completion cancels and drains its earlier in-flight operations before releasing the
 * view exactly once.
 */
RStdFsTaskStartResult r_std_fs_close_file(RStdFsFile *file, RStdFsDeadline deadline);

/*
 * Ownership: successful task start consumes the named directory view. Start failure leaves it
 * unchanged. Completion cancels and drains its earlier in-flight operations before releasing the
 * view exactly once.
 */
RStdFsTaskStartResult r_std_fs_close_directory(RStdFsDirectory *directory, RStdFsDeadline deadline);

/* Ownership: successful start independently retains directory and creates a distinct cursor. */
RStdFsTaskStartResult r_std_fs_iterate(const RStdFsDirectory *directory, RStdFsDeadline deadline);

/*
 * Ownership: successful task start consumes iterator and every non-end result returns that same
 * owner. Start failure leaves iterator unchanged. End consumes and releases the owner.
 */
RStdFsTaskStartResult r_std_fs_next(RStdFsDirectoryIter *iterator, RStdFsDeadline deadline);

/* Private compiler drop/move ABI used by the inline typed glue below. */
void r_library_internal_fs_directory_move(RStdFsDirectory *destination, RStdFsDirectory *source);
void r_library_internal_fs_directory_drop(RStdFsDirectory *directory);
void r_library_internal_fs_file_move(RStdFsFile *destination, RStdFsFile *source);
void r_library_internal_fs_file_drop(RStdFsFile *file);
void r_library_internal_fs_directory_iter_move(RStdFsDirectoryIter *destination,
                                               RStdFsDirectoryIter *source);
void r_library_internal_fs_directory_iter_drop(RStdFsDirectoryIter *iterator);
void r_library_internal_fs_directory_entry_move(RStdFsDirectoryEntry *destination,
                                                RStdFsDirectoryEntry *source);
void r_library_internal_fs_directory_entry_drop(RStdFsDirectoryEntry *entry);
void r_library_internal_fs_directory_next_result_move(RStdFsDirectoryNextResult *destination,
                                                      RStdFsDirectoryNextResult *source);
void r_library_internal_fs_directory_next_result_drop(RStdFsDirectoryNextResult *result);
void r_library_internal_fs_write_file_result_move(RStdFsWriteFileResult *destination,
                                                  RStdFsWriteFileResult *source);
void r_library_internal_fs_write_file_result_drop(RStdFsWriteFileResult *result);

/* Compiler drop glue: consumes the path and leaves a valid empty ABI shell. */
static inline void r_std_fs_path_destroy(RStdFsPath *path) {
    if ((path != NULL) && (path->storage != NULL)) {
        r_runtime_allocator_deallocate(path->storage, _Alignof(max_align_t));
        path->storage = NULL;
    }
}

static inline void r_std_fs_path_move_initialize(RStdFsPath *destination, RStdFsPath *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

static inline void r_std_fs_directory_destroy(RStdFsDirectory *directory) {
    r_library_internal_fs_directory_drop(directory);
}

static inline void r_std_fs_directory_move_initialize(RStdFsDirectory *destination,
                                                      RStdFsDirectory *source) {
    r_library_internal_fs_directory_move(destination, source);
}

static inline void r_std_fs_file_destroy(RStdFsFile *file) {
    r_library_internal_fs_file_drop(file);
}

static inline void r_std_fs_file_move_initialize(RStdFsFile *destination, RStdFsFile *source) {
    r_library_internal_fs_file_move(destination, source);
}

static inline void r_std_fs_directory_iter_destroy(RStdFsDirectoryIter *iterator) {
    r_library_internal_fs_directory_iter_drop(iterator);
}

static inline void r_std_fs_directory_iter_move_initialize(RStdFsDirectoryIter *destination,
                                                           RStdFsDirectoryIter *source) {
    r_library_internal_fs_directory_iter_move(destination, source);
}

static inline void r_std_fs_directory_entry_destroy(RStdFsDirectoryEntry *entry) {
    r_library_internal_fs_directory_entry_drop(entry);
}

static inline void r_std_fs_directory_entry_move_initialize(RStdFsDirectoryEntry *destination,
                                                            RStdFsDirectoryEntry *source) {
    r_library_internal_fs_directory_entry_move(destination, source);
}

static inline void r_std_fs_directory_next_result_destroy(RStdFsDirectoryNextResult *result) {
    r_library_internal_fs_directory_next_result_drop(result);
}

static inline void
r_std_fs_directory_next_result_move_initialize(RStdFsDirectoryNextResult *destination,
                                               RStdFsDirectoryNextResult *source) {
    r_library_internal_fs_directory_next_result_move(destination, source);
}

static inline void r_std_fs_write_file_result_destroy(RStdFsWriteFileResult *result) {
    r_library_internal_fs_write_file_result_drop(result);
}

static inline void r_std_fs_write_file_result_move_initialize(RStdFsWriteFileResult *destination,
                                                              RStdFsWriteFileResult *source) {
    r_library_internal_fs_write_file_result_move(destination, source);
}

#endif

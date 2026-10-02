#ifndef R_STD_ENV_H
#define R_STD_ENV_H

#include "r_std_array.h"
#include "r_std_dict.h"
#include "r_std_error_types.h"
#include "r_std_string.h"

#include <stdint.h>

typedef enum RStdEnvErrorCode {
    R_STD_ENV_ERROR_INVALID_NAME = 0,
    R_STD_ENV_ERROR_INVALID_VALUE = 1,
    R_STD_ENV_ERROR_ALLOCATION_FAILED = 2,
    R_STD_ENV_ERROR_PERMISSION_DENIED = 3,
    R_STD_ENV_ERROR_RESOURCE_EXHAUSTED = 4,
    R_STD_ENV_ERROR_OTHER = 5
} RStdEnvErrorCode;

typedef struct RStdEnvError {
    RStdEnvErrorCode code;
    int64_t native_code;
} RStdEnvError;

typedef enum RStdEnvCallStatus {
    R_STD_ENV_CALL_SUCCESS = 0,
    R_STD_ENV_CALL_ERROR = 1,
    R_STD_ENV_CALL_CONTRACT_VIOLATION = 2
} RStdEnvCallStatus;

typedef struct RStdEnvArgumentsResult {
    RStdEnvCallStatus status;
    RStdArray value;
    RStdEnvError error;
} RStdEnvArgumentsResult;

typedef struct RStdEnvVariablesResult {
    RStdEnvCallStatus status;
    RStdDict value;
    RStdEnvError error;
} RStdEnvVariablesResult;

typedef struct RStdEnvGetResult {
    RStdEnvCallStatus status;
    _Bool has_value;
    RStdString value;
    RStdEnvError error;
} RStdEnvGetResult;

typedef struct RStdEnvVoidResult {
    RStdEnvCallStatus status;
    RStdEnvError error;
} RStdEnvVoidResult;

/*
 * Ownership: allocator is a shared runtime reference retained by the returned array and strings.
 * Success owns a fresh immutable copy of the complete startup argument snapshot. Failure exposes
 * no partial array.
 */
RStdEnvArgumentsResult r_std_env_arguments(RRuntimeAllocator *allocator);

/*
 * Ownership: allocator is retained by the returned dictionary and strings. seed is hidden
 * compiler/runtime metadata for this dictionary identity. Success owns an all-or-error snapshot.
 */
RStdEnvVariablesResult r_std_env_variables(RRuntimeAllocator *allocator, uint64_t seed);

/*
 * Ownership: name is a call-bounded UTF-8 borrow. A present result owns an independent string;
 * absence owns no string. Validation and allocation complete before the environment view escapes.
 */
RStdEnvGetResult r_std_env_get(RRuntimeAllocator *allocator, RStdStringView name);

/*
 * Ownership: name and value are call-bounded UTF-8 borrows. Native copies are complete before the
 * transactional mutation. Neither input is retained.
 */
RStdEnvVoidResult
r_std_env_set(RRuntimeAllocator *allocator, RStdStringView name, RStdStringView value);

/* Ownership: name is a call-bounded UTF-8 borrow and is never retained. */
RStdEnvVoidResult r_std_env_remove(RRuntimeAllocator *allocator, RStdStringView name);

/* Copies the typed error into the portable environment domain without allocation. */
RStdError r_std_env_as_error(RStdEnvError value);

#endif

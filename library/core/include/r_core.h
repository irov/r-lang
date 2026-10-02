#ifndef R_CORE_H
#define R_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef enum RCoreMemoryOrder {
    R_CORE_MEMORY_ORDER_RELAXED = 0,
    R_CORE_MEMORY_ORDER_ACQUIRE = 1,
    R_CORE_MEMORY_ORDER_RELEASE = 2,
    R_CORE_MEMORY_ORDER_ACQ_REL = 3,
    R_CORE_MEMORY_ORDER_SEQ_CST = 4
} RCoreMemoryOrder;

typedef struct RCoreByteSlice {
    const uint8_t *data;
    size_t length;
} RCoreByteSlice;

typedef struct RCoreStringView {
    const uint8_t *data;
    size_t length;
} RCoreStringView;

typedef struct RCoreUtf8Error {
    size_t index;
} RCoreUtf8Error;

/* Core R-FUNC-0026: the refusal of a call of a function with @recursion(depth = N). */
typedef struct RCoreRecursionError {
    size_t depth;
} RCoreRecursionError;

typedef struct RCoreValidateUtf8Result {
    _Bool is_ok;
    RCoreStringView value;
    RCoreUtf8Error error;
} RCoreValidateUtf8Result;

/*
 * Ownership: source is a shared call-bounded borrow. Success returns a view with the same borrow
 * origin; failure reports the first invalid byte. No allocation or mutation occurs.
 */
RCoreValidateUtf8Result r_core_validate_utf8(RCoreByteSlice source);

#endif

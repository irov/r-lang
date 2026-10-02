#ifndef R_STD_UTF8_H
#define R_STD_UTF8_H

#include "r_core.h"

#include <stddef.h>
#include <stdint.h>

/* A zero-length view may carry a null data pointer. The view never owns its storage. */
typedef struct RStdUtf8View {
    const uint8_t *data;
    size_t length;
} RStdUtf8View;

/*
 * Ownership: source is a shared call-bounded borrow. Success returns a string view rooted in the
 * same borrow; failure reports the first invalid byte. No allocation or mutation occurs.
 */
RCoreValidateUtf8Result r_std_utf8_validate(RStdUtf8View source);

/* Ownership: source is a shared call-bounded borrow. No allocation or mutation occurs. */
_Bool r_std_utf8_is_valid(RStdUtf8View source);

#endif

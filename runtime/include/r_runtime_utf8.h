#ifndef R_RUNTIME_UTF8_H
#define R_RUNTIME_UTF8_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

_Bool r_runtime_utf8_validate(const uint8_t *bytes, size_t length, size_t *invalid_index);
_Bool r_runtime_utf8_is_scalar_boundary(const uint8_t *bytes, size_t length, size_t index);
_Bool r_runtime_utf8_encode(uint32_t scalar, uint8_t output[4], size_t *length);

#ifdef __cplusplus
}
#endif

#endif

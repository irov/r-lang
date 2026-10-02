#ifndef R_STD_FORMAT_H
#define R_STD_FORMAT_H

#include "r_std_convert.h"
#include "r_std_string.h"

#include <stdint.h>

typedef struct RStdFormatBuilder {
    RStdString output;
} RStdFormatBuilder;

typedef enum RStdFormatErrorKind {
    R_STD_FORMAT_ERROR_INVALID_RADIX = 0,
    R_STD_FORMAT_ERROR_ALLOCATION_FAILED = 1
} RStdFormatErrorKind;

typedef struct RStdFormatError {
    RStdFormatErrorKind kind;
    RStdAllocError allocation_error;
} RStdFormatError;

typedef enum RStdFormatCallStatus {
    R_STD_FORMAT_CALL_SUCCESS = 0,
    R_STD_FORMAT_CALL_ERROR = 1,
    R_STD_FORMAT_CALL_CONTRACT_VIOLATION = 2
} RStdFormatCallStatus;

typedef struct RStdFormatBuilderResult {
    RStdFormatCallStatus status;
    RStdAllocError error;
    RStdFormatBuilder value;
} RStdFormatBuilderResult;

typedef struct RStdFormatAllocResult {
    RStdFormatCallStatus status;
    RStdAllocError error;
} RStdFormatAllocResult;

typedef struct RStdFormatAppendResult {
    RStdFormatCallStatus status;
    RStdFormatError error;
} RStdFormatAppendResult;

RStdFormatBuilderResult r_std_format_create(RRuntimeAllocator *allocator);
RStdFormatBuilderResult r_std_format_with_capacity(RRuntimeAllocator *allocator, size_t capacity);
RStdStringView r_std_format_as_str(const RStdFormatBuilder *source);
void r_std_format_clear(RStdFormatBuilder *target);

/* Ownership: source is consumed and reset; the returned string owns the identical allocation. */
RStdString r_std_format_finish(RStdFormatBuilder *source);

/* target is exclusive; value is a shared call-bounded UTF-8 borrow and is not retained. */
RStdFormatAllocResult r_std_format_append_str(RStdFormatBuilder *target, RStdStringView value);

/* value is one Unicode scalar supplied by the compiler's char lowering. */
RStdFormatAllocResult r_std_format_append_char(RStdFormatBuilder *target, uint32_t value);

/* Type-erased implementation of the closed append_SUFFIX family. */
RStdFormatAppendResult r_std_format_append_suffix(RStdFormatBuilder *target,
                                                  RStdConvertParsedInteger value,
                                                  uint32_t radix);

/*
 * Ownership: target is an exclusive call-bounded borrow. Each operation appends the canonical
 * locale-independent round-trip representation. Failure preserves target exactly.
 */
RStdFormatAppendResult r_std_format_append_f32(RStdFormatBuilder *target, float value);
RStdFormatAppendResult r_std_format_append_f64(RStdFormatBuilder *target, double value);
RStdFormatAppendResult r_std_format_append_c_float(RStdFormatBuilder *target, float value);
RStdFormatAppendResult r_std_format_append_c_double(RStdFormatBuilder *target, double value);
RStdFormatAppendResult r_std_format_append_c_long_double(RStdFormatBuilder *target,
                                                         long double value);

/* Typed compiler ABI for literal format specifications. No template text is parsed at runtime. */
typedef struct RStdFormatSpec {
    size_t width;
    size_t precision;
    uint32_t radix;
    _Bool uppercase;
    _Bool zero_fill;
    _Bool fixed;
} RStdFormatSpec;

RStdFormatAllocResult r_library_internal_format_integer(RStdFormatBuilder *target,
                                                        RStdConvertParsedInteger value,
                                                        RStdFormatSpec spec);
RStdFormatAllocResult r_library_internal_format_float(RStdFormatBuilder *target,
                                                      long double value,
                                                      uint32_t source_kind,
                                                      RStdFormatSpec spec);

RStdFormatAllocResult r_library_internal_format_float32(RStdFormatBuilder *target,
                                                        float value,
                                                        uint32_t source_kind,
                                                        RStdFormatSpec spec);

/* R-EXPR-0028 (L32): text and one Unicode scalar padded on the left with spaces to spec.width
 * Unicode scalar values. */
RStdFormatAllocResult r_library_internal_format_text(RStdFormatBuilder *target,
                                                     RStdStringView text,
                                                     RStdFormatSpec spec);
RStdFormatAllocResult r_library_internal_format_char(RStdFormatBuilder *target,
                                                     uint32_t value,
                                                     RStdFormatSpec spec);

static inline void r_std_format_builder_destroy(RStdFormatBuilder *builder) {
    r_std_string_destroy(&builder->output);
}

static inline void r_std_format_builder_move_initialize(RStdFormatBuilder *destination,
                                                        RStdFormatBuilder *source) {
    r_std_string_move_initialize(&destination->output, &source->output);
}

#endif

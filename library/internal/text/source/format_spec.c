#include "r_std_format.h"

#include "r_library_float_format_internal.h"
#include "r_library_string_internal.h"

#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <xlocale.h>

#pragma STDC FENV_ACCESS ON

static RStdFormatAllocResult r_format_size_overflow(void) {
    RStdFormatAllocResult result = {0};
    result.status = R_STD_FORMAT_CALL_ERROR;
    result.error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
    return result;
}

/* Reserve once before writing, including arbitrarily large padding or precision. */
static RStdFormatAllocResult r_format_append_padded(RStdFormatBuilder *target,
                                                    const uint8_t *text,
                                                    size_t length,
                                                    size_t trailing_zeros,
                                                    RStdFormatSpec spec,
                                                    _Bool special) {
    RStdFormatAllocResult result = {0};
    RStdStringAllocResult reserved;
    size_t total;
    size_t padding;
    size_t sign;
    uint8_t *output;
    if (length > SIZE_MAX - trailing_zeros) {
        return r_format_size_overflow();
    }
    total = length + trailing_zeros;
    padding = spec.width > total ? spec.width - total : 0U;
    if (total > SIZE_MAX - padding) {
        return r_format_size_overflow();
    }
    total += padding;
    reserved.status = r_library_internal_string_map_allocation_status(
        r_runtime_string_reserve(&target->output, total), &reserved.error);
    result.status = (RStdFormatCallStatus)reserved.status;
    result.error = reserved.error;
    if (result.status != R_STD_FORMAT_CALL_SUCCESS) {
        return result;
    }
    if (total == 0U) {
        return result;
    }
    output = (uint8_t *)target->output.bytes.data + target->output.bytes.length;
    sign = spec.zero_fill && !special && length != 0U && text[0] == (uint8_t)'-' ? 1U : 0U;
    if (sign != 0U) {
        *output++ = (uint8_t)'-';
    }
    if (padding != 0U) {
        (void)memset(output, spec.zero_fill && !special ? '0' : ' ', padding);
        output += padding;
    }
    (void)memcpy(output, text + sign, length - sign);
    output += length - sign;
    if (trailing_zeros != 0U) {
        (void)memset(output, '0', trailing_zeros);
    }
    target->output.bytes.length += total;
    return result;
}

RStdFormatAllocResult r_library_internal_format_integer(RStdFormatBuilder *target,
                                                        RStdConvertParsedInteger value,
                                                        RStdFormatSpec spec) {
    uint8_t text[65];
    size_t start = sizeof(text);
    uint64_t magnitude = value.magnitude;
    const char *digits = spec.uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (spec.radix != 2U && spec.radix != 8U && spec.radix != 10U && spec.radix != 16U) {
        RStdFormatAllocResult invalid = {0};
        invalid.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        return invalid;
    }
    if (spec.radix == 10U) {
        /* Constant divisor: the compiler strength-reduces the decimal case. */
        do {
            text[--start] = (uint8_t)('0' + (magnitude % 10U));
            magnitude /= 10U;
        } while (magnitude != 0U);
    } else {
        do {
            text[--start] = (uint8_t)digits[magnitude % spec.radix];
            magnitude /= spec.radix;
        } while (magnitude != 0U);
    }
    if (value.negative) {
        text[--start] = (uint8_t)'-';
    }
    return r_format_append_padded(target, text + start, sizeof(text) - start, 0U, spec, 0);
}

static RStdFormatAllocResult r_format_float_impl(RStdFormatBuilder *target,
                                                 long double value,
                                                 uint32_t source_kind,
                                                 RStdFormatSpec spec) {
    RStdFormatAllocResult result = {0};
    char text[1536];
    int length;
    if (isnan(value)) {
        return r_format_append_padded(target, (const uint8_t *)"nan", 3U, 0U, spec, 1);
    }
    if (isinf(value)) {
        const char *spelling = signbit(value) ? "-inf" : "inf";
        return r_format_append_padded(
            target, (const uint8_t *)spelling, strlen(spelling), 0U, spec, 1);
    }
    if (!spec.fixed) {
        RStdFormatBuilder temporary = {0};
        RStdFormatAppendResult appended;
        RStdStringView view;
        RLibraryFloatFormatValue input = {0};
        (void)r_runtime_string_initialize(&temporary.output, target->output.bytes.allocator);
        if (source_kind == 0U) {
            input.f32 = (float)value;
        } else if (source_kind == 1U) {
            input.f64 = (double)value;
        } else if (source_kind == 2U) {
            input.c_float = (float)value;
        } else if (source_kind == 3U) {
            input.c_double = (double)value;
        } else {
            input.c_long_double = value;
        }
        appended = r_library_internal_float_append(
            &temporary, input, (RLibraryFloatFormatDestination)source_kind);
        result.status = appended.status;
        result.error = appended.error.allocation_error;
        if (appended.status == R_STD_FORMAT_CALL_SUCCESS) {
            view = (RStdStringView){temporary.output.bytes.data, temporary.output.bytes.length};
            result = r_format_append_padded(target, view.data, view.length, 0U, spec, 0);
        }
        r_runtime_string_destroy(&temporary.output);
        return result;
    }
    /* Every target floating value is exact at 1074 fractional decimal digits. */
    length = snprintf_l(text,
                        sizeof(text),
                        LC_C_LOCALE,
                        "%.*Lf",
                        (int)(spec.precision > 1074U ? 1074U : spec.precision),
                        value);
    if (length <= 0 || (size_t)length >= sizeof(text)) {
        result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
    } else {
        result = r_format_append_padded(target,
                                        (const uint8_t *)text,
                                        (size_t)length,
                                        spec.precision > 1074U ? spec.precision - 1074U : 0U,
                                        spec,
                                        0);
    }
    return result;
}

RStdFormatAllocResult r_library_internal_format_float(RStdFormatBuilder *target,
                                                      long double value,
                                                      uint32_t source_kind,
                                                      RStdFormatSpec spec) {
    const int saved_errno = errno;
    fenv_t environment;
    RStdFormatAllocResult result = {.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION};
    if (feholdexcept(&environment) == 0) {
        if (fesetround(FE_TONEAREST) == 0) {
            result = r_format_float_impl(target, value, source_kind, spec);
        }
        if (fesetenv(&environment) != 0) {
            result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        }
    }
    errno = saved_errno;
    return result;
}

/* Hold the environment before widening a potentially signaling binary32 NaN. */
RStdFormatAllocResult r_library_internal_format_float32(RStdFormatBuilder *target,
                                                        float value,
                                                        uint32_t source_kind,
                                                        RStdFormatSpec spec) {
    const int saved_errno = errno;
    fenv_t environment;
    RStdFormatAllocResult result = {.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION};
    if (feholdexcept(&environment) == 0) {
        result = r_library_internal_format_float(target, (long double)value, source_kind, spec);
        if (fesetenv(&environment) != 0) {
            result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        }
    }
    errno = saved_errno;
    return result;
}

/* R-EXPR-0028 (L32): text padded on the left with spaces to `spec.width` Unicode scalar values;
 * the rest of the specification does not apply to text. */
RStdFormatAllocResult r_library_internal_format_text(RStdFormatBuilder *target,
                                                     RStdStringView text,
                                                     RStdFormatSpec spec) {
    RStdFormatAllocResult result = {0};
    size_t scalars = 0U;
    size_t padding;
    uint8_t *output;
    for (size_t index = 0U; index < text.length; ++index) {
        if ((text.data[index] & 0xc0U) != 0x80U) {
            ++scalars;
        }
    }
    padding = spec.width > scalars ? spec.width - scalars : 0U;
    if (text.length > SIZE_MAX - padding) {
        return r_format_size_overflow();
    }
    result.status = (RStdFormatCallStatus)r_library_internal_string_map_allocation_status(
        r_runtime_string_reserve(&target->output, text.length + padding), &result.error);
    if (result.status != R_STD_FORMAT_CALL_SUCCESS || text.length + padding == 0U) {
        return result;
    }
    output = (uint8_t *)target->output.bytes.data + target->output.bytes.length;
    if (padding != 0U) {
        (void)memset(output, ' ', padding);
    }
    if (text.length != 0U) {
        (void)memcpy(output + padding, text.data, text.length);
    }
    target->output.bytes.length += text.length + padding;
    return result;
}

/* One Unicode scalar supplied by the compiler's char lowering, padded like text. */
RStdFormatAllocResult r_library_internal_format_char(RStdFormatBuilder *target,
                                                     uint32_t value,
                                                     RStdFormatSpec spec) {
    uint8_t text[4];
    size_t length;
    if (value < 0x80U) {
        text[0] = (uint8_t)value;
        length = 1U;
    } else if (value < 0x800U) {
        text[0] = (uint8_t)(0xc0U | (value >> 6U));
        text[1] = (uint8_t)(0x80U | (value & 0x3fU));
        length = 2U;
    } else if (value < 0x10000U) {
        text[0] = (uint8_t)(0xe0U | (value >> 12U));
        text[1] = (uint8_t)(0x80U | ((value >> 6U) & 0x3fU));
        text[2] = (uint8_t)(0x80U | (value & 0x3fU));
        length = 3U;
    } else {
        text[0] = (uint8_t)(0xf0U | (value >> 18U));
        text[1] = (uint8_t)(0x80U | ((value >> 12U) & 0x3fU));
        text[2] = (uint8_t)(0x80U | ((value >> 6U) & 0x3fU));
        text[3] = (uint8_t)(0x80U | (value & 0x3fU));
        length = 4U;
    }
    return r_library_internal_format_text(target, (RStdStringView){text, length}, spec);
}

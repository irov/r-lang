#include "r_std_convert.h"

#include "r_runtime_target_abi.h"

typedef enum RStdConvertStorageKind {
    R_STD_CONVERT_STORAGE_SIGNED_INTEGER = 0,
    R_STD_CONVERT_STORAGE_UNSIGNED_INTEGER = 1,
    R_STD_CONVERT_STORAGE_BINARY32 = 2,
    R_STD_CONVERT_STORAGE_BINARY64 = 3
} RStdConvertStorageKind;

typedef struct RStdConvertTypeDescriptor {
    RStdConvertStorageKind storage;
    uint64_t max_positive;
    uint64_t max_negative_magnitude;
    uint64_t max_finite_significand;
    int32_t max_finite_exponent;
    int32_t minimum_normal_exponent;
    int32_t minimum_subnormal_exponent;
    uint32_t precision;
} RStdConvertTypeDescriptor;

typedef enum RStdConvertDyadicClass {
    R_STD_CONVERT_DYADIC_FINITE = 0,
    R_STD_CONVERT_DYADIC_INFINITY = 1,
    R_STD_CONVERT_DYADIC_NAN = 2
} RStdConvertDyadicClass;

typedef struct RStdConvertDyadic {
    RStdConvertDyadicClass value_class;
    _Bool negative;
    uint64_t significand;
    int32_t exponent;
} RStdConvertDyadic;

static RStdConvertTypeDescriptor r_std_convert_signed_descriptor(uint64_t max_positive,
                                                                 uint64_t max_negative) {
    RStdConvertTypeDescriptor descriptor = {0};

    descriptor.storage = R_STD_CONVERT_STORAGE_SIGNED_INTEGER;
    descriptor.max_positive = max_positive;
    descriptor.max_negative_magnitude = max_negative;
    return descriptor;
}

static RStdConvertTypeDescriptor r_std_convert_unsigned_descriptor(uint64_t maximum) {
    RStdConvertTypeDescriptor descriptor = {0};

    descriptor.storage = R_STD_CONVERT_STORAGE_UNSIGNED_INTEGER;
    descriptor.max_positive = maximum;
    return descriptor;
}

static RStdConvertTypeDescriptor r_std_convert_binary32_descriptor(void) {
    RStdConvertTypeDescriptor descriptor = {0};

    descriptor.storage = R_STD_CONVERT_STORAGE_BINARY32;
    descriptor.max_finite_significand = UINT64_C(0x00ffffff);
    descriptor.max_finite_exponent = 104;
    descriptor.minimum_normal_exponent = -126;
    descriptor.minimum_subnormal_exponent = -149;
    descriptor.precision = 24U;
    return descriptor;
}

static RStdConvertTypeDescriptor r_std_convert_binary64_descriptor(void) {
    RStdConvertTypeDescriptor descriptor = {0};

    descriptor.storage = R_STD_CONVERT_STORAGE_BINARY64;
    descriptor.max_finite_significand = UINT64_C(0x001fffffffffffff);
    descriptor.max_finite_exponent = 971;
    descriptor.minimum_normal_exponent = -1022;
    descriptor.minimum_subnormal_exponent = -1074;
    descriptor.precision = 53U;
    return descriptor;
}

static _Bool r_std_convert_type_descriptor(RStdConvertNumericType type,
                                           RStdConvertTypeDescriptor *descriptor) {
    switch (type) {
    case R_STD_CONVERT_NUMERIC_TYPE_I8:
        *descriptor = r_std_convert_signed_descriptor(UINT64_C(127), UINT64_C(128));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_U8:
        *descriptor = r_std_convert_unsigned_descriptor(UINT64_C(255));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_I16:
        *descriptor = r_std_convert_signed_descriptor(UINT64_C(32767), UINT64_C(32768));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_U16:
        *descriptor = r_std_convert_unsigned_descriptor(UINT64_C(65535));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_I32:
        *descriptor = r_std_convert_signed_descriptor(UINT64_C(2147483647), UINT64_C(2147483648));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_U32:
        *descriptor = r_std_convert_unsigned_descriptor(UINT64_C(4294967295));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_I64:
    case R_STD_CONVERT_NUMERIC_TYPE_ISIZE:
        *descriptor = r_std_convert_signed_descriptor(UINT64_C(9223372036854775807),
                                                      UINT64_C(9223372036854775808));
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_U64:
    case R_STD_CONVERT_NUMERIC_TYPE_USIZE:
        *descriptor = r_std_convert_unsigned_descriptor(UINT64_MAX);
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_F32:
        *descriptor = r_std_convert_binary32_descriptor();
        return 1;
    case R_STD_CONVERT_NUMERIC_TYPE_F64:
        *descriptor = r_std_convert_binary64_descriptor();
        return 1;
#include "target_descriptors.generated.inc"
    }
    return 0;
}

static uint64_t r_std_convert_signed_magnitude(int64_t value) {
    if (value >= 0) {
        return (uint64_t)value;
    }
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static uint32_t r_std_convert_bit_width(uint64_t value) {
    uint32_t width = 0U;

    while (value != UINT64_C(0)) {
        value >>= 1U;
        width += 1U;
    }
    return width;
}

static RStdConvertDyadic r_std_convert_normalize_dyadic(RStdConvertDyadic value) {
    if (value.value_class != R_STD_CONVERT_DYADIC_FINITE || value.significand == UINT64_C(0)) {
        return value;
    }
    while ((value.significand & UINT64_C(1)) == UINT64_C(0)) {
        value.significand >>= 1U;
        value.exponent += 1;
    }
    return value;
}

static void r_std_convert_source_dyadic(RStdConvertNumericValue source,
                                        const RStdConvertTypeDescriptor *descriptor,
                                        RStdConvertDyadic *value) {
    *value = (RStdConvertDyadic){0};
    if (descriptor->storage == R_STD_CONVERT_STORAGE_SIGNED_INTEGER) {
        uint64_t magnitude = r_std_convert_signed_magnitude(source.payload.signed_integer);

        if (source.payload.signed_integer >= 0) {
        } else {
        }
        value->negative = source.payload.signed_integer < 0;
        value->significand = magnitude;
        return;
    }
    if (descriptor->storage == R_STD_CONVERT_STORAGE_UNSIGNED_INTEGER) {
        value->significand = source.payload.unsigned_integer;
        return;
    }
    if (descriptor->storage == R_STD_CONVERT_STORAGE_BINARY32) {
        uint32_t bits = source.payload.binary32_bits;
        uint32_t encoded_exponent = (bits >> 23U) & UINT32_C(0xff);
        uint32_t fraction = bits & UINT32_C(0x007fffff);

        value->negative = (bits & UINT32_C(0x80000000)) != UINT32_C(0);
        if (encoded_exponent == UINT32_C(0xff)) {
            value->value_class =
                fraction == UINT32_C(0) ? R_STD_CONVERT_DYADIC_INFINITY : R_STD_CONVERT_DYADIC_NAN;
            return;
        }
        if (encoded_exponent == UINT32_C(0)) {
            value->significand = (uint64_t)fraction;
            value->exponent = -149;
        } else {
            value->significand = (uint64_t)(UINT32_C(0x00800000) | fraction);
            value->exponent = (int32_t)encoded_exponent - 150;
        }
        *value = r_std_convert_normalize_dyadic(*value);
        return;
    }
    {
        uint64_t bits = source.payload.binary64_bits;
        uint64_t encoded_exponent = (bits >> 52U) & UINT64_C(0x7ff);
        uint64_t fraction = bits & UINT64_C(0x000fffffffffffff);

        value->negative = (bits & UINT64_C(0x8000000000000000)) != UINT64_C(0);
        if (encoded_exponent == UINT64_C(0x7ff)) {
            value->value_class =
                fraction == UINT64_C(0) ? R_STD_CONVERT_DYADIC_INFINITY : R_STD_CONVERT_DYADIC_NAN;
            return;
        }
        if (encoded_exponent == UINT64_C(0)) {
            value->significand = fraction;
            value->exponent = -1074;
        } else {
            value->significand = UINT64_C(0x0010000000000000) | fraction;
            value->exponent = (int32_t)encoded_exponent - 1075;
        }
        *value = r_std_convert_normalize_dyadic(*value);
    }
}

static int r_std_convert_compare_magnitude(uint64_t left_significand,
                                           int32_t left_exponent,
                                           uint64_t right_significand,
                                           int32_t right_exponent) {
    uint32_t left_width = r_std_convert_bit_width(left_significand);
    uint32_t right_width = r_std_convert_bit_width(right_significand);
    int32_t left_top;
    int32_t right_top;

    if (left_width == 0U) {
        return right_width == 0U ? 0 : -1;
    }
    if (right_width == 0U) {
        return 1;
    }
    left_top = left_exponent + (int32_t)left_width - 1;
    right_top = right_exponent + (int32_t)right_width - 1;
    if (left_top < right_top) {
        return -1;
    }
    if (left_top > right_top) {
        return 1;
    }
    if (left_exponent < right_exponent) {
        uint32_t shift = (uint32_t)(right_exponent - left_exponent);
        uint64_t aligned_right = right_significand << shift;

        return left_significand < aligned_right ? -1 : left_significand > aligned_right ? 1 : 0;
    }
    if (left_exponent > right_exponent) {
        uint32_t shift = (uint32_t)(left_exponent - right_exponent);
        uint64_t aligned_left = left_significand << shift;

        return aligned_left < right_significand ? -1 : aligned_left > right_significand ? 1 : 0;
    }
    return left_significand < right_significand ? -1 : left_significand > right_significand ? 1 : 0;
}

static RStdConvertCheckedResult r_std_convert_error(RStdConvertRangeError error) {
    RStdConvertCheckedResult result = {0};

    result.status = R_STD_CONVERT_CALL_ERROR;
    result.error = error;
    return result;
}

static RStdConvertCheckedResult r_std_convert_success_integer(RStdConvertNumericType type,
                                                              _Bool signed_destination,
                                                              _Bool negative,
                                                              uint64_t magnitude) {
    RStdConvertCheckedResult result = {0};

    result.status = R_STD_CONVERT_CALL_SUCCESS;
    result.value.type = type;
    if (!signed_destination) {
        result.value.payload.unsigned_integer = magnitude;
    } else if (!negative) {
        result.value.payload.signed_integer = (int64_t)magnitude;
    } else if (magnitude == UINT64_C(9223372036854775808)) {
        result.value.payload.signed_integer = INT64_MIN;
    } else {
        result.value.payload.signed_integer = -(int64_t)magnitude;
    }
    return result;
}

static RStdConvertCheckedResult
r_std_convert_to_integer(RStdConvertDyadic source,
                         RStdConvertNumericType destination_type,
                         const RStdConvertTypeDescriptor *destination) {
    uint64_t maximum;
    int comparison;

    if (source.value_class != R_STD_CONVERT_DYADIC_FINITE) {
        return r_std_convert_error(R_STD_CONVERT_RANGE_ERROR_NOT_FINITE);
    }
    if (source.significand == UINT64_C(0)) {
        return r_std_convert_success_integer(destination_type,
                                             destination->storage ==
                                                 R_STD_CONVERT_STORAGE_SIGNED_INTEGER,
                                             0,
                                             UINT64_C(0));
    }
    if (destination->storage == R_STD_CONVERT_STORAGE_UNSIGNED_INTEGER && source.negative) {
        return r_std_convert_error(R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM);
    }
    maximum = source.negative ? destination->max_negative_magnitude : destination->max_positive;
    comparison = r_std_convert_compare_magnitude(source.significand, source.exponent, maximum, 0);
    if (comparison > 0) {
        return r_std_convert_error(source.negative ? R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM
                                                   : R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM);
    }
    if (source.exponent < 0) {
        return r_std_convert_error(R_STD_CONVERT_RANGE_ERROR_FRACTIONAL);
    }
    return r_std_convert_success_integer(destination_type,
                                         destination->storage ==
                                             R_STD_CONVERT_STORAGE_SIGNED_INTEGER,
                                         source.negative,
                                         source.significand << (uint32_t)source.exponent);
}

static RStdConvertCheckedResult
r_std_convert_success_float(RStdConvertDyadic source,
                            RStdConvertNumericType destination_type,
                            const RStdConvertTypeDescriptor *destination) {
    RStdConvertCheckedResult result = {0};
    uint64_t sign;
    uint64_t bits;

    result.status = R_STD_CONVERT_CALL_SUCCESS;
    result.value.type = destination_type;
    sign = source.negative ? UINT64_C(1) : UINT64_C(0);
    if (source.value_class == R_STD_CONVERT_DYADIC_NAN) {
        bits = destination->storage == R_STD_CONVERT_STORAGE_BINARY32
                   ? UINT64_C(0x7fc00000)
                   : UINT64_C(0x7ff8000000000000);
    } else if (source.value_class == R_STD_CONVERT_DYADIC_INFINITY) {
        bits = destination->storage == R_STD_CONVERT_STORAGE_BINARY32
                   ? (sign << 31U) | UINT64_C(0x7f800000)
                   : (sign << 63U) | UINT64_C(0x7ff0000000000000);
    } else if (source.significand == UINT64_C(0)) {
        bits = destination->storage == R_STD_CONVERT_STORAGE_BINARY32 ? sign << 31U : sign << 63U;
    } else {
        uint32_t width = r_std_convert_bit_width(source.significand);
        int32_t top_exponent = source.exponent + (int32_t)width - 1;

        if (top_exponent >= destination->minimum_normal_exponent) {
            uint32_t fraction_bits = destination->precision - 1U;
            uint32_t shift = destination->precision - width;
            uint64_t normalized = source.significand << shift;
            uint64_t fraction_mask = (UINT64_C(1) << fraction_bits) - UINT64_C(1);
            uint64_t encoded_exponent =
                (uint64_t)(top_exponent - destination->minimum_normal_exponent + 1);

            bits = destination->storage == R_STD_CONVERT_STORAGE_BINARY32
                       ? (sign << 31U) | (encoded_exponent << 23U) | (normalized & fraction_mask)
                       : (sign << 63U) | (encoded_exponent << 52U) | (normalized & fraction_mask);
        } else {
            uint32_t shift = (uint32_t)(source.exponent - destination->minimum_subnormal_exponent);
            uint64_t fraction = source.significand << shift;

            bits = destination->storage == R_STD_CONVERT_STORAGE_BINARY32
                       ? (sign << 31U) | fraction
                       : (sign << 63U) | fraction;
        }
    }
    if (destination->storage == R_STD_CONVERT_STORAGE_BINARY32) {
        result.value.payload.binary32_bits = (uint32_t)bits;
    } else {
        result.value.payload.binary64_bits = bits;
    }
    return result;
}

static RStdConvertCheckedResult
r_std_convert_to_float(RStdConvertDyadic source,
                       RStdConvertNumericType destination_type,
                       const RStdConvertTypeDescriptor *destination) {
    int comparison;

    if (source.value_class != R_STD_CONVERT_DYADIC_FINITE) {
        return r_std_convert_success_float(source, destination_type, destination);
    }
    if (source.significand == UINT64_C(0)) {
        return r_std_convert_success_float(source, destination_type, destination);
    }
    comparison = r_std_convert_compare_magnitude(source.significand,
                                                 source.exponent,
                                                 destination->max_finite_significand,
                                                 destination->max_finite_exponent);
    if (comparison > 0) {
        return r_std_convert_error(source.negative ? R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM
                                                   : R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM);
    }
    if (source.exponent < destination->minimum_subnormal_exponent ||
        r_std_convert_bit_width(source.significand) > destination->precision) {
        return r_std_convert_error(R_STD_CONVERT_RANGE_ERROR_INEXACT);
    }
    return r_std_convert_success_float(source, destination_type, destination);
}

RStdConvertCheckedResult r_std_convert_checked(RStdConvertNumericValue source,
                                               RStdConvertDestination destination) {
    RStdConvertTypeDescriptor source_descriptor = {0};
    RStdConvertTypeDescriptor destination_descriptor = {0};
    RStdConvertDyadic source_value;
    _Bool source_type_valid;
    _Bool destination_type_valid;

    source_type_valid = r_std_convert_type_descriptor(source.type, &source_descriptor);
    destination_type_valid =
        r_std_convert_type_descriptor(destination.type, &destination_descriptor);
    (void)source_type_valid;
    (void)destination_type_valid;
    r_std_convert_source_dyadic(source, &source_descriptor, &source_value);
    source_value = r_std_convert_normalize_dyadic(source_value);
    if (destination_descriptor.storage == R_STD_CONVERT_STORAGE_SIGNED_INTEGER ||
        destination_descriptor.storage == R_STD_CONVERT_STORAGE_UNSIGNED_INTEGER) {
        return r_std_convert_to_integer(source_value, destination.type, &destination_descriptor);
    }
    return r_std_convert_to_float(source_value, destination.type, &destination_descriptor);
}

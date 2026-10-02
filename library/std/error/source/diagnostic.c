#include "r_std_error.h"

#include <stddef.h>
#include <stdint.h>

static const char *r_std_error_domain_name(RStdErrorDomain domain) {
    switch (domain) {
    case R_STD_ERROR_DOMAIN_ALLOCATION:
        return "allocation";
    case R_STD_ERROR_DOMAIN_ASYNC_RUNTIME:
        return "async_runtime";
    case R_STD_ERROR_DOMAIN_BYTES:
        return "bytes";
    case R_STD_ERROR_DOMAIN_STRING:
        return "string";
    case R_STD_ERROR_DOMAIN_CONVERSION:
        return "conversion";
    case R_STD_ERROR_DOMAIN_FORMAT:
        return "format";
    case R_STD_ERROR_DOMAIN_MATH:
        return "math";
    case R_STD_ERROR_DOMAIN_TIME:
        return "time";
    case R_STD_ERROR_DOMAIN_ENVIRONMENT:
        return "environment";
    case R_STD_ERROR_DOMAIN_IO:
        return "io";
    case R_STD_ERROR_DOMAIN_FILESYSTEM:
        return "filesystem";
    case R_STD_ERROR_DOMAIN_NETWORK:
        return "network";
    case R_STD_ERROR_DOMAIN_PROCESS:
        return "process";
    case R_STD_ERROR_DOMAIN_THREADING:
        return "threading";
    case R_STD_ERROR_DOMAIN_C_ABI:
        return "c_abi";
    default:
        return "unknown";
    }
}

static const char *r_std_error_family_name(uint32_t code) {
    if (code <= UINT32_C(0x00ff)) {
        return "primary";
    }
    if ((code >= UINT32_C(0x0100)) && (code <= UINT32_C(0x01ff))) {
        return "secondary";
    }
    return "unknown";
}

static void r_std_error_append_text(uint8_t *buffer, size_t *length, const char *text) {
    while (*text != '\0') {
        buffer[*length] = (uint8_t)*text;
        *length += 1U;
        text += 1;
    }
}

static void r_std_error_append_view(uint8_t *buffer, size_t *length, RStdStringView view) {
    size_t index;

    for (index = 0U; index < view.length; index += 1U) {
        buffer[*length] = view.data[index];
        *length += 1U;
    }
}

static void r_std_error_append_u64(uint8_t *buffer, size_t *length, uint64_t value) {
    uint8_t reversed[20];
    size_t count = 0U;

    do {
        reversed[count] = (uint8_t)(value % UINT64_C(10));
        count += 1U;
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (count != 0U) {
        count -= 1U;
        buffer[*length] = (uint8_t)(UINT8_C('0') + reversed[count]);
        *length += 1U;
    }
}

static void r_std_error_append_i64(uint8_t *buffer, size_t *length, int64_t value) {
    uint64_t magnitude;

    if (value < INT64_C(0)) {
        buffer[*length] = UINT8_C('-');
        *length += 1U;
        magnitude = (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
    } else {
        magnitude = (uint64_t)value;
    }
    r_std_error_append_u64(buffer, length, magnitude);
}

RStdErrorDiagnosticResult r_std_error_diagnostic(RRuntimeAllocator *allocator, RStdError value) {
    uint8_t buffer[192];
    size_t length = 0U;
    RStdStringView name = r_std_error_name(value);

    r_std_error_append_text(buffer, &length, "domain=");
    r_std_error_append_text(buffer, &length, r_std_error_domain_name(value.domain));
    r_std_error_append_text(buffer, &length, " family=");
    r_std_error_append_text(buffer, &length, r_std_error_family_name(value.code));
    r_std_error_append_text(buffer, &length, " name=");
    r_std_error_append_view(buffer, &length, name);
    r_std_error_append_text(buffer, &length, " code=");
    r_std_error_append_u64(buffer, &length, value.code);
    r_std_error_append_text(buffer, &length, " native_code=");
    r_std_error_append_i64(buffer, &length, value.native_code);

    return r_std_string_from_str(allocator, (RStdStringView){buffer, length});
}

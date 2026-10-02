#include "r_std_error.h"

#include <stddef.h>
#include <stdint.h>

static RStdStringView r_std_error_name_view(const char *text, size_t length) {
    RStdStringView result = {(const uint8_t *)text, length};
    return result;
}

#define R_STD_ERROR_RETURN_NAME(text) return r_std_error_name_view((text), sizeof(text) - 1U)

RStdStringView r_std_error_name(RStdError value) {
    switch (value.domain) {
    case R_STD_ERROR_DOMAIN_ALLOCATION:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("out_of_memory");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("size_overflow");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("unsupported_alignment");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_ASYNC_RUNTIME:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("runtime_stopping");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("scope_full");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_BYTES:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("out_of_bounds");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("range_overflow");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("unexpected_end");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("invalid_width");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_STRING:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_utf8");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("out_of_bounds");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("not_scalar_boundary");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_CONVERSION:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("empty");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("invalid_radix");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("invalid_digit");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("trailing_character");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("below_minimum");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("above_maximum");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("below_minimum");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("above_maximum");
        case UINT32_C(0x0102):
            R_STD_ERROR_RETURN_NAME("not_finite");
        case UINT32_C(0x0103):
            R_STD_ERROR_RETURN_NAME("fractional");
        case UINT32_C(0x0104):
            R_STD_ERROR_RETURN_NAME("inexact");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_FORMAT:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_radix");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_MATH:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("domain");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("pole");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("overflow");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("underflow");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_TIME:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_value");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("overflow");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("unavailable");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("cancelled");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("other");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("invalid_nanoseconds");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("overflow");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_ENVIRONMENT:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_name");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("invalid_value");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("other");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_IO:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("closed");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("broken_pipe");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("invalid_operation");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("cancelled");
        case UINT32_C(6):
            R_STD_ERROR_RETURN_NAME("timed_out");
        case UINT32_C(7):
            R_STD_ERROR_RETURN_NAME("other");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_FILESYSTEM:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_path");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("invalid_relative_path");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("invalid_operation");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("not_found");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("already_exists");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("not_directory");
        case UINT32_C(6):
            R_STD_ERROR_RETURN_NAME("is_directory");
        case UINT32_C(7):
            R_STD_ERROR_RETURN_NAME("directory_not_empty");
        case UINT32_C(8):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(9):
            R_STD_ERROR_RETURN_NAME("read_only");
        case UINT32_C(10):
            R_STD_ERROR_RETURN_NAME("name_too_long");
        case UINT32_C(11):
            R_STD_ERROR_RETURN_NAME("too_many_links");
        case UINT32_C(12):
            R_STD_ERROR_RETURN_NAME("no_space");
        case UINT32_C(13):
            R_STD_ERROR_RETURN_NAME("file_too_large");
        case UINT32_C(14):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(15):
            R_STD_ERROR_RETURN_NAME("cancelled");
        case UINT32_C(16):
            R_STD_ERROR_RETURN_NAME("timed_out");
        case UINT32_C(17):
            R_STD_ERROR_RETURN_NAME("unsupported");
        case UINT32_C(18):
            R_STD_ERROR_RETURN_NAME("other");
        case UINT32_C(19):
            R_STD_ERROR_RETURN_NAME("closed");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("invalid_utf8");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("embedded_nul");
        case UINT32_C(0x0102):
            R_STD_ERROR_RETURN_NAME("not_representable");
        case UINT32_C(0x0103):
            R_STD_ERROR_RETURN_NAME("absolute_component");
        case UINT32_C(0x0104):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_NETWORK:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_address");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("invalid_name");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("name_not_found");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("temporary_failure");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("connection_refused");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("connection_reset");
        case UINT32_C(6):
            R_STD_ERROR_RETURN_NAME("connection_aborted");
        case UINT32_C(7):
            R_STD_ERROR_RETURN_NAME("address_in_use");
        case UINT32_C(8):
            R_STD_ERROR_RETURN_NAME("address_not_available");
        case UINT32_C(9):
            R_STD_ERROR_RETURN_NAME("network_unreachable");
        case UINT32_C(10):
            R_STD_ERROR_RETURN_NAME("host_unreachable");
        case UINT32_C(11):
            R_STD_ERROR_RETURN_NAME("not_connected");
        case UINT32_C(12):
            R_STD_ERROR_RETURN_NAME("closed");
        case UINT32_C(13):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(14):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(15):
            R_STD_ERROR_RETURN_NAME("message_too_large");
        case UINT32_C(16):
            R_STD_ERROR_RETURN_NAME("cancelled");
        case UINT32_C(17):
            R_STD_ERROR_RETURN_NAME("timed_out");
        case UINT32_C(18):
            R_STD_ERROR_RETURN_NAME("unsupported");
        case UINT32_C(19):
            R_STD_ERROR_RETURN_NAME("other");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("empty");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("invalid_character");
        case UINT32_C(0x0102):
            R_STD_ERROR_RETURN_NAME("invalid_component");
        case UINT32_C(0x0103):
            R_STD_ERROR_RETURN_NAME("out_of_range");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_PROCESS:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("invalid_command");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("invalid_argument");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("not_found");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(4):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(5):
            R_STD_ERROR_RETURN_NAME("spawn_failed");
        case UINT32_C(6):
            R_STD_ERROR_RETURN_NAME("not_running");
        case UINT32_C(7):
            R_STD_ERROR_RETURN_NAME("cancelled");
        case UINT32_C(8):
            R_STD_ERROR_RETURN_NAME("timed_out");
        case UINT32_C(9):
            R_STD_ERROR_RETURN_NAME("unsupported");
        case UINT32_C(10):
            R_STD_ERROR_RETURN_NAME("other");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_THREADING:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("unavailable");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("permission_denied");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("zero_participants");
        default:
            break;
        }
        break;
    case R_STD_ERROR_DOMAIN_C_ABI:
        switch (value.code) {
        case UINT32_C(0):
            R_STD_ERROR_RETURN_NAME("missing_nul");
        case UINT32_C(1):
            R_STD_ERROR_RETURN_NAME("embedded_nul");
        case UINT32_C(2):
            R_STD_ERROR_RETURN_NAME("invalid_utf8");
        case UINT32_C(3):
            R_STD_ERROR_RETURN_NAME("allocation_failed");
        case UINT32_C(0x0100):
            R_STD_ERROR_RETURN_NAME("runtime_stopping");
        case UINT32_C(0x0101):
            R_STD_ERROR_RETURN_NAME("resource_exhausted");
        case UINT32_C(0x0102):
            R_STD_ERROR_RETURN_NAME("floating_environment_unavailable");
        default:
            break;
        }
        break;
    default:
        break;
    }
    R_STD_ERROR_RETURN_NAME("unknown");
}

#undef R_STD_ERROR_RETURN_NAME

#include "r_std_net.h"

#include <stddef.h>
#include <stdint.h>

static size_t r_std_net_append_decimal(uint8_t *target, size_t position, uint8_t value) {
    uint8_t reversed[3];
    size_t count = 0U;
    size_t index;

    do {
        reversed[count] = (uint8_t)(UINT8_C('0') + (value % UINT8_C(10)));
        value = (uint8_t)(value / UINT8_C(10));
        ++count;
    } while (value != UINT8_C(0));
    for (index = count; index > 0U; --index) {
        target[position] = reversed[index - 1U];
        ++position;
    }
    return position;
}

static size_t r_std_net_append_hex(uint8_t *target, size_t position, uint16_t value) {
    static const uint8_t digits[] = "0123456789abcdef";
    uint16_t shift = UINT16_C(12);
    _Bool emitted = 0;

    while (1) {
        uint8_t digit = (uint8_t)((value >> shift) & UINT16_C(0x000f));

        if ((digit != UINT8_C(0)) || emitted || (shift == UINT16_C(0))) {
            target[position] = digits[digit];
            ++position;
            emitted = 1;
        }
        if (shift == UINT16_C(0)) {
            break;
        }
        shift = (uint16_t)(shift - UINT16_C(4));
    }
    return position;
}

static RStdNetStringResult
r_std_net_copy_formatted(RRuntimeAllocator *allocator, const uint8_t *bytes, size_t length) {
    RStdNetStringResult result = {0};
    RStdStringAllocValueResult copied;

    copied = r_std_string_from_str(allocator, (RStdStringView){bytes, length});
    if (copied.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_NET_CALL_SUCCESS;
        result.value = copied.value;
    } else {
        result.status = R_STD_NET_CALL_ERROR;
        result.error = copied.error;
    }
    return result;
}

static size_t r_std_net_ipv4_text(RStdNetIpAddress address, uint8_t *text) {
    size_t length = 0U;
    size_t index;

    for (index = 0U; index < 4U; ++index) {
        if (index != 0U) {
            text[length] = UINT8_C('.');
            ++length;
        }
        length = r_std_net_append_decimal(text, length, address.bytes.v4[index]);
    }
    return length;
}

static size_t r_std_net_ipv6_text(RStdNetIpAddress address, uint8_t *text) {
    uint16_t words[8];
    size_t best_start = SIZE_MAX;
    size_t best_length = 0U;
    size_t current_start = 0U;
    size_t current_length = 0U;
    size_t length = 0U;
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        words[index] = (uint16_t)(((uint16_t)address.bytes.v6[index * 2U] << 8U) |
                                  (uint16_t)address.bytes.v6[(index * 2U) + 1U]);
        if (words[index] == UINT16_C(0)) {
            if (current_length == 0U) {
                current_start = index;
            }
            ++current_length;
            if ((current_length >= 2U) && (current_length > best_length)) {
                best_start = current_start;
                best_length = current_length;
            }
        } else {
            current_length = 0U;
        }
    }

    index = 0U;
    while (index < 8U) {
        if (index == best_start) {
            text[length] = UINT8_C(':');
            text[length + 1U] = UINT8_C(':');
            length += 2U;
            index += best_length;
        } else {
            if ((length != 0U) && (text[length - 1U] != UINT8_C(':'))) {
                text[length] = UINT8_C(':');
                ++length;
            }
            length = r_std_net_append_hex(text, length, words[index]);
            ++index;
        }
    }
    return length;
}

size_t r_library_internal_net_ip_text(RStdNetIpAddress address, uint8_t *text) {
    if (address.kind == R_STD_NET_IP_ADDRESS_V4) {
        return r_std_net_ipv4_text(address, text);
    }
    return r_std_net_ipv6_text(address, text);
}

/* R-TYPE-0046 (L32): `a.b.c.d:port`, `[v6]:port`, or `[v6%scope]:port` with a nonzero scope. */
size_t r_library_internal_net_socket_text(RStdNetSocketAddress address, uint8_t *text) {
    size_t length = 0U;
    uint8_t digits[10];
    size_t count = 0U;
    uint32_t value;

    if (address.address.kind == R_STD_NET_IP_ADDRESS_V4) {
        length = r_std_net_ipv4_text(address.address, text);
    } else {
        text[length] = UINT8_C('[');
        ++length;
        length += r_std_net_ipv6_text(address.address, text + length);
        if (address.scope_id != UINT32_C(0)) {
            text[length] = UINT8_C('%');
            ++length;
            value = address.scope_id;
            do {
                digits[count] = (uint8_t)(UINT8_C('0') + (uint8_t)(value % UINT32_C(10)));
                value /= UINT32_C(10);
                ++count;
            } while (value != UINT32_C(0));
            while (count != 0U) {
                --count;
                text[length] = digits[count];
                ++length;
            }
        }
        text[length] = UINT8_C(']');
        ++length;
    }
    text[length] = UINT8_C(':');
    ++length;
    value = address.port;
    do {
        digits[count] = (uint8_t)(UINT8_C('0') + (uint8_t)(value % UINT32_C(10)));
        value /= UINT32_C(10);
        ++count;
    } while (value != UINT32_C(0));
    while (count != 0U) {
        --count;
        text[length] = digits[count];
        ++length;
    }
    return length;
}

RStdNetStringResult r_std_net_format_ip(RRuntimeAllocator *allocator, RStdNetIpAddress address) {
    uint8_t text[39];

    return r_std_net_copy_formatted(allocator, text, r_library_internal_net_ip_text(address, text));
}

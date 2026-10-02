#include "r_library_net_internal.h"

#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>

static RStdNetIpAddressResult r_library_internal_net_parse_error(RStdNetAddressErrorCode code,
                                                                 size_t index) {
    RStdNetIpAddressResult result = {0};

    result.status = R_STD_NET_CALL_ERROR;
    result.error.code = code;
    result.error.index = index;
    return result;
}

static _Bool r_library_internal_net_is_decimal(uint8_t byte) {
    return (byte >= UINT8_C('0')) && (byte <= UINT8_C('9'));
}

static _Bool r_library_internal_net_hex_value(uint8_t byte, uint8_t *value) {
    if ((byte >= UINT8_C('0')) && (byte <= UINT8_C('9'))) {
        *value = (uint8_t)(byte - UINT8_C('0'));
        return 1;
    }
    if ((byte >= UINT8_C('a')) && (byte <= UINT8_C('f'))) {
        *value = (uint8_t)(UINT8_C(10) + byte - UINT8_C('a'));
        return 1;
    }
    if ((byte >= UINT8_C('A')) && (byte <= UINT8_C('F'))) {
        *value = (uint8_t)(UINT8_C(10) + byte - UINT8_C('A'));
        return 1;
    }
    return 0;
}

static _Bool r_library_internal_net_parse_ipv4_bytes(const uint8_t *text,
                                                     size_t length,
                                                     size_t base_index,
                                                     uint8_t bytes[4],
                                                     RStdNetAddressError *error) {
    size_t component;
    size_t position = 0U;

    for (component = 0U; component < 4U; ++component) {
        uint16_t value = UINT16_C(0);

        if (position == length) {
            error->code = R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT;
            error->index = base_index + length;
            return 0;
        }
        if (text[position] == UINT8_C('.')) {
            error->code = R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT;
            error->index = base_index + position;
            return 0;
        }
        while ((position < length) && (text[position] != UINT8_C('.'))) {
            uint16_t digit;

            if (!r_library_internal_net_is_decimal(text[position])) {
                error->code = R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER;
                error->index = base_index + position;
                return 0;
            }
            digit = (uint16_t)(text[position] - UINT8_C('0'));
            value = (uint16_t)((value * UINT16_C(10)) + digit);
            if (value > UINT16_C(255)) {
                error->code = R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE;
                error->index = base_index + position;
                return 0;
            }
            ++position;
        }
        bytes[component] = (uint8_t)value;
        if (component != 3U) {
            if (position == length) {
                error->code = R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT;
                error->index = base_index + length;
                return 0;
            }
            ++position;
        } else if (position != length) {
            error->code = R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT;
            error->index = base_index + position;
            return 0;
        }
    }
    return 1;
}

static RStdNetIpAddressResult r_library_internal_net_parse_ipv4(RStdStringView text) {
    RStdNetIpAddressResult result = {0};

    result.value.kind = R_STD_NET_IP_ADDRESS_V4;
    if (!r_library_internal_net_parse_ipv4_bytes(
            text.data, text.length, 0U, result.value.bytes.v4, &result.error)) {
        result.status = R_STD_NET_CALL_ERROR;
        return result;
    }
    result.status = R_STD_NET_CALL_SUCCESS;
    return result;
}

static RStdNetIpAddressResult r_library_internal_net_parse_ipv6(RStdStringView text) {
    RStdNetIpAddressResult result = {0};
    uint16_t explicit_words[8] = {0};
    size_t explicit_count = 0U;
    size_t compression_slot = SIZE_MAX;
    size_t position = 0U;
    size_t index;

    result.value.kind = R_STD_NET_IP_ADDRESS_V6;
    if (text.data[0] == UINT8_C(':')) {
        if ((text.length < 2U) || (text.data[1] != UINT8_C(':'))) {
            return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                      0U);
        }
        compression_slot = 0U;
        position = 2U;
        if ((position < text.length) && (text.data[position] == UINT8_C(':'))) {
            return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                      1U);
        }
    }

    while (position < text.length) {
        uint8_t ipv4_bytes[4] = {0};
        size_t component_start = position;
        size_t component_end;
        _Bool has_dot = 0;

        while ((position < text.length) && (text.data[position] != UINT8_C(':'))) {
            if (text.data[position] == UINT8_C('.')) {
                has_dot = 1;
            }
            ++position;
        }
        component_end = position;
        if (component_end == component_start) {
            return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                      component_start);
        }

        if (has_dot) {
            RStdNetAddressError error = {0};
            size_t new_count = explicit_count + 2U;

            if (text.data[component_start] == UINT8_C('.')) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          component_start);
            }
            if (!r_library_internal_net_is_decimal(text.data[component_start])) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER,
                                                          component_start);
            }
            if ((new_count > 8U) || ((compression_slot != SIZE_MAX) && (new_count >= 8U))) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          component_start);
            }
            if (!r_library_internal_net_parse_ipv4_bytes(text.data + component_start,
                                                         component_end - component_start,
                                                         component_start,
                                                         ipv4_bytes,
                                                         &error)) {
                return r_library_internal_net_parse_error(error.code, error.index);
            }
            if (component_end != text.length) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          component_end);
            }
            explicit_words[explicit_count] =
                (uint16_t)(((uint16_t)ipv4_bytes[0] << 8U) | (uint16_t)ipv4_bytes[1]);
            explicit_words[explicit_count + 1U] =
                (uint16_t)(((uint16_t)ipv4_bytes[2] << 8U) | (uint16_t)ipv4_bytes[3]);
            explicit_count = new_count;
        } else {
            uint16_t word = UINT16_C(0);
            size_t digit_index;
            uint8_t first_digit = UINT8_C(0);

            if (!r_library_internal_net_hex_value(text.data[component_start], &first_digit)) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER,
                                                          component_start);
            }
            if ((explicit_count == 8U) ||
                ((compression_slot != SIZE_MAX) && (explicit_count == 7U))) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          component_start);
            }

            for (digit_index = 0U; digit_index < component_end - component_start; ++digit_index) {
                uint8_t digit = UINT8_C(0);

                if (!r_library_internal_net_hex_value(text.data[component_start + digit_index],
                                                      &digit)) {
                    return r_library_internal_net_parse_error(
                        R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER, component_start + digit_index);
                }
                if (digit_index == 4U) {
                    return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE,
                                                              component_start + digit_index);
                }
                word = (uint16_t)((word << 4U) | (uint16_t)digit);
            }
            explicit_words[explicit_count] = word;
            ++explicit_count;
        }

        if (position == text.length) {
            break;
        }
        if (((position + 1U) < text.length) && (text.data[position + 1U] == UINT8_C(':'))) {
            if (compression_slot != SIZE_MAX) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          position);
            }
            if (explicit_count == 8U) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          position);
            }
            compression_slot = explicit_count;
            position += 2U;
            if ((position < text.length) && (text.data[position] == UINT8_C(':'))) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          position - 1U);
            }
        } else {
            ++position;
            if (position == text.length) {
                return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                          text.length);
            }
        }
    }

    if (compression_slot == SIZE_MAX) {
        if (explicit_count != 8U) {
            return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                                      text.length);
        }
    } else {
        size_t suffix_count = explicit_count - compression_slot;
        size_t suffix_destination = 8U - suffix_count;

        for (index = suffix_count; index > 0U; --index) {
            explicit_words[suffix_destination + index - 1U] =
                explicit_words[compression_slot + index - 1U];
        }
        for (index = compression_slot; index < suffix_destination; ++index) {
            explicit_words[index] = UINT16_C(0);
        }
    }

    for (index = 0U; index < 8U; ++index) {
        result.value.bytes.v6[index * 2U] = (uint8_t)(explicit_words[index] >> 8U);
        result.value.bytes.v6[(index * 2U) + 1U] = (uint8_t)explicit_words[index];
    }
    result.status = R_STD_NET_CALL_SUCCESS;
    return result;
}

RStdNetIpAddressResult r_library_internal_net_parse_ip(RStdStringView text) {
    size_t index;

    if (text.length == 0U) {
        return r_library_internal_net_parse_error(R_STD_NET_ADDRESS_ERROR_EMPTY, 0U);
    }
    for (index = 0U; index < text.length; ++index) {
        if (text.data[index] == UINT8_C(':')) {
            return r_library_internal_net_parse_ipv6(text);
        }
    }
    return r_library_internal_net_parse_ipv4(text);
}

#include "r_std_net.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static RStdStringView r_test_net_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static int r_test_net_parse_equals(const char *text,
                                   RStdNetIpAddressKind kind,
                                   const uint8_t *expected,
                                   size_t expected_length) {
    RStdNetIpAddressResult parsed = r_std_net_parse_ip(r_test_net_view(text));
    const uint8_t *actual;

    R_TEST_CHECK(parsed.status == R_STD_NET_CALL_SUCCESS);
    R_TEST_CHECK(parsed.value.kind == kind);
    actual = kind == R_STD_NET_IP_ADDRESS_V4 ? parsed.value.bytes.v4 : parsed.value.bytes.v6;
    R_TEST_CHECK(memcmp(actual, expected, expected_length) == 0);
    return 0;
}

static int r_test_net_error_equals(const char *text, RStdNetAddressErrorCode code, size_t index) {
    RStdNetIpAddressResult parsed = r_std_net_parse_ip(r_test_net_view(text));

    R_TEST_CHECK(parsed.status == R_STD_NET_CALL_ERROR);
    R_TEST_CHECK(parsed.error.code == code);
    R_TEST_CHECK(parsed.error.index == index);
    return 0;
}

static int r_test_net_format_equals(const char *source, const char *expected) {
    RRuntimeAllocator allocator;
    RStdNetIpAddressResult parsed;
    RStdNetStringResult formatted;
    RStdStringView view;
    size_t expected_length = strlen(expected);

    r_runtime_allocator_initialize(&allocator);
    parsed = r_std_net_parse_ip(r_test_net_view(source));
    R_TEST_CHECK(parsed.status == R_STD_NET_CALL_SUCCESS);
    formatted = r_std_net_format_ip(&allocator, parsed.value);
    R_TEST_CHECK(formatted.status == R_STD_NET_CALL_SUCCESS);
    view = r_std_string_as_str(&formatted.value);
    R_TEST_CHECK(view.length == expected_length);
    R_TEST_CHECK((expected_length == 0U) || (memcmp(view.data, expected, expected_length) == 0));
    r_runtime_string_destroy(&formatted.value);
    return 0;
}

static int r_test_net_ipv4(void) {
    static const uint8_t zero[] = {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    static const uint8_t maximum[] = {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)};
    static const uint8_t ordinary[] = {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};

    R_TEST_CHECK(r_test_net_parse_equals("0.0.0.0", R_STD_NET_IP_ADDRESS_V4, zero, sizeof(zero)) ==
                 0);
    R_TEST_CHECK(r_test_net_parse_equals(
                     "255.255.255.255", R_STD_NET_IP_ADDRESS_V4, maximum, sizeof(maximum)) == 0);
    R_TEST_CHECK(r_test_net_parse_equals(
                     "001.002.003.004", R_STD_NET_IP_ADDRESS_V4, ordinary, sizeof(ordinary)) == 0);
    R_TEST_CHECK(
        r_test_net_parse_equals("00000.0.0.0", R_STD_NET_IP_ADDRESS_V4, zero, sizeof(zero)) == 0);
    R_TEST_CHECK(r_test_net_error_equals("", R_STD_NET_ADDRESS_ERROR_EMPTY, 0U) == 0);
    R_TEST_CHECK(
        r_test_net_error_equals("1.2.x.4", R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER, 4U) == 0);
    R_TEST_CHECK(r_test_net_error_equals("1.2.256.4", R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE, 6U) ==
                 0);
    R_TEST_CHECK(r_test_net_error_equals("1..2.3", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 2U) ==
                 0);
    R_TEST_CHECK(r_test_net_error_equals("1.2.3", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 5U) ==
                 0);
    R_TEST_CHECK(
        r_test_net_error_equals("1.2.3.4.5", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 7U) == 0);
    R_TEST_CHECK(r_test_net_format_equals("001.002.003.004", "1.2.3.4") == 0);
    return 0;
}

static int r_test_net_ipv6(void) {
    static const uint8_t loopback[] = {UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(1)};
    static const uint8_t embedded[] = {UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0),
                                       UINT8_C(0xff),
                                       UINT8_C(0xff),
                                       UINT8_C(192),
                                       UINT8_C(0),
                                       UINT8_C(2),
                                       UINT8_C(128)};
    const char *embedded_range = "::ffff:192.0.2.256";
    const char *misplaced_ipv4 = "::ffff:192.0.2.1:7";

    R_TEST_CHECK(
        r_test_net_parse_equals("::1", R_STD_NET_IP_ADDRESS_V6, loopback, sizeof(loopback)) == 0);
    R_TEST_CHECK(r_test_net_parse_equals(
                     "::ffff:192.0.2.128", R_STD_NET_IP_ADDRESS_V6, embedded, sizeof(embedded)) ==
                 0);
    R_TEST_CHECK(r_test_net_format_equals("::", "::") == 0);
    R_TEST_CHECK(r_test_net_format_equals("0:0:0:0:0:0:0:1", "::1") == 0);
    R_TEST_CHECK(
        r_test_net_format_equals("2001:0db8:0:0:0:ff00:0042:8329", "2001:db8::ff00:42:8329") == 0);
    R_TEST_CHECK(r_test_net_format_equals("1:0:0:2:0:0:3:4", "1::2:0:0:3:4") == 0);
    R_TEST_CHECK(r_test_net_format_equals("1:0:2:3:4:5:6:7", "1:0:2:3:4:5:6:7") == 0);
    R_TEST_CHECK(r_test_net_format_equals("::ffff:192.0.2.128", "::ffff:c000:280") == 0);

    R_TEST_CHECK(r_test_net_error_equals(
                     "1:2:3:4:5:6:7", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 13U) == 0);
    R_TEST_CHECK(r_test_net_error_equals(
                     "1:2:3:4:5:6:7:8:9", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 16U) == 0);
    R_TEST_CHECK(r_test_net_error_equals(
                     "1:2:3:4:5:6:7:8:00000", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 16U) == 0);
    R_TEST_CHECK(r_test_net_error_equals(
                     "1:2:3:4:5:6:7:00000", R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE, 18U) == 0);
    R_TEST_CHECK(
        r_test_net_error_equals("1::2::3", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 4U) == 0);
    R_TEST_CHECK(r_test_net_error_equals(":::1", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 1U) ==
                 0);
    R_TEST_CHECK(r_test_net_error_equals("::gg", R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER, 2U) ==
                 0);
    R_TEST_CHECK(r_test_net_error_equals(embedded_range,
                                         R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE,
                                         strlen(embedded_range) - 1U) == 0);
    R_TEST_CHECK(r_test_net_error_equals("::ffff:192.0.2",
                                         R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                         strlen("::ffff:192.0.2")) == 0);
    R_TEST_CHECK(r_test_net_error_equals(misplaced_ipv4,
                                         R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT,
                                         (size_t)(strrchr(misplaced_ipv4, ':') - misplaced_ipv4)) ==
                 0);
    R_TEST_CHECK(
        r_test_net_error_equals("1::2::x", R_STD_NET_ADDRESS_ERROR_INVALID_COMPONENT, 4U) == 0);
    R_TEST_CHECK(r_test_net_error_equals(
                     "1:2:3:4:5:6:7:0000x", R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER, 18U) == 0);
    return 0;
}

static int r_test_net_contracts_and_allocation(void) {
    static const uint8_t non_ascii[] = {UINT8_C(0xcf), UINT8_C(0x80)};
    RRuntimeAllocator allocator;
    RStdNetIpAddressResult parsed;
    RStdNetStringResult formatted;
    RStdError converted;

    parsed = r_std_net_parse_ip((RStdStringView){non_ascii, sizeof(non_ascii)});
    R_TEST_CHECK(parsed.status == R_STD_NET_CALL_ERROR);
    R_TEST_CHECK(parsed.error.code == R_STD_NET_ADDRESS_ERROR_INVALID_CHARACTER);
    R_TEST_CHECK(parsed.error.index == 0U);

    parsed = r_std_net_parse_ip(r_test_net_view("127.0.0.1"));
    R_TEST_CHECK(parsed.status == R_STD_NET_CALL_SUCCESS);
    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    formatted = r_std_net_format_ip(&allocator, parsed.value);
    R_TEST_CHECK(formatted.status == R_STD_NET_CALL_ERROR);
    R_TEST_CHECK(formatted.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    converted = r_std_net_as_error((RStdNetError){R_STD_NET_ERROR_CONNECTION_RESET, INT64_C(54)});
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_NETWORK);
    R_TEST_CHECK(converted.code == (uint32_t)R_STD_NET_ERROR_CONNECTION_RESET);
    R_TEST_CHECK(converted.native_code == INT64_C(54));
    return 0;
}

static uint64_t r_test_net_next(uint64_t *state) {
    uint64_t value = *state;

    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    *state = value;
    return value;
}

static int r_test_net_canonical_round_trip(void) {
    RRuntimeAllocator allocator;
    uint64_t state = UINT64_C(0x8f4d5a6bc172930e);
    size_t iteration;

    r_runtime_allocator_initialize(&allocator);
    for (iteration = 0U; iteration < 8192U; ++iteration) {
        RStdNetIpAddress source = {0};
        RStdNetStringResult formatted;
        RStdStringView text;
        RStdNetIpAddressResult parsed;
        size_t byte_count;
        size_t index;

        source.kind = (iteration & 1U) == 0U ? R_STD_NET_IP_ADDRESS_V4 : R_STD_NET_IP_ADDRESS_V6;
        byte_count = source.kind == R_STD_NET_IP_ADDRESS_V4 ? 4U : 16U;
        for (index = 0U; index < byte_count; ++index) {
            uint8_t value = (uint8_t)r_test_net_next(&state);

            if (source.kind == R_STD_NET_IP_ADDRESS_V4) {
                source.bytes.v4[index] = value;
            } else {
                source.bytes.v6[index] = value;
            }
        }
        formatted = r_std_net_format_ip(&allocator, source);
        R_TEST_CHECK(formatted.status == R_STD_NET_CALL_SUCCESS);
        text = r_std_string_as_str(&formatted.value);
        R_TEST_CHECK(text.length <= (source.kind == R_STD_NET_IP_ADDRESS_V4 ? 15U : 39U));
        parsed = r_std_net_parse_ip(text);
        R_TEST_CHECK(parsed.status == R_STD_NET_CALL_SUCCESS);
        R_TEST_CHECK(parsed.value.kind == source.kind);
        R_TEST_CHECK(
            memcmp(source.kind == R_STD_NET_IP_ADDRESS_V4 ? source.bytes.v4 : source.bytes.v6,
                   source.kind == R_STD_NET_IP_ADDRESS_V4 ? parsed.value.bytes.v4
                                                          : parsed.value.bytes.v6,
                   byte_count) == 0);
        r_runtime_string_destroy(&formatted.value);
    }
    return 0;
}

/* R-TYPE-0046 (L32): the standard texts of addresses equal format_ip, with the port and a
 * nonzero v6 scope. */
static int r_test_net_address_texts(void) {
    static const struct {
        const char *address;
        uint16_t port;
        uint32_t scope;
        const char *expected;
    } cases[] = {
        {"10.0.0.1", 80U, 0U, "10.0.0.1:80"},
        {"255.255.255.255", 65535U, 0U, "255.255.255.255:65535"},
        {"::", 0U, 0U, "[::]:0"},
        {"fe80::1", 8080U, 3U, "[fe80::1%3]:8080"},
        {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
         65535U,
         4294967295U,
         "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff%4294967295]:65535"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint8_t text[64];
        uint8_t ip_text[39];
        RStdNetIpAddressResult parsed = r_std_net_parse_ip(r_test_net_view(cases[index].address));
        R_TEST_CHECK(parsed.status == R_STD_NET_CALL_SUCCESS);
        const RStdNetSocketAddress socket = {parsed.value, cases[index].port, cases[index].scope};
        const size_t length = r_library_internal_net_socket_text(socket, text);
        R_TEST_CHECK(length == strlen(cases[index].expected));
        R_TEST_CHECK(memcmp(text, cases[index].expected, length) == 0);
        const size_t ip_length = r_library_internal_net_ip_text(parsed.value, ip_text);
        R_TEST_CHECK(ip_length == strlen(cases[index].address));
        R_TEST_CHECK(memcmp(ip_text, cases[index].address, ip_length) == 0);
    }
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_net_ipv4() == 0);
    R_TEST_CHECK(r_test_net_ipv6() == 0);
    R_TEST_CHECK(r_test_net_contracts_and_allocation() == 0);
    R_TEST_CHECK(r_test_net_canonical_round_trip() == 0);
    R_TEST_CHECK(r_test_net_address_texts() == 0);
    return 0;
}

#include "r_library_net_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"

#include <arpa/inet.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Bool r_library_internal_net_address_to_native(RStdNetSocketAddress address,
                                               struct sockaddr_storage *native,
                                               socklen_t *native_length,
                                               int *domain) {
    if (native == NULL || native_length == NULL || domain == NULL) {
        return 0;
    }
    (void)memset(native, 0, sizeof(*native));
    if (address.address.kind == R_STD_NET_IP_ADDRESS_V4) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)native;

        if (address.scope_id != 0U) {
            return 0;
        }
#if defined(__APPLE__)
        v4->sin_len = (uint8_t)sizeof(*v4);
#endif
        v4->sin_family = AF_INET;
        v4->sin_port = htons(address.port);
        (void)memcpy(&v4->sin_addr, address.address.bytes.v4, sizeof(address.address.bytes.v4));
        *native_length = (socklen_t)sizeof(*v4);
        *domain = AF_INET;
        return 1;
    }
    if (address.address.kind == R_STD_NET_IP_ADDRESS_V6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)native;

        if (address.scope_id != 0U) {
            char interface_name[IF_NAMESIZE];

            if (if_indextoname(address.scope_id, interface_name) == NULL) {
                return 0;
            }
        }
#if defined(__APPLE__)
        v6->sin6_len = (uint8_t)sizeof(*v6);
#endif
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(address.port);
        v6->sin6_scope_id = address.scope_id;
        (void)memcpy(&v6->sin6_addr, address.address.bytes.v6, sizeof(address.address.bytes.v6));
        *native_length = (socklen_t)sizeof(*v6);
        *domain = AF_INET6;
        return 1;
    }
    return 0;
}

static int compare_instant(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds != right.storage_seconds) {
        return left.storage_seconds < right.storage_seconds ? -1 : 1;
    }
    if (left.storage_nanoseconds != right.storage_nanoseconds) {
        return left.storage_nanoseconds < right.storage_nanoseconds ? -1 : 1;
    }
    return 0;
}

RLibraryNetDeadlineStatus r_library_internal_net_deadline_timeout(RStdNetDeadline deadline,
                                                                  uint64_t *timeout_nanoseconds,
                                                                  RStdNetError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    if (timeout_nanoseconds == NULL || error == NULL) {
        return R_LIBRARY_NET_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = 0U;
    *error = (RStdNetError){0};
    if (!deadline.has_value) {
        return R_LIBRARY_NET_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        error->code = R_STD_NET_ERROR_OTHER;
        return R_LIBRARY_NET_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        error->code = R_STD_NET_ERROR_OTHER;
        error->native_code = now.error.native_code;
        return R_LIBRARY_NET_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        error->code = R_STD_NET_ERROR_TIMED_OUT;
        return R_LIBRARY_NET_DEADLINE_EXPIRED;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        error->code = R_STD_NET_ERROR_OTHER;
        error->native_code = remaining.error.native_code;
        return R_LIBRARY_NET_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *timeout_nanoseconds = (uint64_t)INT64_MAX;
    } else {
        const uint64_t whole = seconds * nanoseconds_per_second;
        const uint64_t fraction = (uint64_t)remaining.value.nanoseconds;

        *timeout_nanoseconds =
            fraction > ((uint64_t)INT64_MAX - whole) ? (uint64_t)INT64_MAX : whole + fraction;
    }
    if (*timeout_nanoseconds == 0U) {
        *timeout_nanoseconds = 1U;
    }
    return R_LIBRARY_NET_DEADLINE_READY;
}

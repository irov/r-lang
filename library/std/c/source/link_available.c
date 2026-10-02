#include "r_std_c.h"

#include <string.h>

static _Bool r_std_c_logical_name_is_valid(RStdStringView logical_name) {
    size_t index;
    _Bool segment_start = 1;

    if ((logical_name.length == 0U) || (logical_name.length > 255U)) {
        return 0;
    }
    for (index = 0U; index < logical_name.length; ++index) {
        const uint8_t value = logical_name.data[index];

        if (segment_start) {
            if ((value < UINT8_C('a')) || (value > UINT8_C('z'))) {
                return 0;
            }
            segment_start = 0;
        } else if ((value == UINT8_C('.')) || (value == UINT8_C('_')) || (value == UINT8_C('-'))) {
            segment_start = 1;
        } else if (!(((value >= UINT8_C('a')) && (value <= UINT8_C('z'))) ||
                     ((value >= UINT8_C('0')) && (value <= UINT8_C('9'))))) {
            return 0;
        }
    }
    return !segment_start;
}

_Bool r_std_c_link_available(RStdCLinkManifestView manifest, RStdStringView logical_name) {
    size_t index;

    if (!r_std_c_logical_name_is_valid(logical_name)) {
        return 0;
    }
    for (index = 0U; index < manifest.count; ++index) {
        const RStdCLinkManifestEntry *entry = &manifest.entries[index];

        if ((entry->logical_name.length == logical_name.length) &&
            (memcmp(entry->logical_name.data, logical_name.data, logical_name.length) == 0)) {
            return entry->available;
        }
    }
    return 0;
}

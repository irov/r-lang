#include "named_standard_copy_abi.h"

#include "named_standard_copy_abi.generated.inc"

#include <string.h>

_Static_assert(sizeof(r_named_standard_copy_abi_headers) /
                       sizeof(r_named_standard_copy_abi_headers[0]) <=
                   64U,
               "named standard Copy ABI header bitset overflow");
_Static_assert(sizeof(r_named_standard_layout_abi_headers) /
                       sizeof(r_named_standard_layout_abi_headers[0]) <=
                   64U,
               "named standard layout ABI header bitset overflow");

size_t r_named_standard_copy_abi_count(void) {
    return sizeof(r_named_standard_copy_abi_records) / sizeof(r_named_standard_copy_abi_records[0]);
}

const RNamedStandardCopyAbi *r_named_standard_copy_abi_at(size_t index) {
    return index < r_named_standard_copy_abi_count() ? &r_named_standard_copy_abi_records[index]
                                                     : NULL;
}

const RNamedStandardCopyAbi *r_named_standard_copy_abi_find(const char *name, size_t length) {
    size_t left = 0U;
    size_t right = r_named_standard_copy_abi_count();

    if (name == NULL) {
        return NULL;
    }
    while (left < right) {
        const size_t middle = left + ((right - left) / 2U);
        const RNamedStandardCopyAbi *candidate = &r_named_standard_copy_abi_records[middle];
        const size_t common_length =
            length < candidate->r_name_length ? length : candidate->r_name_length;
        const int comparison = memcmp(name, candidate->r_name, common_length);

        if ((comparison == 0) && (length == candidate->r_name_length)) {
            return candidate;
        }
        if ((comparison < 0) || ((comparison == 0) && (length < candidate->r_name_length))) {
            right = middle;
        } else {
            left = middle + 1U;
        }
    }
    return NULL;
}

size_t r_named_standard_copy_abi_header_count(void) {
    return sizeof(r_named_standard_copy_abi_headers) / sizeof(r_named_standard_copy_abi_headers[0]);
}

const char *r_named_standard_copy_abi_header_at(size_t index) {
    return index < r_named_standard_copy_abi_header_count()
               ? r_named_standard_copy_abi_headers[index]
               : NULL;
}

size_t r_named_standard_layout_abi_count(void) {
    return sizeof(r_named_standard_layout_abi_records) /
           sizeof(r_named_standard_layout_abi_records[0]);
}

const RNamedStandardLayoutAbi *r_named_standard_layout_abi_at(size_t index) {
    return index < r_named_standard_layout_abi_count() ? &r_named_standard_layout_abi_records[index]
                                                       : NULL;
}

const RNamedStandardLayoutAbi *r_named_standard_layout_abi_find(const char *name, size_t length) {
    size_t left = 0U;
    size_t right = r_named_standard_layout_abi_count();

    if (name == NULL) {
        return NULL;
    }
    while (left < right) {
        const size_t middle = left + ((right - left) / 2U);
        const RNamedStandardLayoutAbi *candidate = &r_named_standard_layout_abi_records[middle];
        const size_t common_length =
            length < candidate->r_name_length ? length : candidate->r_name_length;
        const int comparison = memcmp(name, candidate->r_name, common_length);

        if ((comparison == 0) && (length == candidate->r_name_length)) {
            return candidate;
        }
        if ((comparison < 0) || ((comparison == 0) && (length < candidate->r_name_length))) {
            right = middle;
        } else {
            left = middle + 1U;
        }
    }
    return NULL;
}

size_t r_named_standard_layout_abi_header_count(void) {
    return sizeof(r_named_standard_layout_abi_headers) /
           sizeof(r_named_standard_layout_abi_headers[0]);
}

const char *r_named_standard_layout_abi_header_at(size_t index) {
    return index < r_named_standard_layout_abi_header_count()
               ? r_named_standard_layout_abi_headers[index]
               : NULL;
}

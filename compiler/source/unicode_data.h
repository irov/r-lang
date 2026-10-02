/* Generated from Unicode 17.0.0; do not edit. */
#ifndef R_UNICODE_DATA_H
#define R_UNICODE_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct RUnicodeRange {
    uint32_t first;
    uint32_t last;
} RUnicodeRange;

typedef struct RUnicodeCombiningClass {
    uint32_t code_point;
    uint8_t combining_class;
} RUnicodeCombiningClass;

typedef struct RUnicodeDecomposition {
    uint32_t code_point;
    uint32_t first_mapping;
    uint16_t mapping_length;
} RUnicodeDecomposition;

typedef struct RUnicodeComposition {
    uint32_t first;
    uint32_t second;
    uint32_t composite;
} RUnicodeComposition;

extern const RUnicodeRange r_unicode_xid_start_ranges[];
extern const size_t r_unicode_xid_start_range_count;
extern const RUnicodeRange r_unicode_xid_continue_ranges[];
extern const size_t r_unicode_xid_continue_range_count;
extern const RUnicodeCombiningClass r_unicode_combining_classes[];
extern const size_t r_unicode_combining_class_count;
extern const RUnicodeDecomposition r_unicode_decompositions[];
extern const size_t r_unicode_decomposition_count;
extern const uint32_t r_unicode_decomposition_mappings[];
extern const size_t r_unicode_decomposition_mapping_count;
extern const RUnicodeComposition r_unicode_compositions[];
extern const size_t r_unicode_composition_count;

#endif

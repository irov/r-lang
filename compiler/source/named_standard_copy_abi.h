#ifndef R_NAMED_STANDARD_COPY_ABI_H
#define R_NAMED_STANDARD_COPY_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RNamedStandardCopyAbi {
    const char *r_name;
    size_t r_name_length;
    const char *c_type;
    uint32_t generic_arity;
    uint32_t header_index;
    bool is_pod;
    size_t object_size;
    size_t alignment;
} RNamedStandardCopyAbi;

typedef struct RNamedStandardLayoutAbi {
    const char *r_name;
    size_t r_name_length;
    const char *c_type;
    const char *drop;
    uint32_t generic_arity;
    uint32_t header_index;
    uint32_t record_index;
    size_t object_size;
    size_t alignment;
} RNamedStandardLayoutAbi;

size_t r_named_standard_copy_abi_count(void);
const RNamedStandardCopyAbi *r_named_standard_copy_abi_at(size_t index);
const RNamedStandardCopyAbi *r_named_standard_copy_abi_find(const char *name, size_t length);
size_t r_named_standard_copy_abi_header_count(void);
const char *r_named_standard_copy_abi_header_at(size_t index);
size_t r_named_standard_layout_abi_count(void);
const RNamedStandardLayoutAbi *r_named_standard_layout_abi_at(size_t index);
const RNamedStandardLayoutAbi *r_named_standard_layout_abi_find(const char *name, size_t length);
size_t r_named_standard_layout_abi_header_count(void);
const char *r_named_standard_layout_abi_header_at(size_t index);

#endif

#ifndef R_NAMED_STANDARD_MOVE_ABI_H
#define R_NAMED_STANDARD_MOVE_ABI_H

#include <stddef.h>
#include <stdint.h>

typedef struct RNamedStandardMoveAbi {
    const char *r_name;
    size_t r_name_length;
    const char *c_type;
    const char *move_initialize;
    const char *drop;
    uint32_t generic_arity;
    uint32_t header_index;
} RNamedStandardMoveAbi;

size_t r_named_standard_move_abi_count(void);
const RNamedStandardMoveAbi *r_named_standard_move_abi_at(size_t index);
const RNamedStandardMoveAbi *r_named_standard_move_abi_find(const char *name, size_t length);
size_t r_named_standard_move_abi_header_count(void);
const char *r_named_standard_move_abi_header_at(size_t index);

#endif

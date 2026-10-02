#ifndef R_LIBRARY_BYTES_INTERNAL_H
#define R_LIBRARY_BYTES_INTERNAL_H

#include "r_std_bytes.h"

RStdAllocCallStatus r_library_internal_bytes_map_allocation_status(RRuntimeArrayStatus status,
                                                                   RStdAllocError *error);
RStdBytesAllocResult
r_library_internal_bytes_append(RStdBytes *target, const uint8_t *source, size_t length);

#endif

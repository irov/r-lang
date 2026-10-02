#ifndef R_LIBRARY_STRING_INTERNAL_H
#define R_LIBRARY_STRING_INTERNAL_H

#include "r_std_string.h"

RStdStringCallStatus r_library_internal_string_map_allocation_status(RRuntimeStringStatus status,
                                                                     RStdAllocError *error);

#endif

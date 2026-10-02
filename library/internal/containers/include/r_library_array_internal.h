#ifndef R_LIBRARY_ARRAY_INTERNAL_H
#define R_LIBRARY_ARRAY_INTERNAL_H

#include "r_std_array.h"

RStdArrayCallStatus r_library_internal_array_map_allocation_status(RRuntimeArrayStatus status,
                                                                   RStdAllocError *error);

#endif

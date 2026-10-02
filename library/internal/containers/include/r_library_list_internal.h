#ifndef R_LIBRARY_LIST_INTERNAL_H
#define R_LIBRARY_LIST_INTERNAL_H

#include "r_std_list.h"

RStdListCallStatus r_library_internal_list_map_allocation_status(RRuntimeListStatus status,
                                                                 RStdAllocError *error);

#endif

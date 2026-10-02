#include "r_std_list.h"

RStdListMutPointerOption r_std_list_get_mut(RStdList *source, size_t index) {
    RStdListMutPointerOption result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_get_mut(source, index);
    result.has_value = result.value != NULL;
    return result;
}

#include "r_std_list.h"

RStdListMutPointerOption r_std_list_back_mut(RStdList *source) {
    RStdListMutPointerOption result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_back_mut(source);
    result.has_value = result.value != NULL;
    return result;
}

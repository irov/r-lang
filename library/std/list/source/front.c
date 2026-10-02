#include "r_std_list.h"

RStdListConstPointerOption r_std_list_front(const RStdList *source) {
    RStdListConstPointerOption result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_front(source);
    result.has_value = result.value != NULL;
    return result;
}

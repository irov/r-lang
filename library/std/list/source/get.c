#include "r_std_list.h"

RStdListConstPointerOption r_std_list_get(const RStdList *source, size_t index) {
    RStdListConstPointerOption result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_get(source, index);
    result.has_value = result.value != NULL;
    return result;
}

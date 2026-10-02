#include "r_std_list.h"

RStdListIteratorResult r_std_list_iter(const RStdList *source) {
    RStdListIteratorResult result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_iter(source);
    return result;
}

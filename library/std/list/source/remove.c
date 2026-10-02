#include "r_std_list.h"

RStdListValueResult r_std_list_remove(RStdList *target, void *element, void *result_storage) {
    RStdListValueResult result = {0};
    _Bool removed;

    removed = r_runtime_list_remove(target, element, result_storage);
    (void)removed;
    result.status = R_STD_LIST_CALL_SUCCESS;
    return result;
}

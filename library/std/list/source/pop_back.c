#include "r_std_list.h"

RStdListValueOptionResult r_std_list_pop_back(RStdList *target, void *result_storage) {
    RStdListValueOptionResult result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.has_value = r_runtime_list_pop_back(target, result_storage);
    return result;
}

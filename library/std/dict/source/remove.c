#include "r_std_dict.h"

RStdDictValueOptionResult
r_std_dict_remove(RStdDict *target, const void *key, void *result_storage) {
    RStdDictValueOptionResult result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.has_value = r_runtime_dict_remove(target, key, result_storage);
    return result;
}

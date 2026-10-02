#include "r_std_dict.h"

RStdDictBoolResult r_std_dict_contains(const RStdDict *source, const void *key) {
    RStdDictBoolResult result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.value = r_runtime_dict_contains(source, key);
    return result;
}

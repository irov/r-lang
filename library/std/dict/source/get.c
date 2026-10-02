#include "r_std_dict.h"

RStdDictConstPointerOption r_std_dict_get(const RStdDict *source, const void *key) {
    RStdDictConstPointerOption result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.value = r_runtime_dict_get(source, key);
    result.has_value = result.value != NULL;
    return result;
}

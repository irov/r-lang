#include "r_std_dict.h"

RStdDictMutPointerOption r_std_dict_get_mut(RStdDict *source, const void *key) {
    RStdDictMutPointerOption result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.value = r_runtime_dict_get_mut(source, key);
    result.has_value = result.value != NULL;
    return result;
}

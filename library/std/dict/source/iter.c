#include "r_std_dict.h"

RStdDictIteratorResult r_std_dict_iter(const RStdDict *source) {
    RStdDictIteratorResult result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.value = r_runtime_dict_iter(source);
    return result;
}

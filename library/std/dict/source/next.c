#include "r_std_dict.h"

RStdDictEntryOption r_std_dict_next(RStdDictIterator *iterator) {
    RStdDictEntryOption result = {0};

    result.status = R_STD_DICT_CALL_SUCCESS;
    result.has_value = r_runtime_dict_next(iterator, &result.value);
    return result;
}

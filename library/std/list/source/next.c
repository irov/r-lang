#include "r_std_list.h"

RStdListConstPointerOption r_std_list_next(RStdListIterator *iterator) {
    RStdListConstPointerOption result = {0};

    result.status = R_STD_LIST_CALL_SUCCESS;
    result.value = r_runtime_list_next(iterator);
    result.has_value = result.value != NULL;
    return result;
}

#include "r_std_async.h"

uint64_t r_std_async_task_id(void) {
    return r_runtime_task_current_id();
}

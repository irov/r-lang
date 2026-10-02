#include "r_std_async.h"

void r_std_async_detach(RRuntimeTask **operation) {
    r_runtime_task_detach(operation);
}

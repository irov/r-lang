#include "r_std_async.h"

void r_std_async_cancel(RRuntimeTask **operation) {
    r_runtime_task_cancel(operation);
}

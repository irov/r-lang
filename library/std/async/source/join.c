#include "r_std_async.h"
#include "r_std_thread.h"

void r_std_async_join(struct RStdThreadPanicReport *report) {
    r_library_internal_thread_panic_report_take(report);
}

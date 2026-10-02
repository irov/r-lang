#include "r_std_thread.h"

#include <stdint.h>
#include <string.h>

RStdStringView r_std_thread_panic_category(const RStdThreadPanicReport *report) {
    RStdStringView result;
    const char *name;

    name = r_runtime_panic_category_name(report->category);
    result.data = (const uint8_t *)name;
    result.length = strlen(name);
    return result;
}

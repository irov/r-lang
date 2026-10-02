#include "r_std_thread.h"

RStdStringView r_std_thread_panic_text(const RStdThreadPanicReport *report) {
    RStdStringView result;

    result.data = r_runtime_string_bytes(&report->text);
    result.length = r_runtime_string_length(&report->text);
    return result;
}

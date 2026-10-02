#include "r_std_c.h"

void r_std_c_detach_thread(RStdCThreadAttachment *attachment) {
    r_runtime_thread_detach(attachment);
}

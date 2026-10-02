#include "r_std_format.h"

void r_std_format_clear(RStdFormatBuilder *target) {
    r_std_string_clear(&target->output);
}

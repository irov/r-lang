#include "r_std_format.h"

RStdStringView r_std_format_as_str(const RStdFormatBuilder *source) {
    return r_std_string_as_str(&source->output);
}

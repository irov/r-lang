#include "r_std_format.h"

RStdString r_std_format_finish(RStdFormatBuilder *source) {
    RStdString result = source->output;
    source->output = (RStdString){0};
    return result;
}

#include "r_std_utf8.h"

_Bool r_std_utf8_is_valid(RStdUtf8View source) {
    return r_core_validate_utf8((RCoreByteSlice){source.data, source.length}).is_ok;
}

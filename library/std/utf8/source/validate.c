#include "r_std_utf8.h"

RCoreValidateUtf8Result r_std_utf8_validate(RStdUtf8View source) {
    return r_core_validate_utf8((RCoreByteSlice){source.data, source.length});
}

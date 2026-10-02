#include "r_std_bytes.h"

#include <string.h>

void r_std_bytes_fill(RStdBytesMutSlice destination, uint8_t value) {
    if (destination.length != 0U) {
        (void)memset(destination.data, (int)value, destination.length);
    }
}

#include "r_std_secret.h"

RStdSecretBuffer r_std_secret_from_bytes(RRuntimeArray *source) {
    RStdSecretBuffer buffer = *source;

    source->data = NULL;
    source->length = 0U;
    source->capacity = 0U;
    return buffer;
}

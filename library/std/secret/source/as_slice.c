#include "r_std_secret.h"

RStdSecretSlice r_std_secret_as_slice(const RStdSecretBuffer *source) {
    return (RStdSecretSlice){(const uint8_t *)source->data, source->length};
}

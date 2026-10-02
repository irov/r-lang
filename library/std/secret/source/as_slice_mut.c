#include "r_std_secret.h"

RStdSecretMutSlice r_std_secret_as_slice_mut(RStdSecretBuffer *source) {
    return (RStdSecretMutSlice){(uint8_t *)source->data, source->length};
}

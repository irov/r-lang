#include "r_std_secret.h"

void r_std_secret_zeroize(RStdSecretMutSlice target) {
    if (target.length != 0U) {
        r_std_secret_erase(target.data, target.length);
    }
}

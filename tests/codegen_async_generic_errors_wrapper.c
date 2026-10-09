#include "r_runtime_own.h"
#include <stdbool.h>
#include <stdint.h>

void r_test_own_release(RRuntimeOwn *owner);
#define r_runtime_own_release r_test_own_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release

static unsigned released;
static bool valid = true;
void r_test_own_release(RRuntimeOwn *owner) {
    unsigned bit = 0;
    if (owner->allocation != NULL) {
        switch (*(const int32_t *)owner->allocation) {
        case 11:
            bit = 1;
            break;
        case 13:
            bit = 2;
            break;
        case 17:
            bit = 4;
            break;
        case 19:
            bit = 8;
            break;
        default:
            break;
        }
    }
    if (bit == 0 || (released & bit) != 0) {
        valid = false;
    }
    released |= bit;
    r_runtime_own_release(owner);
}
int main(int argc, char *argv[]) {
    int status = r_generated_main(argc, argv);
    return status == 0 && valid && released == 15 ? 0 : 1;
}

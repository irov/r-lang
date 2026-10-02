#include "r_core.h"

#include <stdio.h>
#include <stdlib.h>

static void require(_Bool condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "core library test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    static const uint8_t valid[] = {
        UINT8_C(0x61),
        UINT8_C(0xc3),
        UINT8_C(0xa9),
        UINT8_C(0xf0),
        UINT8_C(0x9f),
        UINT8_C(0x98),
        UINT8_C(0x80),
    };
    static const uint8_t invalid[] = {
        UINT8_C(0x61),
        UINT8_C(0xe2),
        UINT8_C(0x28),
        UINT8_C(0xa1),
    };
    RCoreValidateUtf8Result result = r_core_validate_utf8((RCoreByteSlice){valid, sizeof(valid)});

    require(result.is_ok, "valid UTF-8 rejected");
    require((result.value.data == valid) && (result.value.length == sizeof(valid)),
            "successful conversion must preserve slice identity");
    result = r_core_validate_utf8((RCoreByteSlice){invalid, sizeof(invalid)});
    require(!result.is_ok && (result.error.index == 1U), "first invalid sequence index");
    result = r_core_validate_utf8((RCoreByteSlice){NULL, 0U});
    require(result.is_ok && (result.value.length == 0U), "null empty slice is valid");
    (void)fprintf(stdout, "library_core_tests: ok\n");
    return EXIT_SUCCESS;
}

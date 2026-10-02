#include <stdint.h>
#include <string.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static uint32_t r_test_f32_bits(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t r_test_f64_bits(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int main(void) {
    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000001(1)) != UINT32_C(0x3f800000)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000002(1)) != UINT32_C(0x3f800001)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000003(1)) != UINT32_C(0x3f800001)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000004(1)) != UINT32_C(0x00000001)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000005(1)) != UINT32_C(0x00000000)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000006(1)) != UINT32_C(0x80000000)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000007(1)) != UINT32_C(0x7f7fffff)) {
        return __LINE__;
    }
    if (r_test_f64_bits(r_f00000008(1)) != UINT64_C(0x3ff0000000000001)) {
        return __LINE__;
    }
    if (r_test_f64_bits(r_f00000009(1)) != UINT64_C(0x7fefffffffffffff)) {
        return __LINE__;
    }
    if (r_test_f64_bits(r_f00000010(1)) != UINT64_C(0x3ff8000000000000)) {
        return __LINE__;
    }
    if (r_test_f32_bits(r_f00000011(1)) != UINT32_C(0x41480000)) {
        return __LINE__;
    }
    return r_generated_main(0, NULL);
}

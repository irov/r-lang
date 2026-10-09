#include "r_runtime_0_1.h"

#include <stdint.h>
#include <string.h>

#define main r_generated_main
int main(int argc, char *argv[]);
/* The C exports of the fixture (@export_name): the literal of one of its functions, selected by
 * index in source order, in the C type of the same IEC 60559 format. */
float floatLiteral32(int index);
double floatLiteral64(int index);
#include R_TEST_PROGRAM_PRELUDE
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

/* R-TYPE-0006: decimal literals round to nearest, ties to even (a ties down to 1.0, b lies just
 * above the tie and rounds up), hexadecimal literals are exact, an underflow keeps its sign and
 * the largest finite values round to themselves. Comparing bits tells -0.0 from 0.0. */
static int r_test_check_literals(void) {
    static const uint32_t f32_expected[] = {
        UINT32_C(0x3f800000), /* a_half_even */
        UINT32_C(0x3f800001), /* b_half_up */
        UINT32_C(0x3f800001), /* c_hex_successor */
        UINT32_C(0x00000001), /* d_min_subnormal */
        UINT32_C(0x00000000), /* e_positive_underflow */
        UINT32_C(0x80000000), /* f_negative_underflow */
        UINT32_C(0x7f7fffff), /* g_max_f32 */
        UINT32_C(0x41480000), /* k_digit_separators */
    };
    static const uint64_t f64_expected[] = {
        UINT64_C(0x3ff0000000000001), /* h_hex_successor */
        UINT64_C(0x7fefffffffffffff), /* i_max_f64 */
        UINT64_C(0x3ff8000000000000), /* j_default_f64 */
    };
    int index;

    for (index = 0; index < (int)(sizeof(f32_expected) / sizeof(f32_expected[0])); ++index) {
        if (r_test_f32_bits(floatLiteral32(index)) != f32_expected[index]) {
            return 10 + index;
        }
    }
    for (index = 0; index < (int)(sizeof(f64_expected) / sizeof(f64_expected[0])); ++index) {
        if (r_test_f64_bits(floatLiteral64(index)) != f64_expected[index]) {
            return 20 + index;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const RRuntimeStartResult start = r_runtime_hosted_start(argc, argv);
    int status;

    if (!start.started) {
        return start.process_status;
    }
    status = r_runtime_hosted_finish(r_test_check_literals());
    return status == 0 ? r_generated_main(argc, argv) : status;
}

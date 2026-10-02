module test.codegen.bitwise_shift;

i32 main() {
    u16 small = 3;
    u16 combined = (small | (small << 4)) as u16;

    u32 bits = 0xf0f0_0f0f;
    u32 masked = bits & 0x00ff_00ff;
    masked ^= 0x000f_000f;
    masked >>= 4;

    i32 negative = -5;
    i32 arithmetic_right = negative >> 1;
    i32 negative_left = negative << 2;
    i32 positive = 1;
    i32 positive_left = positive << 5;
    u8 narrow = 3;
    narrow <<= 1usize;
    u64 mixed = 10;
    u32 increment = 7;
    mixed += increment;

    if (combined == 51) {
        if (masked == 0x000f_f000) {
            if (arithmetic_right == -3) {
                if (negative_left == -20) {
                    if (positive_left == 32) {
                        if (narrow == 6) {
                            if (mixed == 17u64) {
                                return 0;
                            } else {
                                return 7;
                            }
                        } else {
                            return 6;
                        }
                    } else {
                        return 5;
                    }
                } else {
                    return 4;
                }
            } else {
                return 3;
            }
        } else {
            return 2;
        }
    } else {
        return 1;
    }
}

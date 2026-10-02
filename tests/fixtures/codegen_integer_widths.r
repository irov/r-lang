module test.codegen.integer_widths;

i32 main() {
    i64 signed_wide = 7;
    i64 signed_wide_2 = signed_wide * 6;
    signed_wide_2 -= 2;

    u64 unsigned_wide = 18446744073709551615;
    unsigned_wide += 2;

    isize signed_size = 9;
    isize signed_size_2 = signed_size / 3;

    usize unsigned_size = 12;
    usize unsigned_size_2 = unsigned_size - 5;

    u8 wrapped = 255;
    wrapped += 1;

    i8 checked = 120;
    checked += 7;

    u16 cast_source = 40;
    u8 narrowed = cast_source as u8;
    u16 widened = narrowed as u16;
    u8 incremented = (narrowed + 1) as u8;

    if (signed_wide_2 == 40) {
        if (unsigned_wide == 1) {
            if (signed_size_2 == 3) {
                if (unsigned_size_2 == 7) {
                    if (wrapped == 0) {
                        if (checked == 127) {
                            if (widened == 40) {
                                if (incremented == 41) {
                                    return 0;
                                } else {
                                    return 8;
                                }
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

// Exhaustive frontend and C17 audit for the closed std.convert parse family.
module audit.async_std_convert_parse_all;

async i8 probe_000() throws std.convert::parse_error {
    return std.convert::parse_i8("0", 10);
}

async u8 probe_001() throws std.convert::parse_error {
    return std.convert::parse_u8("0", 10);
}

async i16 probe_002() throws std.convert::parse_error {
    return std.convert::parse_i16("0", 10);
}

async u16 probe_003() throws std.convert::parse_error {
    return std.convert::parse_u16("0", 10);
}

async i32 probe_004() throws std.convert::parse_error {
    return std.convert::parse_i32("0", 10);
}

async u32 probe_005() throws std.convert::parse_error {
    return std.convert::parse_u32("0", 10);
}

async i64 probe_006() throws std.convert::parse_error {
    return std.convert::parse_i64("0", 10);
}

async u64 probe_007() throws std.convert::parse_error {
    return std.convert::parse_u64("0", 10);
}

async isize probe_008() throws std.convert::parse_error {
    return std.convert::parse_isize("0", 10);
}

async usize probe_009() throws std.convert::parse_error {
    return std.convert::parse_usize("0", 10);
}

async c_char probe_010() throws std.convert::parse_error {
    return std.convert::parse_c_char("0", 10);
}

async c_schar probe_011() throws std.convert::parse_error {
    return std.convert::parse_c_schar("0", 10);
}

async c_uchar probe_012() throws std.convert::parse_error {
    return std.convert::parse_c_uchar("0", 10);
}

async c_short probe_013() throws std.convert::parse_error {
    return std.convert::parse_c_short("0", 10);
}

async c_ushort probe_014() throws std.convert::parse_error {
    return std.convert::parse_c_ushort("0", 10);
}

async c_int probe_015() throws std.convert::parse_error {
    return std.convert::parse_c_int("0", 10);
}

async c_uint probe_016() throws std.convert::parse_error {
    return std.convert::parse_c_uint("0", 10);
}

async c_long probe_017() throws std.convert::parse_error {
    return std.convert::parse_c_long("0", 10);
}

async c_ulong probe_018() throws std.convert::parse_error {
    return std.convert::parse_c_ulong("0", 10);
}

async c_llong probe_019() throws std.convert::parse_error {
    return std.convert::parse_c_llong("0", 10);
}

async c_ullong probe_020() throws std.convert::parse_error {
    return std.convert::parse_c_ullong("0", 10);
}

async c_bool probe_021() throws std.convert::parse_error {
    return std.convert::parse_c_bool("0", 10);
}

async c_wchar probe_022() throws std.convert::parse_error {
    return std.convert::parse_c_wchar("0", 10);
}

async c_wint probe_023() throws std.convert::parse_error {
    return std.convert::parse_c_wint("0", 10);
}

async c_int8 probe_024() throws std.convert::parse_error {
    return std.convert::parse_c_int8("0", 10);
}

async c_uint8 probe_025() throws std.convert::parse_error {
    return std.convert::parse_c_uint8("0", 10);
}

async c_int16 probe_026() throws std.convert::parse_error {
    return std.convert::parse_c_int16("0", 10);
}

async c_uint16 probe_027() throws std.convert::parse_error {
    return std.convert::parse_c_uint16("0", 10);
}

async c_int32 probe_028() throws std.convert::parse_error {
    return std.convert::parse_c_int32("0", 10);
}

async c_uint32 probe_029() throws std.convert::parse_error {
    return std.convert::parse_c_uint32("0", 10);
}

async c_int64 probe_030() throws std.convert::parse_error {
    return std.convert::parse_c_int64("0", 10);
}

async c_uint64 probe_031() throws std.convert::parse_error {
    return std.convert::parse_c_uint64("0", 10);
}

async c_intptr probe_032() throws std.convert::parse_error {
    return std.convert::parse_c_intptr("0", 10);
}

async c_uintptr probe_033() throws std.convert::parse_error {
    return std.convert::parse_c_uintptr("0", 10);
}

async c_intmax probe_034() throws std.convert::parse_error {
    return std.convert::parse_c_intmax("0", 10);
}

async c_uintmax probe_035() throws std.convert::parse_error {
    return std.convert::parse_c_uintmax("0", 10);
}

async c_size probe_036() throws std.convert::parse_error {
    return std.convert::parse_c_size("0", 10);
}

async c_ptrdiff probe_037() throws std.convert::parse_error {
    return std.convert::parse_c_ptrdiff("0", 10);
}

async f32 probe_038() throws std.convert::parse_error {
    return std.convert::parse_f32("0");
}

async f64 probe_039() throws std.convert::parse_error {
    return std.convert::parse_f64("0");
}

async c_float probe_040() throws std.convert::parse_error {
    return std.convert::parse_c_float("0");
}

async c_double probe_041() throws std.convert::parse_error {
    return std.convert::parse_c_double("0");
}

async c_long_double probe_042() throws std.convert::parse_error {
    return std.convert::parse_c_long_double("0");
}

protected std.convert::parse_error_code expected_above_maximum() {
    return std.convert::parse_error_code::above_maximum;
}

async i32 main() {
    try {
        i8 minimum = std.convert::parse_i8("-128", 10);
        if (minimum != -128i8) {
            return 1;
        }

        u64 maximum = std.convert::parse_u64("18446744073709551615", 10);
        if (maximum != 18446744073709551615u64) {
            return 2;
        }

        i32 hexadecimal = std.convert::parse_i32("0x2a", 0);
        if (hexadecimal != 42) {
            return 3;
        }

        c_bool flag = std.convert::parse_c_bool("1", 10);
        flag as void;

        f64 fraction = std.convert::parse_f64("1.5");
        if (fraction != 1.5f64) {
            return 5;
        }

        try {
            u8 too_large = std.convert::parse_u8("256", 10);
            too_large as void;
            return 6;
        } catch (std.convert::parse_error failure) {
            if (failure.code != expected_above_maximum()) {
                return 7;
            }
            if (failure.index != 3usize) {
                return 8;
            }
        }
        return 0;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 9;
    }
}

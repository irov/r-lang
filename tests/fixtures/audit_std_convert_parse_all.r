// Exhaustive frontend and C17 audit for the closed std.convert parse family.
module audit.std_convert_parse_all;

i8 probe_000(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_i8(source, radix);
}

u8 probe_001(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_u8(source, radix);
}

i16 probe_002(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_i16(source, radix);
}

u16 probe_003(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_u16(source, radix);
}

i32 probe_004(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_i32(source, radix);
}

u32 probe_005(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_u32(source, radix);
}

i64 probe_006(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_i64(source, radix);
}

u64 probe_007(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_u64(source, radix);
}

isize probe_008(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_isize(source, radix);
}

usize probe_009(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_usize(source, radix);
}

c_char probe_010(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_char(source, radix);
}

c_schar probe_011(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_schar(source, radix);
}

c_uchar probe_012(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uchar(source, radix);
}

c_short probe_013(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_short(source, radix);
}

c_ushort probe_014(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_ushort(source, radix);
}

c_int probe_015(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_int(source, radix);
}

c_uint probe_016(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uint(source, radix);
}

c_long probe_017(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_long(source, radix);
}

c_ulong probe_018(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_ulong(source, radix);
}

c_llong probe_019(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_llong(source, radix);
}

c_ullong probe_020(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_ullong(source, radix);
}

c_bool probe_021(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_bool(source, radix);
}

c_wchar probe_022(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_wchar(source, radix);
}

c_wint probe_023(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_wint(source, radix);
}

c_int8 probe_024(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_int8(source, radix);
}

c_uint8 probe_025(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uint8(source, radix);
}

c_int16 probe_026(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_int16(source, radix);
}

c_uint16 probe_027(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uint16(source, radix);
}

c_int32 probe_028(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_int32(source, radix);
}

c_uint32 probe_029(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uint32(source, radix);
}

c_int64 probe_030(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_int64(source, radix);
}

c_uint64 probe_031(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uint64(source, radix);
}

c_intptr probe_032(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_intptr(source, radix);
}

c_uintptr probe_033(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uintptr(source, radix);
}

c_intmax probe_034(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_intmax(source, radix);
}

c_uintmax probe_035(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_uintmax(source, radix);
}

c_size probe_036(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_size(source, radix);
}

c_ptrdiff probe_037(str source, u32 radix) throws std.convert::parse_error {
    return std.convert::parse_c_ptrdiff(source, radix);
}

f32 probe_038(str source) throws std.convert::parse_error {
    return std.convert::parse_f32(source);
}

f64 probe_039(str source) throws std.convert::parse_error {
    return std.convert::parse_f64(source);
}

c_float probe_040(str source) throws std.convert::parse_error {
    return std.convert::parse_c_float(source);
}

c_double probe_041(str source) throws std.convert::parse_error {
    return std.convert::parse_c_double(source);
}

c_long_double probe_042(str source) throws std.convert::parse_error {
    return std.convert::parse_c_long_double(source);
}

protected std.convert::parse_error_code expected_above_maximum() {
    return std.convert::parse_error_code::above_maximum;
}

i32 main() {
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

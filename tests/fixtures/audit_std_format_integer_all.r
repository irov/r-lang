// Exhaustive frontend, runtime, and C17 audit for the closed integer append family.
module audit.std_format_integer_all;

usize exercise_sync() throws std.convert::parse_error, std.format::format_error {
    std.format::builder builder = std.format::create();
    i8 value_000 = std.convert::parse_i8("1", 10);
    std.format::append_i8(&builder, value_000, 10);
    u8 value_001 = std.convert::parse_u8("1", 10);
    std.format::append_u8(&builder, value_001, 10);
    i16 value_002 = std.convert::parse_i16("1", 10);
    std.format::append_i16(&builder, value_002, 10);
    u16 value_003 = std.convert::parse_u16("1", 10);
    std.format::append_u16(&builder, value_003, 10);
    i32 value_004 = std.convert::parse_i32("1", 10);
    std.format::append_i32(&builder, value_004, 10);
    u32 value_005 = std.convert::parse_u32("1", 10);
    std.format::append_u32(&builder, value_005, 10);
    i64 value_006 = std.convert::parse_i64("1", 10);
    std.format::append_i64(&builder, value_006, 10);
    u64 value_007 = std.convert::parse_u64("1", 10);
    std.format::append_u64(&builder, value_007, 10);
    isize value_008 = std.convert::parse_isize("1", 10);
    std.format::append_isize(&builder, value_008, 10);
    usize value_009 = std.convert::parse_usize("1", 10);
    std.format::append_usize(&builder, value_009, 10);
    c_char value_010 = std.convert::parse_c_char("1", 10);
    std.format::append_c_char(&builder, value_010, 10);
    c_schar value_011 = std.convert::parse_c_schar("1", 10);
    std.format::append_c_schar(&builder, value_011, 10);
    c_uchar value_012 = std.convert::parse_c_uchar("1", 10);
    std.format::append_c_uchar(&builder, value_012, 10);
    c_short value_013 = std.convert::parse_c_short("1", 10);
    std.format::append_c_short(&builder, value_013, 10);
    c_ushort value_014 = std.convert::parse_c_ushort("1", 10);
    std.format::append_c_ushort(&builder, value_014, 10);
    c_int value_015 = std.convert::parse_c_int("1", 10);
    std.format::append_c_int(&builder, value_015, 10);
    c_uint value_016 = std.convert::parse_c_uint("1", 10);
    std.format::append_c_uint(&builder, value_016, 10);
    c_long value_017 = std.convert::parse_c_long("1", 10);
    std.format::append_c_long(&builder, value_017, 10);
    c_ulong value_018 = std.convert::parse_c_ulong("1", 10);
    std.format::append_c_ulong(&builder, value_018, 10);
    c_llong value_019 = std.convert::parse_c_llong("1", 10);
    std.format::append_c_llong(&builder, value_019, 10);
    c_ullong value_020 = std.convert::parse_c_ullong("1", 10);
    std.format::append_c_ullong(&builder, value_020, 10);
    c_bool value_021 = std.convert::parse_c_bool("1", 10);
    std.format::append_c_bool(&builder, value_021, 10);
    c_wchar value_022 = std.convert::parse_c_wchar("1", 10);
    std.format::append_c_wchar(&builder, value_022, 10);
    c_wint value_023 = std.convert::parse_c_wint("1", 10);
    std.format::append_c_wint(&builder, value_023, 10);
    c_int8 value_024 = std.convert::parse_c_int8("1", 10);
    std.format::append_c_int8(&builder, value_024, 10);
    c_uint8 value_025 = std.convert::parse_c_uint8("1", 10);
    std.format::append_c_uint8(&builder, value_025, 10);
    c_int16 value_026 = std.convert::parse_c_int16("1", 10);
    std.format::append_c_int16(&builder, value_026, 10);
    c_uint16 value_027 = std.convert::parse_c_uint16("1", 10);
    std.format::append_c_uint16(&builder, value_027, 10);
    c_int32 value_028 = std.convert::parse_c_int32("1", 10);
    std.format::append_c_int32(&builder, value_028, 10);
    c_uint32 value_029 = std.convert::parse_c_uint32("1", 10);
    std.format::append_c_uint32(&builder, value_029, 10);
    c_int64 value_030 = std.convert::parse_c_int64("1", 10);
    std.format::append_c_int64(&builder, value_030, 10);
    c_uint64 value_031 = std.convert::parse_c_uint64("1", 10);
    std.format::append_c_uint64(&builder, value_031, 10);
    c_intptr value_032 = std.convert::parse_c_intptr("1", 10);
    std.format::append_c_intptr(&builder, value_032, 10);
    c_uintptr value_033 = std.convert::parse_c_uintptr("1", 10);
    std.format::append_c_uintptr(&builder, value_033, 10);
    c_intmax value_034 = std.convert::parse_c_intmax("1", 10);
    std.format::append_c_intmax(&builder, value_034, 10);
    c_uintmax value_035 = std.convert::parse_c_uintmax("1", 10);
    std.format::append_c_uintmax(&builder, value_035, 10);
    c_size value_036 = std.convert::parse_c_size("1", 10);
    std.format::append_c_size(&builder, value_036, 10);
    c_ptrdiff value_037 = std.convert::parse_c_ptrdiff("1", 10);
    std.format::append_c_ptrdiff(&builder, value_037, 10);
    str view = std.format::as_str(&builder);
    usize length = len(view);
    view as void;
    drop builder;
    return length;
}

bool invalid_radix_preserves_builder() {
    std.format::builder builder = std.format::create();
    try {
        std.format::append_str(&builder, "ok");
        str before_view = std.format::as_str(&builder);
        usize before = len(before_view);
        before_view as void;
        std.format::append_i32(&builder, 7, 1);
        before as void;
        drop builder;
        return false;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        drop builder;
        return false;
    } catch (std.format::format_error failure) {
        failure as void;
        str after_view = std.format::as_str(&builder);
        usize after = len(after_view);
        after_view as void;
        drop builder;
        return after == 2usize;
    }
}

async i32 main() {
    try {
        if (exercise_sync() != 38usize) {
            return 1;
        }
        if (invalid_radix_preserves_builder() == false) {
            return 2;
        }

        std.format::builder builder = std.format::create();
        i8 value_000 = std.convert::parse_i8("1", 10);
        std.format::append_i8(&builder, value_000, 10);
        u8 value_001 = std.convert::parse_u8("1", 10);
        std.format::append_u8(&builder, value_001, 10);
        i16 value_002 = std.convert::parse_i16("1", 10);
        std.format::append_i16(&builder, value_002, 10);
        u16 value_003 = std.convert::parse_u16("1", 10);
        std.format::append_u16(&builder, value_003, 10);
        i32 value_004 = std.convert::parse_i32("1", 10);
        std.format::append_i32(&builder, value_004, 10);
        u32 value_005 = std.convert::parse_u32("1", 10);
        std.format::append_u32(&builder, value_005, 10);
        i64 value_006 = std.convert::parse_i64("1", 10);
        std.format::append_i64(&builder, value_006, 10);
        u64 value_007 = std.convert::parse_u64("1", 10);
        std.format::append_u64(&builder, value_007, 10);
        isize value_008 = std.convert::parse_isize("1", 10);
        std.format::append_isize(&builder, value_008, 10);
        usize value_009 = std.convert::parse_usize("1", 10);
        std.format::append_usize(&builder, value_009, 10);
        c_char value_010 = std.convert::parse_c_char("1", 10);
        std.format::append_c_char(&builder, value_010, 10);
        c_schar value_011 = std.convert::parse_c_schar("1", 10);
        std.format::append_c_schar(&builder, value_011, 10);
        c_uchar value_012 = std.convert::parse_c_uchar("1", 10);
        std.format::append_c_uchar(&builder, value_012, 10);
        c_short value_013 = std.convert::parse_c_short("1", 10);
        std.format::append_c_short(&builder, value_013, 10);
        c_ushort value_014 = std.convert::parse_c_ushort("1", 10);
        std.format::append_c_ushort(&builder, value_014, 10);
        c_int value_015 = std.convert::parse_c_int("1", 10);
        std.format::append_c_int(&builder, value_015, 10);
        c_uint value_016 = std.convert::parse_c_uint("1", 10);
        std.format::append_c_uint(&builder, value_016, 10);
        c_long value_017 = std.convert::parse_c_long("1", 10);
        std.format::append_c_long(&builder, value_017, 10);
        c_ulong value_018 = std.convert::parse_c_ulong("1", 10);
        std.format::append_c_ulong(&builder, value_018, 10);
        c_llong value_019 = std.convert::parse_c_llong("1", 10);
        std.format::append_c_llong(&builder, value_019, 10);
        c_ullong value_020 = std.convert::parse_c_ullong("1", 10);
        std.format::append_c_ullong(&builder, value_020, 10);
        c_bool value_021 = std.convert::parse_c_bool("1", 10);
        std.format::append_c_bool(&builder, value_021, 10);
        c_wchar value_022 = std.convert::parse_c_wchar("1", 10);
        std.format::append_c_wchar(&builder, value_022, 10);
        c_wint value_023 = std.convert::parse_c_wint("1", 10);
        std.format::append_c_wint(&builder, value_023, 10);
        c_int8 value_024 = std.convert::parse_c_int8("1", 10);
        std.format::append_c_int8(&builder, value_024, 10);
        c_uint8 value_025 = std.convert::parse_c_uint8("1", 10);
        std.format::append_c_uint8(&builder, value_025, 10);
        c_int16 value_026 = std.convert::parse_c_int16("1", 10);
        std.format::append_c_int16(&builder, value_026, 10);
        c_uint16 value_027 = std.convert::parse_c_uint16("1", 10);
        std.format::append_c_uint16(&builder, value_027, 10);
        c_int32 value_028 = std.convert::parse_c_int32("1", 10);
        std.format::append_c_int32(&builder, value_028, 10);
        c_uint32 value_029 = std.convert::parse_c_uint32("1", 10);
        std.format::append_c_uint32(&builder, value_029, 10);
        c_int64 value_030 = std.convert::parse_c_int64("1", 10);
        std.format::append_c_int64(&builder, value_030, 10);
        c_uint64 value_031 = std.convert::parse_c_uint64("1", 10);
        std.format::append_c_uint64(&builder, value_031, 10);
        c_intptr value_032 = std.convert::parse_c_intptr("1", 10);
        std.format::append_c_intptr(&builder, value_032, 10);
        c_uintptr value_033 = std.convert::parse_c_uintptr("1", 10);
        std.format::append_c_uintptr(&builder, value_033, 10);
        c_intmax value_034 = std.convert::parse_c_intmax("1", 10);
        std.format::append_c_intmax(&builder, value_034, 10);
        c_uintmax value_035 = std.convert::parse_c_uintmax("1", 10);
        std.format::append_c_uintmax(&builder, value_035, 10);
        c_size value_036 = std.convert::parse_c_size("1", 10);
        std.format::append_c_size(&builder, value_036, 10);
        c_ptrdiff value_037 = std.convert::parse_c_ptrdiff("1", 10);
        std.format::append_c_ptrdiff(&builder, value_037, 10);
        str view = std.format::as_str(&builder);
        usize length = len(view);
        view as void;
        drop builder;
        i32 selected = length == 38usize ? 0 : 3;
        return selected;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 4;
    } catch (std.format::format_error failure) {
        failure as void;
        return 5;
    }
}

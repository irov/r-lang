module test.codegen.library_uuid;

import std.cmp;
import std.uuid;

// R-SLIB-UUID-0001..0003 (M20): the version 4 and 7 examples of RFC 9562 appendix A, the text
// form, parsing errors, ordering and dictionary keys.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 rejects(str text, usize index, std.convert::parse_error_code code) {
    try {
        std.uuid::parse(text) as void;
        return 1;
    } catch (std.convert::parse_error failure) {
        if (failure.index != index || failure.code != code) { return 2; }
        return 0;
    }
}

protected i32 check() throws std.alloc::alloc_error, std.convert::parse_error, std.time::time_error {
    u8[16] random = {0x91u8, 0x91u8, 0x08u8, 0xf7u8, 0x52u8, 0xd1u8, 0x03u8, 0x20u8,
                     0x1bu8, 0xacu8, 0xf8u8, 0x47u8, 0xdbu8, 0x41u8, 0x48u8, 0xa8u8};
    std.uuid::uuid four = std.uuid::from_random_v4(random);
    std.string::string four_text = f"{four}";
    if (same(four_text.as_str(), "919108f7-52d1-4320-9bac-f847db4148a8") == false) { return 1; }
    if (std.uuid::version(&four) != 4u8) { return 2; }
    u8[10] tail = {0x0cu8, 0xc3u8, 0x18u8, 0xc4u8, 0xdcu8, 0x0cu8, 0x0cu8, 0x07u8, 0x39u8, 0x8fu8};
    std.uuid::uuid seven = std.uuid::from_time_v7(0x017f22e279b0u64, tail);
    std.string::string seven_text = f"{seven}";
    if (same(seven_text.as_str(), "017f22e2-79b0-7cc3-98c4-dc0c0c07398f") == false) { return 3; }
    std.uuid::uuid parsed = std.uuid::parse("017F22E2-79B0-7CC3-98C4-DC0C0C07398F");
    if (std.cmp::is_equal(&parsed, &seven) == false) { return 4; }
    std.uuid::uuid nil = std.uuid::nil();
    std.uuid::uuid max = std.uuid::max();
    std.string::string nil_text = f"{nil}";
    std.string::string max_text = f"{max}";
    i32 status = 0;
    if (same(nil_text.as_str(), "00000000-0000-0000-0000-000000000000") == false) { status = 5; }
    if (same(max_text.as_str(), "ffffffff-ffff-ffff-ffff-ffffffffffff") == false) { status = 6; }
    if (status != 0) { return status; }
    if (std.cmp::is_less(&nil, &seven) == false || std.cmp::is_less(&seven, &max) == false) {
        return 7;
    }
    std.uuid::uuid fresh = std.uuid::v4();
    if (std.uuid::version(&fresh) != 4u8 || (fresh.bytes[8] & 0xc0u8) != 0x80u8) { return 8; }
    std.uuid::uuid timed = std.uuid::v7();
    if (std.uuid::version(&timed) != 7u8 || (timed.bytes[8] & 0xc0u8) != 0x80u8) { return 9; }
    if (std.cmp::is_less(&seven, &timed) == false) { return 10; }
    std.uuid::uuid later = std.uuid::from_time_v7(0x017f22e279b1u64, tail);
    if (std.cmp::is_less(&seven, &later) == false) { return 11; }
    dict<std.uuid::uuid, i32> names = std.dict::create::<std.uuid::uuid, i32>();
    try {
        names.insert(seven, 7) as void;
        names.insert(four, 4) as void;
    } catch (std.dict::insert_error<std.uuid::uuid, i32> failure) {
        failure as void;
        return 30;
    }
    o<const i32*> found = names.get(&parsed);
    switch (found) {
    case variant o::some(value): if (**value != 7) { return 12; }
    case variant o::none: return 13;
    }
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    if (rejects("", 0usize, std.convert::parse_error_code::empty) != 0) { return 14; }
    if (rejects("017f22e2-79b0-7cc3-98c4-dc0c0c07398", 35usize, digit) != 0) { return 15; }
    if (rejects("017f22e2-79b0-7cc3-98c4-dc0c0c07398f0", 36usize,
                std.convert::parse_error_code::trailing_character) != 0) { return 16; }
    if (rejects("017f22e2_79b0-7cc3-98c4-dc0c0c07398f", 8usize, digit) != 0) { return 17; }
    if (rejects("017f22g2-79b0-7cc3-98c4-dc0c0c07398f", 6usize, digit) != 0) { return 18; }
    if (rejects("017f22e2-79b0-7cc3-98c4-dc0c0c07398fx", 36usize,
                std.convert::parse_error_code::trailing_character) != 0) { return 19; }
    if (rejects("017f", 4usize, digit) != 0) { return 20; }
    return 0;
}

i32 main() {
    try {
        return check();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 97;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 98;
    } catch (std.time::time_error failure) {
        failure as void;
        return 99;
    }
}

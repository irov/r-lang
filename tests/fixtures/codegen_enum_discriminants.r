module test.codegen.enum_discriminants;
enum Small : i8 { Min = -128, Next, Zero = 0, Max = 127, };
enum Big : u64 { Zero, Max = 18446744073709551615, Half = (1u64 << 63usize), };
enum Signed : i64 { Min = -9223372036854775808, After, Max = 9223372036854775807, };
enum C : c_long { A = -9223372036854775808, B, C = 9223372036854775807, };
enum Calc : i16 { A = 2 * 3, B = (1 << 10), C = -3, D, };
enum Repr_i8 : i8 { A = 1, B = 7, C, };
enum Repr_u8 : u8 { A = 1, B = 7, C, };
enum Repr_i16 : i16 { A = 1, B = 7, C, };
enum Repr_u16 : u16 { A = 1, B = 7, C, };
enum Repr_i32 : i32 { A = 1, B = 7, C, };
enum Repr_u32 : u32 { A = 1, B = 7, C, };
enum Repr_i64 : i64 { A = 1, B = 7, C, };
enum Repr_u64 : u64 { A = 1, B = 7, C, };
enum Repr_isize : isize { A = 1, B = 7, C, };
enum Repr_usize : usize { A = 1, B = 7, C, };
enum Repr_c_char : c_char { A = 1, B = 7, C, };
enum Repr_c_schar : c_schar { A = 1, B = 7, C, };
enum Repr_c_uchar : c_uchar { A = 1, B = 7, C, };
enum Repr_c_short : c_short { A = 1, B = 7, C, };
enum Repr_c_ushort : c_ushort { A = 1, B = 7, C, };
enum Repr_c_int : c_int { A = 1, B = 7, C, };
enum Repr_c_uint : c_uint { A = 1, B = 7, C, };
enum Repr_c_long : c_long { A = 1, B = 7, C, };
enum Repr_c_ulong : c_ulong { A = 1, B = 7, C, };
enum Repr_c_llong : c_llong { A = 1, B = 7, C, };
enum Repr_c_ullong : c_ullong { A = 1, B = 7, C, };
enum Repr_c_wchar : c_wchar { A = 1, B = 7, C, };
enum Repr_c_wint : c_wint { A = 1, B = 7, C, };
enum Repr_c_int8 : c_int8 { A = 1, B = 7, C, };
enum Repr_c_uint8 : c_uint8 { A = 1, B = 7, C, };
enum Repr_c_int16 : c_int16 { A = 1, B = 7, C, };
enum Repr_c_uint16 : c_uint16 { A = 1, B = 7, C, };
enum Repr_c_int32 : c_int32 { A = 1, B = 7, C, };
enum Repr_c_uint32 : c_uint32 { A = 1, B = 7, C, };
enum Repr_c_int64 : c_int64 { A = 1, B = 7, C, };
enum Repr_c_uint64 : c_uint64 { A = 1, B = 7, C, };
enum Repr_c_intptr : c_intptr { A = 1, B = 7, C, };
enum Repr_c_uintptr : c_uintptr { A = 1, B = 7, C, };
enum Repr_c_intmax : c_intmax { A = 1, B = 7, C, };
enum Repr_c_uintmax : c_uintmax { A = 1, B = 7, C, };

protected i32 select(Small value) {
    switch(value) {
    case Small::Min: return 1;
    case Small::Next: return 2;
    case Small::Zero: return 3;
    case Small::Max: return 4;
    }
}
protected i32 exercise() {
    Small small = Small::Min;
    Big big = Big::Max;
    Signed wide = Signed::Min;
    C c_value = C::B;
    Calc calc = Calc::B;
    if (small != Small::Min || big != Big::Max || wide != Signed::Min ||
        c_value != C::B || calc != Calc::B) { return 1; }
    i32 first = select(Small::Min);
    i32 second = select(Small::Next);
    second as void;
    i32 third = select(Small::Zero);
    third as void;
    i32 fourth = select(Small::Max);
    fourth as void;
    if (first != 1 || second != 2 || third != 3 || fourth != 4) { return 2; }
    Repr_i8 r_i8 = Repr_i8::B;
    if (r_i8 != Repr_i8::B) { return 10; }
    Repr_u8 r_u8 = Repr_u8::B;
    if (r_u8 != Repr_u8::B) { return 11; }
    Repr_i16 r_i16 = Repr_i16::B;
    if (r_i16 != Repr_i16::B) { return 12; }
    Repr_u16 r_u16 = Repr_u16::B;
    if (r_u16 != Repr_u16::B) { return 13; }
    Repr_i32 r_i32 = Repr_i32::B;
    if (r_i32 != Repr_i32::B) { return 14; }
    Repr_u32 r_u32 = Repr_u32::B;
    if (r_u32 != Repr_u32::B) { return 15; }
    Repr_i64 r_i64 = Repr_i64::B;
    if (r_i64 != Repr_i64::B) { return 16; }
    Repr_u64 r_u64 = Repr_u64::B;
    if (r_u64 != Repr_u64::B) { return 17; }
    Repr_isize r_isize = Repr_isize::B;
    if (r_isize != Repr_isize::B) { return 18; }
    Repr_usize r_usize = Repr_usize::B;
    if (r_usize != Repr_usize::B) { return 19; }
    Repr_c_char r_c_char = Repr_c_char::B;
    if (r_c_char != Repr_c_char::B) { return 20; }
    Repr_c_schar r_c_schar = Repr_c_schar::B;
    if (r_c_schar != Repr_c_schar::B) { return 21; }
    Repr_c_uchar r_c_uchar = Repr_c_uchar::B;
    if (r_c_uchar != Repr_c_uchar::B) { return 22; }
    Repr_c_short r_c_short = Repr_c_short::B;
    if (r_c_short != Repr_c_short::B) { return 23; }
    Repr_c_ushort r_c_ushort = Repr_c_ushort::B;
    if (r_c_ushort != Repr_c_ushort::B) { return 24; }
    Repr_c_int r_c_int = Repr_c_int::B;
    if (r_c_int != Repr_c_int::B) { return 25; }
    Repr_c_uint r_c_uint = Repr_c_uint::B;
    if (r_c_uint != Repr_c_uint::B) { return 26; }
    Repr_c_long r_c_long = Repr_c_long::B;
    if (r_c_long != Repr_c_long::B) { return 27; }
    Repr_c_ulong r_c_ulong = Repr_c_ulong::B;
    if (r_c_ulong != Repr_c_ulong::B) { return 28; }
    Repr_c_llong r_c_llong = Repr_c_llong::B;
    if (r_c_llong != Repr_c_llong::B) { return 29; }
    Repr_c_ullong r_c_ullong = Repr_c_ullong::B;
    if (r_c_ullong != Repr_c_ullong::B) { return 30; }
    Repr_c_wchar r_c_wchar = Repr_c_wchar::B;
    if (r_c_wchar != Repr_c_wchar::B) { return 31; }
    Repr_c_wint r_c_wint = Repr_c_wint::B;
    if (r_c_wint != Repr_c_wint::B) { return 32; }
    Repr_c_int8 r_c_int8 = Repr_c_int8::B;
    if (r_c_int8 != Repr_c_int8::B) { return 33; }
    Repr_c_uint8 r_c_uint8 = Repr_c_uint8::B;
    if (r_c_uint8 != Repr_c_uint8::B) { return 34; }
    Repr_c_int16 r_c_int16 = Repr_c_int16::B;
    if (r_c_int16 != Repr_c_int16::B) { return 35; }
    Repr_c_uint16 r_c_uint16 = Repr_c_uint16::B;
    if (r_c_uint16 != Repr_c_uint16::B) { return 36; }
    Repr_c_int32 r_c_int32 = Repr_c_int32::B;
    if (r_c_int32 != Repr_c_int32::B) { return 37; }
    Repr_c_uint32 r_c_uint32 = Repr_c_uint32::B;
    if (r_c_uint32 != Repr_c_uint32::B) { return 38; }
    Repr_c_int64 r_c_int64 = Repr_c_int64::B;
    if (r_c_int64 != Repr_c_int64::B) { return 39; }
    Repr_c_uint64 r_c_uint64 = Repr_c_uint64::B;
    if (r_c_uint64 != Repr_c_uint64::B) { return 40; }
    Repr_c_intptr r_c_intptr = Repr_c_intptr::B;
    if (r_c_intptr != Repr_c_intptr::B) { return 41; }
    Repr_c_uintptr r_c_uintptr = Repr_c_uintptr::B;
    if (r_c_uintptr != Repr_c_uintptr::B) { return 42; }
    Repr_c_intmax r_c_intmax = Repr_c_intmax::B;
    if (r_c_intmax != Repr_c_intmax::B) { return 43; }
    Repr_c_uintmax r_c_uintmax = Repr_c_uintmax::B;
    if (r_c_uintmax != Repr_c_uintmax::B) { return 44; }
    switch(big) {
    case Big::Zero: return 3;
    case Big::Max: break;
    case Big::Half: return 4;
    }
    switch(wide) {
    case Signed::Min: return 0;
    default: return 5;
    }
}

i32 main() { i32 outcome = exercise(); return outcome; }

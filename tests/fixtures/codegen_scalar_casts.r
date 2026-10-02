module test.codegen.scalar_casts;

/* R-EXPR-0016: char converts with integers as a Unicode scalar value. */
/* R-EXPR-0018: a fieldless @repr(C) enum converts with integers through its discriminant. */
@repr(C) enum Kind : c_int { Zero, One = 5, Two, };
@repr(C) enum Small : c_uchar { A, B = 200, };

i32 main() {
    char letter = 'a';
    u32 code = letter as u32;
    if (code != 97) {
        return 1;
    }
    u8 narrow = letter as u8;
    if (narrow != 97) {
        return 2;
    }
    i64 wide = 128512;
    char emoji = wide as char;
    u32 emoji_code = emoji as u32;
    if (emoji_code != 128512) {
        return 3;
    }
    Kind kind = Kind::Two;
    i32 number = kind as i32;
    if (number != 6) {
        return 4;
    }
    u64 big = 5;
    Kind decoded = big as Kind;
    if (decoded != Kind::One) {
        return 5;
    }
    u32 source = 200;
    Small small = source as Small;
    u8 small_value = small as u8;
    if (small_value != 200) {
        return 6;
    }
    return 0;
}

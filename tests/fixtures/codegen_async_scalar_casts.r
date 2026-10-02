module test.codegen.async_scalar_casts;

/* R-EXPR-0016 and R-EXPR-0018 inside an async frame. */
@repr(C) enum Kind : c_int { Zero, One = 5, Two, };

async i32 main() {
    char letter = 'z';
    u32 code = letter as u32;
    if (code != 122) {
        return 1;
    }
    u32 source = 955;
    char lambda = source as char;
    if (lambda != 'λ') {
        return 2;
    }
    i64 wide = 6;
    Kind kind = wide as Kind;
    if (kind != Kind::Two) {
        return 3;
    }
    i32 number = kind as i32;
    return number - 6;
}

module test.codegen.conditional_expression;

struct Record { i32 code; constexpr str message; };
struct Owned { own i32* payload; i32 code; };
error Error { i32 code; };

protected i32 tick(i32* count, i32 value) {
    *count += 1;
    return value;
}

protected i32 checked(i32* count, bool fail) throws Error {
    *count += 1;
    throw (fail == true) Error { .code = 7 };
    return 13;
}

protected i32 run(bool flag) {
    i32 value = 17;
    const i32* pointer = &value;
    const i32* selected = flag == true ? pointer : pointer;
    if (*selected != 17) { return 1; }
    u8[2] data = {11, 13};
    const u8[] view = &data;
    const u8[] selected_view = flag == true ? view : view;
    if (selected_view[1] != 13) { return 2; }
    str message = "one";
    str selected_message = flag == true ? message : "two";
    const u8[] text_view = selected_message;
    if ((flag == true && text_view[0] != 111) ||
        (flag == false && text_view[0] != 116)) { return 3; }
    own i32* retained = new i32(61);
    own i32* constant_choice = false ? move retained : new i32(67);
    if (*retained != 61 || *constant_choice != 67) { return 15; }
    i32 count = 0;
    i32 chosen = (tick(&count, 1) == 1 && flag == true)
        ? tick(&count, 7) : tick(&count, 9);
    chosen as void;
    if (count != 2) { return 1; }
    if ((flag == true && chosen != 7) || (flag == false && chosen != 9)) { return 2; }
    i32 divisor = 0;
    if (flag == true) { divisor = 1; }
    i32 lazy = flag == true ? 12 / divisor : 3;
    if ((flag == true && lazy != 12) || (flag == false && lazy != 3)) { return 3; }
    i32 nested = flag == true ? 10 : count == 2 ? 20 : 30;
    if ((flag == true && nested != 10) || (flag == false && nested != 20)) { return 4; }
    const i32 left = 41;
    i32 right = 42;
    i32 copy = flag == true ? left : right;
    if (left != 41 || right != 42) { return 5; }
    copy as void;
    i16 narrow = 123;
    i64 wide = 456;
    i64 widened = flag == true ? narrow : wide;
    if ((flag == true && widened != 123) || (flag == false && widened != 456)) { return 6; }
    u64 literal = flag == true ? 18446744073709551615 : 0;
    if ((flag == true && literal != 18446744073709551615) ||
        (flag == false && literal != 0)) { return 7; }
    f32 small = 1.5f32;
    f64 large = 2.5;
    f64 floating = flag == true ? small : large;
    floating as void;
    Record record = {
        .code = flag == true ? 17 : 19,
        .message = flag == true ? "exists" : "cannot publish",
    };
    if ((flag == true && record.code != 17) || (flag == false && record.code != 19)) { return 8; }
    own i32* payload = new i32(55);
    own i32* owner = flag == true ? move payload : move payload;
    if (*owner != 55) { return 9; }
    Owned fresh = flag == true
        ? Owned { .payload = new i32(1), .code = 21 }
        : Owned { .payload = new i32(2), .code = 23 };
    if ((flag == true && fresh.code != 21) || (flag == false && fresh.code != 23)) { return 10; }
    i32 effects = 0;
    i32 finalizations = 0;
    try {
        try {
            i32 value = flag == true ? checked(&effects, true) : checked(&effects, false);
            if (value != 13 || flag == true) { return 11; }
        } finally { finalizations += 1; }
    } catch (Error error) {
        if (error.code != 7 || flag == false) { return 12; }
    }
    if (effects != 1 || finalizations != 1) { return 13; }
    return 0;
}

i32 main() {
    i32 first = run(true);
    i32 second = run(false);
    second as void;
    if (first != 0) { return first; }
    return second;
}

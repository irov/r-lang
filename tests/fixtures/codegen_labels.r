module test.codegen.labels;

import std.string;
import std.text;

/* L20: implicit break (R-STMT-0007), throw arms (R-EXPR-0031), string labels and case matchers
   (R-STMT-0006, R-TYPE-0046). */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

error Usage { i32 code; };

enum Color { red, green, blue };
enum Slot { empty, held(Token) };

i32 paint(Color color) {
    i32 total = 0;
    switch (color) {
    case Color::red: total = 1;
    case Color::green:
        total = 2;
        fallthrough;
    case Color::blue: total += 10;
    }
    return total;
}

i32 take(Slot slot) throws Usage {
    return match (move slot) {
        case variant Slot::held(move token): token.id;
        default: throw Usage {.code = 7};
    };
}

i32 held_code(Slot slot) throws Usage {
    return match (move slot) {
        case variant Slot::held(move token): throw Usage {.code = token.id};
        default: 0;
    };
}

i32 kind_code(str kind) {
    return match (kind) {
        case "f32": 1;
        case "f64": 2;
        default: 0;
    };
}

i32 kind_switch(str kind) {
    i32 code = 0;
    switch (kind) {
    case "a": code = 1;
    case "b":
        code = 2;
        fallthrough;
    default: code += 100;
    }
    return code;
}

std.string::string name_of(i32 n) throws std.alloc::alloc_error {
    if (n == 1) { return std.string::from_str("one"); }
    return std.string::from_str("other");
}

i32 computed(i32 n) throws std.alloc::alloc_error {
    std.string::string text = name_of(n);
    i32 code = 0;
    switch (text) {
    case "one": code = 1;
    default: code = 9;
    }
    drop text;
    return code;
}

struct Parity { i32 value; };

impl core::CaseMatcher for Parity {
    type Label = i32;
    bool matches(const Parity* this, i32 label) { return this->value % 2 == label; }
};

i32 parity(i32 value) {
    Parity p = {.value = value};
    i32 code = 0;
    switch (p) {
    case 0: code = 10;
    case 1: code = 20;
    default: code = 30;
    }
    return code + match (p) {
        case 1: 1;
        default: 0;
    };
}

i32 caseless(str kind) {
    i32 code = 0;
    switch (std.text::ignore_ascii_case(kind)) {
    case "c_float": code = 3;
    case "c_double": code = 4;
    default: code = 9;
    }
    return code + match (std.text::ignore_ascii_case(kind)) {
        case "F32": 100;
        default: 0;
    };
}

i32 main() {
    i32 failures = paint(Color::red) == 1 ? 0 : 1;
    failures += paint(Color::green) == 12 ? 0 : 2;
    failures += paint(Color::blue) == 10 ? 0 : 4;
    try {
        failures += take(Slot::held(Token {.id = 3})) == 3 ? 0 : 8;
        failures += take(Slot::empty) == 0 ? 16 : 16;
    } catch (Usage failure) { failures += failure.code == 7 ? 0 : 32; }
    failures += drop_count(false) == 1 ? 0 : 64;
    try {
        failures += held_code(Slot::held(Token {.id = 5})) == 0 ? 128 : 128;
    } catch (Usage failure) { failures += failure.code == 5 ? 0 : 256; }
    failures += drop_count(false) == 2 ? 0 : 512;
    failures += kind_code("f64") == 2 && kind_code("F64") == 0 ? 0 : 1024;
    failures += kind_switch("a") == 1 && kind_switch("b") == 102 && kind_switch("c") == 100 ? 0 : 2048;
    try {
        failures += computed(1) == 1 && computed(2) == 9 ? 0 : 4096;
    } catch (std.alloc::alloc_error failure) { failures += 4096; }
    failures += parity(4) == 10 && parity(5) == 21 ? 0 : 8192;
    failures += caseless("C_Double") == 4 && caseless("f32") == 109 && caseless("x") == 9 ? 0 : 16384;
    return failures;
}

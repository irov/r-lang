module test.codegen.async_labels;

import std.string;
import std.text;

/* L20 in asynchronous frames: implicit break in switch and select clauses, throw arms, string
   labels and case matchers across awaits. */
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

enum Slot { empty, held(Token) };

async u32 value_of(u32 v) { return v; }

async i32 take(Slot slot) throws Usage {
    i32 id = match (move slot) {
        case variant Slot::held(move token): token.id;
        default: throw Usage {.code = 7};
    };
    return id;
}

async std.string::string name_of(i32 n) throws std.alloc::alloc_error {
    if (n == 1) { return std.string::from_str("One"); }
    return std.string::from_str("other");
}

async i32 classify(i32 n) throws std.alloc::alloc_error, std.async::start_error {
    std.string::string text = await name_of(n);
    i32 code = 0;
    switch (std.text::ignore_ascii_case(text.as_str())) {
    case "one": code = 1;
    case "two": code = 2;
    default: code = 9;
    }
    i32 exact = match (text.as_str()) {
        case "One": 10;
        default: 0;
    };
    drop text;
    return code + exact;
}

// L24-1: a `constexpr str` subject, such as the name of a portable error.
async i32 named(std.error::error error) {
    i32 code = 0;
    switch (std.error::name(error)) {
    case "scope_full": code += 1;
    default: code += 2;
    }
    i32 exact = match (std.error::name(error)) {
        case "scope_full": 10;
        default: 20;
    };
    return code + exact;
}

async u32 pick() throws std.async::start_error {
    u32 result = 0u32;
    task_scope(1) group {
        auto first = value_of(5u32);
        select (group) {
        case u32 value = await move first: result = value;
        }
    }
    return result;
}

async i32 main() {
    i32 failures = 0;
    try {
        failures += await take(Slot::held(Token {.id = 3})) == 3 ? 0 : 1;
    } catch (Usage failure) { failures += 2; }
    catch (std.async::start_error failure) { failures += 2; }
    failures += drop_count(false) == 1 ? 0 : 4;
    try {
        failures += await take(Slot::empty) == 0 ? 8 : 8;
    } catch (Usage failure) { failures += failure.code == 7 ? 0 : 16; }
    catch (std.async::start_error failure) { failures += 16; }
    try {
        failures += await classify(1) == 11 && await classify(3) == 9 ? 0 : 32;
        failures += await pick() == 5u32 ? 0 : 64;
        std.error::error full = std.error::from_async(std.async::start_error::scope_full);
        failures += await named(full) == 11 ? 0 : 256;
    } catch (std.alloc::alloc_error failure) { failures += 128; }
    catch (std.async::start_error failure) { failures += 128; }
    return failures;
}

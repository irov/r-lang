module test.codegen.consteval_checked;

/* L22.1 (R-FUNC-0023, R-EXPR-0032): checked errors, try, catch and finally, error families and
   variants of module objects during translation. */

error bad_digit { u32 position; };
error too_large { u32 value; };
error text_fault { empty, spaces };

error config_error { u32 line; };
error syntax_error : config_error { u32 column; };
error range_error : config_error { i64 value; };
error deep_error : syntax_error { bool fatal; };

enum shape { circle(u32), square { u32 side; u32 depth; }, dot };

u32 digit_at(str text, usize index) throws bad_digit {
    u8 byte = text[index];
    throw (byte < 48u8 || byte > 57u8) bad_digit {.position = index as u32};
    return (byte - 48u8) as u32;
}

u32 parse(str text) throws bad_digit, too_large, text_fault {
    throw (len(text) == 0usize) text_fault::empty;
    throw (text[0usize] == 32u8) text_fault::spaces;
    u32 value = 0u32;
    for (usize index = 0usize; index < len(text); index += 1usize) {
        value = value * 10u32 + digit_at(text, index);
        throw (value > 1000u32) too_large {.value = value};
    }
    return value;
}

u32 parse_or(str text, u32 fallback) {
    try {
        return parse(text);
    } catch (bad_digit error) {
        return fallback + error.position;
    } catch (too_large error) {
        return error.value;
    } catch (text_fault error) {
        if (error == text_fault::empty) {
            return 1u32;
        }
        return 2u32;
    }
}

u32 with_finally(str text) {
    u32 trace = 0u32;
    try {
        try {
            trace += parse(text);
        } finally {
            trace += 100000u32;
        }
    } catch (bad_digit error) {
        trace += 7u32;
    } catch (too_large error) {
        trace += 8u32;
    } catch (text_fault error) {
        trace += 9u32;
    }
    return trace;
}

u32 counter(str text) throws bad_digit, too_large, text_fault {
    u32 count = 0u32;
    try {
        count += parse(text);
    } finally {
        count += 1u32;
    }
    return count;
}

u32 rethrow(str text) throws too_large {
    try {
        return parse(text);
    } catch (bad_digit error) {
        throw too_large {.value = error.position + 5000u32};
    } catch (text_fault error) {
        return 3u32;
    }
}

u32 rethrown(str text) {
    try {
        return rethrow(text);
    } catch (too_large error) {
        return error.value;
    }
}

u32 check(u32 code) throws config_error {
    throw (code == 1u32) syntax_error {.line = 3u32, .column = 7u32};
    throw (code == 2u32) range_error {.line = 4u32, .value = -5i64};
    throw (code == 3u32) config_error {.line = 9u32};
    throw (code == 4u32) deep_error {.line = 11u32, .column = 2u32, .fatal = true};
    return code;
}

u32 line_of(u32 code) {
    try {
        return check(code) + 100u32;
    } catch (config_error error) {
        return error.line;
    }
}

u32 forward(u32 code) throws config_error {
    try {
        return check(code);
    } catch (config_error error) {
        throw error;
    }
}

u32 nearest(u32 code) {
    try {
        return forward(code);
    } catch (config_error error) {
        return 1000u32 + error.line;
    } catch (syntax_error error) {
        return 2000u32 + error.column;
    } catch (range_error error) {
        u32 magnitude = (0i64 - error.value) as u32;
        return 3000u32 + magnitude;
    }
}

config_error make(u32 line) {
    config_error made = syntax_error {.line = line, .column = 1u32};
    return made;
}

u32 area(shape value) {
    return match (value) {
        case variant shape::circle(radius): radius * 3u32;
        case variant shape::square(item): item.side * item.depth;
        case variant shape::dot: 0u32;
    };
}

i32 classify(u32 port) {
    switch (port) {
    case parse("80"):
        return 1;
    case parse("443"):
        return 2;
    default:
        return 0;
    }
}

u32 folded_in_try() {
    u32 total = 0u32;
    try {
        total += parse("808");
    } catch (bad_digit error) {
        total += 1u32;
    } catch (too_large error) {
        total += 2u32;
    } catch (text_fault error) {
        total += 3u32;
    }
    return total;
}

u32 runtime_throw(str text) {
    u32 total = 0u32;
    try {
        total += parse(text);
    } catch (bad_digit error) {
        total += error.position + 100u32;
    } catch (too_large error) {
        total += 2u32;
    } catch (text_fault error) {
        total += 3u32;
    }
    return total;
}

const u32 PLAIN = parse_or("123", 9u32);
const u32 DIGIT = parse_or("12x", 9u32);
const u32 LARGE = parse_or("99999", 9u32);
const u32 EMPTY = parse_or("", 9u32);
const u32 SPACES = parse_or(" 1", 9u32);
const u32 FIN_OK = with_finally("42");
const u32 FIN_DIGIT = with_finally("4y");
const u32 FIN_LARGE = with_finally("5000");
const u32 FIN_EMPTY = with_finally("");
const u32 COUNT = counter("17");
const u32 AGAIN = rethrown("1z");
const u32 AGAIN_OK = rethrown(" 7");
const u32 LINE_SYNTAX = line_of(1u32);
const u32 LINE_RANGE = line_of(2u32);
const u32 LINE_BASE = line_of(3u32);
const u32 LINE_NONE = line_of(5u32);
const u32 NEAR_SYNTAX = nearest(1u32);
const u32 NEAR_RANGE = nearest(2u32);
const u32 NEAR_BASE = nearest(3u32);
const u32 NEAR_DEEP = nearest(4u32);
const config_error SAMPLE = make(42u32);
const shape CIRCLE = shape::circle(2u32);
const shape SQUARE = shape::square {.side = 3u32, .depth = 4u32};
const u32[2] PORTS = {parse("80"), parse("443")};

i32 main() {
    static const u32 cached = parse("81");
    i32 failures = 0;
    failures += PLAIN == 123u32 ? 0 : 1;
    failures += DIGIT == 11u32 ? 0 : 1;
    failures += LARGE == 9999u32 ? 0 : 1;
    failures += EMPTY == 1u32 ? 0 : 1;
    failures += SPACES == 2u32 ? 0 : 1;
    failures += FIN_OK == 100042u32 ? 0 : 1;
    failures += FIN_DIGIT == 100007u32 ? 0 : 1;
    failures += FIN_LARGE == 100008u32 ? 0 : 1;
    failures += FIN_EMPTY == 100009u32 ? 0 : 1;
    failures += COUNT == 18u32 ? 0 : 1;
    failures += AGAIN == 5001u32 ? 0 : 1;
    failures += AGAIN_OK == 3u32 ? 0 : 1;
    failures += LINE_SYNTAX == 3u32 ? 0 : 1;
    failures += LINE_RANGE == 4u32 ? 0 : 1;
    failures += LINE_BASE == 9u32 ? 0 : 1;
    failures += LINE_NONE == 105u32 ? 0 : 1;
    failures += NEAR_SYNTAX == 2007u32 ? 0 : 1;
    failures += NEAR_RANGE == 3005u32 ? 0 : 1;
    failures += NEAR_BASE == 1009u32 ? 0 : 1;
    failures += NEAR_DEEP == 2002u32 ? 0 : 1;
    failures += SAMPLE.line == 42u32 ? 0 : 1;
    failures += area(CIRCLE) == 6u32 ? 0 : 1;
    failures += area(SQUARE) == 12u32 ? 0 : 1;
    failures += PORTS[1] == 443u32 ? 0 : 1;
    failures += cached == 81u32 ? 0 : 1;
    failures += classify(443u32) == 2 ? 0 : 1;
    failures += classify(PORTS[0]) == 1 ? 0 : 1;
    failures += folded_in_try() == 808u32 ? 0 : 1;
    failures += runtime_throw("8x") == 101u32 ? 0 : 1;
    failures += runtime_throw(" ") == 3u32 ? 0 : 1;
    @if (parse("7") == 7u32) {
        failures += 0;
    } @else {
        failures += 1;
    }
    return failures;
}

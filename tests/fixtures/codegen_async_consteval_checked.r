module test.codegen.async_consteval_checked;

/* L22.1 (R-EXPR-0032): translation-time values with checked errors inside asynchronous
   functions, lowered through MIR. */

error bad_digit { u32 position; };
error config_error { u32 line; };
error syntax_error : config_error { u32 column; };

u32 parse(str text) throws bad_digit {
    u32 value = 0u32;
    for (usize index = 0usize; index < len(text); index += 1usize) {
        u8 byte = text[index];
        throw (byte < 48u8 || byte > 57u8) bad_digit {.position = index as u32};
        value = value * 10u32 + ((byte - 48u8) as u32);
    }
    return value;
}

u32 parse_or(str text, u32 fallback) {
    try {
        return parse(text);
    } catch (bad_digit error) {
        return fallback + error.position;
    }
}

u32 check(u32 code) throws config_error {
    throw (code == 1u32) syntax_error {.line = 3u32, .column = 7u32};
    return code;
}

u32 line_of(u32 code) {
    try {
        return check(code);
    } catch (config_error error) {
        return error.line;
    } finally {
        code as void;
    }
}

const u32 PORT = parse_or("8080", 0u32);
const u32 FALLBACK = parse_or("80a", 100u32);
const u32 LINE = line_of(1u32);
const u32[2] PORTS = {parse("80"), parse("443")};

async u32 folded() {
    u32 total = 0u32;
    try {
        total += parse("808");
    } catch (bad_digit error) {
        total += 1u32;
    }
    return total;
}

async u32 runtime(bool digits) {
    str text = "4q";
    if (digits == true) {
        text = "12";
    }
    u32 total = 0u32;
    try {
        total += parse(text);
    } catch (bad_digit error) {
        total += error.position + 100u32;
    }
    return total;
}

async i32 main() {
    i32 failures = 0;
    failures += PORT == 8080u32 ? 0 : 1;
    failures += FALLBACK == 102u32 ? 0 : 1;
    failures += LINE == 3u32 ? 0 : 1;
    failures += PORTS[1] == 443u32 ? 0 : 1;
    u32 first = await folded();
    failures += first == 808u32 ? 0 : 1;
    u32 second = await runtime(false);
    failures += second == 101u32 ? 0 : 1;
    u32 third = await runtime(true);
    failures += third == 12u32 ? 0 : 1;
    return failures;
}

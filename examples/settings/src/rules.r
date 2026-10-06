module example.settings.rules;

import std.string;

/* The failures of one argument form a family (Core R-AGG-0011): each error below derives from
   setting_error, whose field argument every member begins with. A catch of setting_error
   receives any of them, and a value of setting_error reads that field whatever it holds. */
error setting_error { usize argument; };
error missing_value : setting_error { std.string::string key; };
error unknown_key : setting_error { std.string::string key; };
error number_error : setting_error { constexpr str key; };
error range_error : setting_error { constexpr str key; i64 low; i64 high; };
error choice_error : setting_error { constexpr str key; };

/* How the workers run; `safe` is the default variant (Core R-INIT-0005). */
enum Mode { @default safe, fast };

/* The declared initializers are the settings of an initialization that omits a field (Core
   R-INIT-0004): `Settings {}` is port 8080, one worker, the default mode and no retries. */
struct Settings {
    i64 port = 8080;
    i64 workers = 1;
    Mode mode;
    u8 retries = 0;
};

/* Whether bytes [start, end) of the text spell the word. */
bool spells(str text, usize start, usize end, str word) {
    if (end - start != len(word)) { return false; }
    for (usize index = 0usize; index < len(word); index += 1usize) {
        if (text[start + index] != word[index]) { return false; }
    }
    return true;
}

/* The decimal number in bytes [start, end) of a default within [low, high]. */
i64 decimal(str text, usize start, usize end, constexpr str key, usize argument, i64 low, i64 high)
    throws setting_error {
    throw (start == end) number_error {.argument = argument, .key = key};
    i64 value = 0;
    for (usize index = start; index < end; index += 1usize) {
        u8 byte = text[index];
        throw (byte < 48u8 || byte > 57u8) number_error {.argument = argument, .key = key};
        value = value * 10 + ((byte - 48u8) as i64);
        throw (value > high) range_error {.argument = argument, .key = key, .low = low, .high = high};
    }
    throw (value < low) range_error {.argument = argument, .key = key, .low = low, .high = high};
    return value;
}

/* The key that bytes [start, end) name: 1 port, 2 workers, 3 retries, 4 mode, 0 none. */
u8 key_code(str text, usize start, usize end) {
    if (spells(text, start, end, "port") == true) { return 1u8; }
    if (spells(text, start, end, "workers") == true) { return 2u8; }
    if (spells(text, start, end, "retries") == true) { return 3u8; }
    if (spells(text, start, end, "mode") == true) { return 4u8; }
    return 0u8;
}

/* Bytes [start, end) of the text as an owned string. */
std.string::string copy_text(str text, usize start, usize end) throws std.alloc::alloc_error {
    std.string::string copied = std.string::create();
    for (usize index = start; index < end; index += 1usize) {
        std.string::push_scalar(&copied, text[index] as char);
    }
    return move copied;
}

/* The declared settings changed by the `KEY=VALUE` words separated by spaces; the argument of a
   failure is the position of its word. */
Settings parse_defaults(str text) throws setting_error, std.alloc::alloc_error {
    Settings parsed = Settings {};
    usize count = len(text);
    usize start = 0usize;
    usize position = 1usize;
    while (start < count) {
        usize end = start;
        usize equals = count;
        while (end < count && text[end] != 32u8) {
            if (equals == count && text[end] == 61u8) { equals = end; }
            end += 1usize;
        }
        if (equals == count) {
            throw missing_value {.argument = position, .key = copy_text(text, start, end)};
        }
        usize value = equals + 1usize;
        switch (key_code(text, start, equals)) {
        case 1u8: parsed.port = decimal(text, value, end, "port", position, 1, 65535);
        case 2u8: parsed.workers = decimal(text, value, end, "workers", position, 1, 64);
        case 3u8: parsed.retries = decimal(text, value, end, "retries", position, 0, 255) as u8;
        case 4u8:
            if (spells(text, value, end, "fast") == true) {
                parsed.mode = Mode::fast;
            } else {
                throw (spells(text, value, end, "safe") == false)
                    choice_error {.argument = position, .key = "mode"};
                parsed.mode = Mode::safe;
            }
        default:
            throw unknown_key {.argument = position, .key = copy_text(text, start, equals)};
        }
        start = end + 1usize;
        position += 1usize;
    }
    return parsed;
}

/* The defaults are the declared settings with settings text applied, checked while the program
   is built (Core R-EXPR-0032): the parser runs during translation, and a default such as
   `port=80800` stops the build with the error it throws, reported as a translation error instead
   of at run time. */
const Settings DEFAULTS = parse_defaults("workers=4 retries=3");

/* A decimal number of a key within [low, high]; the failure names the argument. */
i64 bounded(str text, constexpr str key, usize argument, i64 low, i64 high) throws setting_error {
    try {
        i64 value = std.convert::parse_i64(text, 10u32);
        throw (value < low || value > high)
            range_error {.argument = argument, .key = key, .low = low, .high = high};
        return value;
    } catch (std.convert::parse_error failure) {
        throw number_error {.argument = argument, .key = key};
    }
}

/* Reads KEY VALUE pairs after the program name. A retry count is converted by the standard
   library as it is: its failure is not a failure of one argument and leaves as a member of the
   root of the standard errors (Library R-SLIB-ERR-0004), like an allocation failure. */
Settings read(const str[] arguments) throws setting_error, std.error::fault {
    Settings chosen = DEFAULTS;
    usize count = len(arguments);
    for (usize index = 1usize; index < count; index += 2usize) {
        str key = arguments[index];
        if (index + 1usize == count) {
            throw missing_value {.argument = index, .key = std.string::from_str(key)};
        }
        str value = arguments[index + 1usize];
        switch (key) {
        case "port": chosen.port = bounded(value, "port", index + 1usize, 1, 65535);
        case "workers": chosen.workers = bounded(value, "workers", index + 1usize, 1, 64);
        case "mode":
            switch (value) {
            case "fast": chosen.mode = Mode::fast;
            case "safe": chosen.mode = Mode::safe;
            default: throw choice_error {.argument = index + 1usize, .key = "mode"};
            }
        case "retries": chosen.retries = std.convert::parse_u8(value, 10u32);
        default: throw unknown_key {.argument = index, .key = std.string::from_str(key)};
        }
    }
    return chosen;
}

/* The message of a failure. The exact error decides after a rethrow (Core R-ERR-0002); the
   nearest ancestor wins whatever the order of the clauses, so the clause for setting_error
   itself receives only a failure of that exact type (Core R-ERR-0003). */
std.string::string describe(setting_error failure) throws std.alloc::alloc_error {
    usize argument = failure.argument;
    try {
        throw move failure;
    } catch (setting_error other) {
        return f"argument {argument}: invalid setting\n";
    } catch (missing_value e) {
        str key = e.key;
        return f"argument {argument}: {key} has no value\n";
    } catch (unknown_key e) {
        str key = e.key;
        return f"argument {argument}: unknown key {key}\n";
    } catch (number_error e) {
        return f"argument {argument}: {e.key} shall be a decimal integer\n";
    } catch (range_error e) {
        return f"argument {argument}: {e.key} shall be from {e.low} to {e.high}\n";
    } catch (choice_error e) {
        return f"argument {argument}: {e.key} shall be fast or safe\n";
    }
}

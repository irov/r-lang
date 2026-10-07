module test.codegen.library_args;
import std.args;
import std.text;

// R-SLIB-ARGS-0001..0003 (M23): declarations, the parse of flags, options, grouped letters,
// positional and remaining arguments, help, and every error with its index.

error Failed { i32 code; };

std.args::parser declared() throws std.args::args_error, std.alloc::alloc_error {
    std.args::parser parser = std.args::parser::create("tool", "an argument test");
    parser.flag("verbose", "v", "print more");
    parser.flag("dry-run", "n", "change nothing");
    parser.option("port", "p", "PORT", "listening port");
    parser.option("log.level", "", "LEVEL", "lowest level written");
    parser.positional("mode", "what to do", true);
    parser.positional("target", "where", false);
    parser.remaining("rest", "more inputs");
    return move parser;
}

i32 code_of(std.args::error_code code) {
    return core::enum_ordinal(code) as i32 + 1;
}

/* The error code and index of one parse, or zero. */
i32 parse_failure(const std.args::parser* parser, const str[] arguments, usize index)
    throws std.alloc::alloc_error {
    try {
        std.args::matches found = parser->parse(arguments);
        drop found;
        return 0;
    } catch (std.args::args_error failure) {
        if (failure.index != index) { return 100 + failure.index as i32; }
        return code_of(failure.code);
    }
}

i32 checks() throws std.args::args_error, std.alloc::alloc_error, Failed {
    std.args::parser parser = declared();
    str[9] full = {"tool", "-vn", "--port=80", "--log.level", "debug", "run", "here", "a", "b"};
    std.args::matches found = parser.parse(full[0usize..9usize]);
    throw (found.has("verbose") == false || found.has("dry-run") == false) Failed {.code = 1};
    o<std.string::string> port = found.value("port");
    switch (move port) {
    case variant o::some(move text): throw (std.bytes::equal(text, "80") == false) Failed {.code = 2};
    case variant o::none: throw Failed {.code = 3};
    }
    o<std.string::string> level = found.value("log.level");
    switch (move level) {
    case variant o::some(move text): throw (std.bytes::equal(text, "debug") == false) Failed {.code = 4};
    case variant o::none: throw Failed {.code = 5};
    }
    array<std.string::string> rest = found.remaining();
    throw (len(rest) != 2usize || std.bytes::equal(rest[1], "b") == false) Failed {.code = 6};
    /* A short option takes the rest of its argument or the next one; the last value counts. */
    str[6] short_forms = {"tool", "-p8080", "-vp", "9090", "run", "--"};
    std.args::matches second = parser.parse(short_forms[0usize..6usize]);
    o<std.string::string> last = second.value("port");
    switch (move last) {
    case variant o::some(move text): throw (std.bytes::equal(text, "9090") == false) Failed {.code = 7};
    case variant o::none: throw Failed {.code = 8};
    }
    throw (second.has("target") == true) Failed {.code = 9};
    /* After -- everything is positional; help skips the check of required arguments. */
    str[4] dashes = {"tool", "--", "-v", "--port"};
    std.args::matches third = parser.parse(dashes[0usize..4usize]);
    throw (third.has("verbose") == true || third.has("target") == false) Failed {.code = 10};
    str[2] help = {"tool", "--help"};
    std.args::matches asked = parser.parse(help[0usize..2usize]);
    throw (asked.help_requested() == false) Failed {.code = 11};
    std.string::string text = parser.help();
    throw (std.text::starts_with(text, "tool - an argument test\nusage: tool [options] mode [target] [rest...]\n") == false)
        Failed {.code = 12};
    /* Errors with the index of the argument. */
    str[3] unknown = {"tool", "run", "--colour"};
    throw (parse_failure(&parser, unknown[0usize..3usize], 2usize) != code_of(std.args::error_code::unknown_option)) Failed {.code = 13};
    str[3] missing = {"tool", "run", "--port"};
    throw (parse_failure(&parser, missing[0usize..3usize], 2usize) != code_of(std.args::error_code::missing_value)) Failed {.code = 14};
    str[3] valued = {"tool", "run", "--verbose=yes"};
    throw (parse_failure(&parser, valued[0usize..3usize], 2usize) != code_of(std.args::error_code::unexpected_value)) Failed {.code = 15};
    str[2] none = {"tool", "-v"};
    throw (parse_failure(&parser, none[0usize..2usize], 2usize) != code_of(std.args::error_code::missing_argument)) Failed {.code = 16};
    str[3] letter = {"tool", "-vx", "run"};
    throw (parse_failure(&parser, letter[0usize..3usize], 1usize) != code_of(std.args::error_code::unknown_option)) Failed {.code = 17};
    try {
        found.has("colour") as void;
        throw Failed {.code = 18};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::unknown_name) Failed {.code = 19};
    }
    /* A parser without remaining arguments rejects extra ones. */
    std.args::parser strict = std.args::parser::create("strict", "no extras");
    strict.positional("only", "one input", false);
    str[3] extra = {"strict", "a", "b"};
    throw (parse_failure(&strict, extra[0usize..3usize], 2usize) != code_of(std.args::error_code::extra_argument)) Failed {.code = 20};
    /* Declaration errors carry the index of the declaration. */
    try {
        strict.flag("Bad", "", "upper case");
        throw Failed {.code = 21};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::invalid_name || failure.index != 1usize) Failed {.code = 22};
    }
    try {
        strict.option("only", "o", "X", "twice");
        throw Failed {.code = 23};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::duplicate_name) Failed {.code = 24};
    }
    try {
        strict.positional("late", "required after optional", true);
        throw Failed {.code = 25};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::invalid_name) Failed {.code = 26};
    }
    try {
        strict.flag("helpful", "h", "the help letter");
        throw Failed {.code = 27};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::invalid_name) Failed {.code = 28};
    }
    /* A flag has no value, the help lines line up, and remaining is declared once. */
    o<std.string::string> flag_value = found.value("verbose");
    switch (move flag_value) {
    case variant o::some(move value): drop value; throw Failed {.code = 29};
    case variant o::none: break;
    }
    throw (std.text::contains(text, "\n  -p, --port PORT          listening port\n") == false ||
           std.text::contains(text, "\n  --log.level LEVEL        lowest level written\n") == false ||
           std.text::contains(text, "\n  -h, --help               print this help\n") == false)
        Failed {.code = 30};
    try {
        parser.remaining("more", "twice");
        throw Failed {.code = 31};
    } catch (std.args::args_error failure) {
        throw (failure.code != std.args::error_code::invalid_name || failure.index != 6usize)
            Failed {.code = 32};
    }
    return 0;
}

i32 main() {
    try {
        return checks();
    } catch (Failed failure) {
        return failure.code;
    } catch (std.args::args_error failure) {
        return 90 + core::enum_ordinal(failure.code) as i32;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    }
}

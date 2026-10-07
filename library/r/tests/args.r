module tests.std.args;
import std.test;
import std.args;
import std.text;

// The tests of std.args (Library R-SLIB-ARGS-0001..0003), run in test mode (Core R-FUNC-0025).

/* The parser of most tests: two flags, two options, two positional arguments and the rest. */
protected std.args::parser declared() throws std.args::args_error, std.alloc::alloc_error {
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

/* Checks that a declared option or positional argument was given with the value. */
protected void expect_value(const std.args::matches* found, str name, str expected)
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    o<std.string::string> value = found->value(name);
    switch (move value) {
    case variant o::some(move text): std.test::equal_text(text, expected);
    case variant o::none:
        std.string::string message = f"{name} has no value";
        std.test::fail(message);
    }
}

/* Checks that a declared name has no value. */
protected void expect_no_value(const std.args::matches* found, str name)
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    o<std.string::string> value = found->value(name);
    switch (move value) {
    case variant o::some(move text):
        std.string::string message = f"{name} has the value {text}";
        std.test::fail(message);
    case variant o::none: break;
    }
}

/* Checks the code and index of an error. */
protected void expect_error(std.args::args_error failure, std.args::error_code code, usize index)
    throws std.test::failure, std.alloc::alloc_error {
    std.test::equal_text(core::enum_name(failure.code), core::enum_name(code));
    std.test::equal(failure.index, index);
}

/* Checks that the parse of the arguments reports the code at the index. */
protected void expect_parse_error(const std.args::parser* parser, const str[] arguments,
                                  std.args::error_code code, usize index)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        std.args::matches found = parser->parse(arguments);
        drop found;
    } catch (std.args::args_error failure) {
        expect_error(failure, code, index);
        return;
    }
    std.test::fail("the parse reported no error");
}

@test
void parses_long_and_grouped_arguments()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    str[9] full = {"tool", "-vn", "--port=80", "--log.level", "debug", "run", "here", "a", "b"};
    std.args::matches found = parser.parse(full[0usize..9usize]);
    std.test::check(found.help_requested() == false, "no help was asked for");
    std.test::check(found.has("verbose"), "-v gives verbose");
    std.test::check(found.has("dry-run"), "-n gives dry-run");
    expect_value(&found, "port", "80");
    expect_value(&found, "log.level", "debug");
    expect_value(&found, "mode", "run");
    expect_value(&found, "target", "here");
    array<std.string::string> rest = found.remaining();
    std.test::equal(len(rest), 2usize);
    std.test::equal_text(rest[0], "a");
    std.test::equal_text(rest[1], "b");
    /* Without them, the flags, options and optional arguments are absent. */
    str[2] least = {"tool", "run"};
    std.args::matches bare = parser.parse(least[0usize..2usize]);
    std.test::check(bare.has("verbose") == false, "verbose was not given");
    std.test::check(bare.has("port") == false, "port was not given");
    std.test::check(bare.has("target") == false, "target was not given");
    expect_no_value(&bare, "port");
    expect_no_value(&bare, "target");
    expect_value(&bare, "mode", "run");
    array<std.string::string> none = bare.remaining();
    std.test::equal(len(none), 0usize);
}

@test
void reads_option_values() throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    /* An option letter takes the rest of its argument, or the next argument when nothing is
       left; the flags before it count. */
    str[3] attached = {"tool", "-vp8080", "run"};
    std.args::matches first = parser.parse(attached[0usize..3usize]);
    std.test::check(first.has("verbose"), "-v before the option letter");
    expect_value(&first, "port", "8080");
    expect_value(&first, "mode", "run");
    str[4] separate = {"tool", "-np", "9090", "run"};
    std.args::matches second = parser.parse(separate[0usize..4usize]);
    std.test::check(second.has("dry-run"), "-n before the option letter");
    std.test::check(second.has("verbose") == false, "-v was not given");
    expect_value(&second, "port", "9090");
    expect_value(&second, "mode", "run");
    /* The value after `=` may hold `=`, and the next argument is a value even with a dash. */
    str[5] spelled = {"tool", "--log.level=a=b", "--port", "-5", "run"};
    std.args::matches third = parser.parse(spelled[0usize..5usize]);
    expect_value(&third, "log.level", "a=b");
    expect_value(&third, "port", "-5");
    expect_value(&third, "mode", "run");
    /* The last value of an option counts, and an empty value is a value. */
    str[7] repeated = {"tool", "--port=1", "-p2", "run", "--port", "3", "--log.level="};
    std.args::matches last = parser.parse(repeated[0usize..7usize]);
    expect_value(&last, "port", "3");
    expect_value(&last, "log.level", "");
    std.test::check(last.has("log.level"), "an empty value is given");
    /* A flag has no value, given or not. */
    expect_no_value(&first, "verbose");
    expect_no_value(&first, "dry-run");
}

@test
void ends_options_at_double_dash()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    str[6] dashes = {"tool", "-v", "--", "-n", "--port", "--"};
    std.args::matches found = parser.parse(dashes[0usize..6usize]);
    std.test::check(found.has("verbose"), "-v before --");
    std.test::check(found.has("dry-run") == false, "-n after -- is positional");
    std.test::check(found.has("port") == false, "--port after -- is positional");
    expect_value(&found, "mode", "-n");
    expect_value(&found, "target", "--port");
    array<std.string::string> rest = found.remaining();
    std.test::equal(len(rest), 1usize);
    std.test::equal_text(rest[0], "--");
    /* A lone dash is an argument, not an option. */
    str[3] lone = {"tool", "-", "-v"};
    std.args::matches single = parser.parse(lone[0usize..3usize]);
    expect_value(&single, "mode", "-");
    std.test::check(single.has("verbose"), "-v after a lone dash");
}

@test
void asks_for_help() throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    /* Help is asked for with -h or --help, and then a required argument may be missing. */
    str[2] long_form = {"tool", "--help"};
    std.args::matches asked = parser.parse(long_form[0usize..2usize]);
    std.test::check(asked.help_requested(), "--help asks for help");
    std.test::check(asked.has("mode") == false, "mode is missing");
    str[3] short_form = {"tool", "-v", "-h"};
    std.args::matches short_asked = parser.parse(short_form[0usize..3usize]);
    std.test::check(short_asked.help_requested(), "-h asks for help");
    std.test::check(short_asked.has("verbose"), "the other arguments still count");
    str[2] plain = {"tool", "run"};
    std.args::matches not_asked = parser.parse(plain[0usize..2usize]);
    std.test::check(not_asked.help_requested() == false, "no help was asked for");
    /* After -- they are ordinary arguments. */
    str[3] quoted = {"tool", "--", "--help"};
    std.args::matches positional = parser.parse(quoted[0usize..3usize]);
    std.test::check(positional.help_requested() == false, "--help after -- is positional");
    expect_value(&positional, "mode", "--help");
}

@test
void writes_the_help_text() throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    std.string::string text = parser.help();
    std.test::equal_text(text,
                         "tool - an argument test\n"
                         "usage: tool [options] mode [target] [rest...]\n"
                         "  -v, --verbose            print more\n"
                         "  -n, --dry-run            change nothing\n"
                         "  -p, --port PORT          listening port\n"
                         "  --log.level LEVEL        lowest level written\n"
                         "  mode                     what to do\n"
                         "  target                   where\n"
                         "  rest                     more inputs\n"
                         "  -h, --help               print this help\n");
    std.args::parser empty = std.args::parser::create("bare", "nothing declared");
    std.string::string least = empty.help();
    std.test::equal_text(least,
                         "bare - nothing declared\n"
                         "usage: bare [options]\n"
                         "  -h, --help               print this help\n");
}

@test
void reports_parse_errors_with_their_index()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    str[3] unknown = {"tool", "run", "--colour"};
    expect_parse_error(&parser, unknown[0usize..3usize],
                       std.args::error_code::unknown_option, 2usize);
    str[3] letter = {"tool", "-vx", "run"};
    expect_parse_error(&parser, letter[0usize..3usize],
                       std.args::error_code::unknown_option, 1usize);
    str[3] missing = {"tool", "run", "--port"};
    expect_parse_error(&parser, missing[0usize..3usize],
                       std.args::error_code::missing_value, 2usize);
    str[4] missing_short = {"tool", "-v", "run", "-vp"};
    expect_parse_error(&parser, missing_short[0usize..4usize],
                       std.args::error_code::missing_value, 3usize);
    str[3] valued = {"tool", "run", "--verbose=yes"};
    expect_parse_error(&parser, valued[0usize..3usize],
                       std.args::error_code::unexpected_value, 2usize);
    str[3] absent = {"tool", "-v", "--port=1"};
    expect_parse_error(&parser, absent[0usize..3usize],
                       std.args::error_code::missing_argument, 3usize);
    str[1] nothing = {"tool"};
    expect_parse_error(&parser, nothing[0usize..1usize],
                       std.args::error_code::missing_argument, 1usize);
    std.args::parser strict = std.args::parser::create("strict", "no extras");
    strict.positional("only", "one input", false);
    str[4] extra = {"strict", "a", "b", "c"};
    expect_parse_error(&strict, extra[0usize..4usize],
                       std.args::error_code::extra_argument, 2usize);
}

@test
void reports_declaration_errors()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = std.args::parser::create("tool", "declarations");
    parser.flag("verbose", "v", "print more");
    /* Names: 1..64 lowercase letters, digits, -, _ and ., starting with a letter. */
    try {
        parser.flag("Bad", "", "upper case");
        std.test::fail("an upper-case name is invalid");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.option("", "", "X", "empty");
        std.test::fail("an empty name is invalid");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.flag("9lives", "", "digit first");
        std.test::fail("a name starts with a letter");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.flag("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "",
                    "65 bytes");
        std.test::fail("a name has at most 64 bytes");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.flag("help", "", "taken");
        std.test::fail("help is taken");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    /* Short names: empty or one ASCII letter, h taken. */
    try {
        parser.flag("helpful", "h", "the help letter");
        std.test::fail("h is taken");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.option("width", "wd", "N", "two letters");
        std.test::fail("a short name has one letter");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    try {
        parser.option("count", "1", "N", "a digit");
        std.test::fail("a short name is a letter");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
    /* The longest name, every allowed byte and an upper-case short name are valid; the failed
       declarations added nothing. */
    parser.flag("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "", "64 bytes");
    parser.option("a.b_c-9", "V", "X", "every byte");
    /* Names and short names are declared once. */
    try {
        parser.option("verbose", "", "X", "twice");
        std.test::fail("verbose is declared");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::duplicate_name, 3usize);
    }
    try {
        parser.flag("very", "v", "the same letter");
        std.test::fail("v is declared");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::duplicate_name, 3usize);
    }
    try {
        parser.positional("verbose", "a positional name", false);
        std.test::fail("positional names share the names");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::duplicate_name, 3usize);
    }
    /* A required positional argument does not follow an optional one. */
    parser.positional("first", "required", true);
    parser.positional("second", "optional", false);
    try {
        parser.positional("third", "required after optional", true);
        std.test::fail("required after optional");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 5usize);
    }
    parser.positional("fourth", "optional after optional", false);
    /* remaining is declared once with a valid name; the index is the number of declarations. */
    try {
        parser.remaining("Rest", "invalid");
        std.test::fail("an upper-case remaining name is invalid");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 6usize);
    }
    parser.remaining("rest", "more");
    try {
        parser.remaining("more", "twice");
        std.test::fail("remaining is declared once");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 6usize);
    }
}

@test
void rejects_undeclared_names()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    str[2] arguments = {"tool", "run"};
    std.args::matches found = parser.parse(arguments[0usize..2usize]);
    try {
        found.has("colour") as void;
        std.test::fail("colour is not declared");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::unknown_name, 0usize);
    }
    try {
        o<std.string::string> value = found.value("help");
        drop value;
        std.test::fail("help is not a declared name");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::unknown_name, 0usize);
    }
}

@test(expect = std.args::args_error)
void rejects_an_extra_argument() throws std.args::args_error, std.alloc::alloc_error {
    std.args::parser parser = std.args::parser::create("strict", "one input");
    parser.positional("only", "one input", true);
    str[3] arguments = {"strict", "a", "b"};
    std.args::matches found = parser.parse(arguments[0usize..3usize]);
    drop found;
}

@test(allocations)
void declares_parses_and_copies()
    throws std.args::args_error, std.test::failure, std.alloc::alloc_error {
    std.args::parser parser = declared();
    str[8] arguments = {"tool", "-v", "--port", "8080", "run", "here", "one", "two"};
    std.args::matches found = parser.parse(arguments[0usize..8usize]);
    std.test::check(found.has("verbose"), "verbose was given");
    expect_value(&found, "port", "8080");
    expect_value(&found, "target", "here");
    array<std.string::string> rest = found.remaining();
    std.test::equal(len(rest), 2usize);
    std.test::equal_text(rest[1], "two");
    std.string::string text = parser.help();
    std.test::check(std.text::starts_with(text, "tool - an argument test\n"),
                    "the help starts with the program and summary");
}

/* M24-9: `help` is taken by the help, also as the name of the remaining arguments. */
@test
void remaining_cannot_be_named_help() throws std.test::failure, std.alloc::alloc_error,
    std.args::args_error {
    std.args::parser parser = std.args::parser::create("tool", "remaining names");
    parser.flag("verbose", "v", "more output");
    try {
        parser.remaining("help", "the rest");
        std.test::fail("help is taken by the help");
    } catch (std.args::args_error failure) {
        expect_error(failure, std.args::error_code::invalid_name, 1usize);
    }
}

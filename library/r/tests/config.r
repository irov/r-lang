module tests.std.config;
import std.test;
import std.args;
import std.config;
import std.text;

// The tests of std.config (Library R-SLIB-CONFIG-0001..0004), run in test mode (Core R-FUNC-0025).

/* The keys of most tests, declared in this order. */
protected std.config::config declared() throws std.config::config_error, std.alloc::alloc_error {
    std.config::config settings = std.config::config::create();
    settings.define("port", "8080", "listening port");
    settings.define("log.level", "info", "lowest level written");
    settings.define("log.json", "false", "JSON records");
    settings.define("limits.max_clients", "16", "connections at once");
    settings.define("api.token", "none", "access token");
    return move settings;
}

/* Checks the code and index of an error. */
protected void expect_error(std.config::config_error failure, std.config::error_code code,
                            usize index) throws std.test::failure, std.alloc::alloc_error {
    std.test::equal_text(core::enum_name(failure.code), core::enum_name(code));
    std.test::equal(failure.index, index);
}

/* Checks the current value and source of a key. */
protected void expect_key(const std.config::config* settings, str key, str value,
                          std.config::source origin)
    throws std.config::config_error, std.test::failure, std.alloc::alloc_error {
    std.test::equal_text(settings->get(key), value);
    std.test::equal_text(core::enum_name(settings->source_of(key)), core::enum_name(origin));
}

/* Writes the text to the file at the path, replacing it. */
@scoped
protected async void write_text(const std.fs::path* path, str text) throws std.error::fault {
    std.fs::open_file_options options = std.fs::open_file_options {
        .access = std.fs::access::write, .create = std.fs::create_mode::open_or_create,
        .truncate = true, .append = false, .follow_final_symlink = true};
    std.fs::path file = std.fs::path_clone(path);
    std.fs::file target = await std.fs::open_file(&file, options);
    task_scope(1) io { await std.fs::write_all_from(&target, text); }
    await std.fs::close_file(move target);
}

/* The offset that std.json reports for a text that is not JSON. */
protected usize json_offset(str text) throws std.test::failure, std.alloc::alloc_error {
    try {
        std.json::value document = std.json::parse(text);
        drop document;
    } catch (std.json::error failure) {
        return failure.offset;
    }
    std.test::fail("the text is JSON");
    return 0usize;
}

/* Loads a file that fails and checks its error, and that no key changed. */
@scoped
protected async void expect_file_error(const std.fs::path* path, str text,
                                       std.config::error_code code, usize index)
    throws std.config::config_error, std.test::failure, std.error::fault {
    task_scope(1) writing { await write_text(path, text); }
    std.config::config settings = declared();
    try {
        task_scope(1) loading { await settings.load_file(path); }
        std.test::fail("the file loaded");
    } catch (std.config::config_error failure) {
        expect_error(failure, code, index);
    }
    expect_key(&settings, "port", "8080", std.config::source::default_value);
    expect_key(&settings, "log.level", "info", std.config::source::default_value);
}

/* An object with one member, padded with spaces to `size` bytes. */
protected std.string::string padded_object(usize size) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("{\"port\": 1}");
    std.string::string chunk = std.string::create();
    for (usize index = 0usize; index < 4096usize; index += 1usize) { chunk.append(" "); }
    while (std.string::len(&text) + 4096usize <= size) { text.append(chunk); }
    while (std.string::len(&text) < size) { text.append(" "); }
    drop chunk;
    return move text;
}

@test
void declares_keys_with_defaults()
    throws std.config::config_error, std.test::failure, std.alloc::alloc_error {
    std.config::config settings = declared();
    expect_key(&settings, "port", "8080", std.config::source::default_value);
    expect_key(&settings, "log.level", "info", std.config::source::default_value);
    expect_key(&settings, "api.token", "none", std.config::source::default_value);
    /* set replaces the value and source of one key. */
    settings.set("port", "9000", std.config::source::file);
    expect_key(&settings, "port", "9000", std.config::source::file);
    settings.set("port", "9100", std.config::source::arguments);
    expect_key(&settings, "port", "9100", std.config::source::arguments);
    settings.set("log.level", "", std.config::source::environment);
    expect_key(&settings, "log.level", "", std.config::source::environment);
    expect_key(&settings, "log.json", "false", std.config::source::default_value);
    settings.define("empty", "", "an empty default");
    expect_key(&settings, "empty", "", std.config::source::default_value);
}

@test
void rejects_invalid_and_duplicate_keys()
    throws std.config::config_error, std.test::failure, std.alloc::alloc_error {
    std.config::config settings = declared();
    /* A key is 1..64 lowercase letters, digits and _ in segments separated by `.`. */
    try {
        settings.define("Port", "1", "upper case");
        std.test::fail("an upper-case key is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define("", "1", "empty");
        std.test::fail("an empty key is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define("log..level", "1", "an empty segment");
        std.test::fail("an empty segment is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define(".log", "1", "a leading dot");
        std.test::fail("a leading dot is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define("log.", "1", "a trailing dot");
        std.test::fail("a trailing dot is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define("max-clients", "1", "a dash");
        std.test::fail("a dash is invalid");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    try {
        settings.define("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "1",
                        "65 bytes");
        std.test::fail("a key has at most 64 bytes");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_key, 5usize);
    }
    /* Digits and _ anywhere, and 64 bytes, are valid; the failed declarations added nothing. */
    settings.define("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "1",
                    "64 bytes");
    settings.define("9.x_1._", "2", "digits and underscores");
    expect_key(&settings, "9.x_1._", "2", std.config::source::default_value);
    try {
        settings.define("port", "1", "twice");
        std.test::fail("port is declared");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::duplicate_key, 7usize);
    }
    /* An undeclared key is unknown_key with index zero. */
    try {
        settings.set("colour", "red", std.config::source::arguments);
        std.test::fail("colour is not declared");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::unknown_key, 0usize);
    }
    try {
        settings.get("log") as void;
        std.test::fail("log is not a key");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::unknown_key, 0usize);
    }
    try {
        settings.source_of("Port") as void;
        std.test::fail("keys are case-sensitive");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::unknown_key, 0usize);
    }
    try {
        settings.get_bool("colour") as void;
        std.test::fail("colour is not declared");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::unknown_key, 0usize);
    }
}

@test
void reads_typed_values()
    throws std.config::config_error, std.test::failure, std.alloc::alloc_error {
    std.config::config settings = std.config::config::create();
    settings.define("count", "-42", "");
    settings.define("size", "42", "");
    settings.define("largest", "18446744073709551615", "");
    settings.define("on", "true", "");
    settings.define("off", "false", "");
    settings.define("word", "yes", "");
    settings.define("upper", "TRUE", "");
    settings.define("fraction", "2.5", "");
    settings.define("empty", "", "");
    std.test::equal(settings.get_i64("count"), -42i64);
    std.test::equal(settings.get_i64("size"), 42i64);
    std.test::equal(settings.get_u64("size"), 42u64);
    std.test::equal(settings.get_u64("largest"), 18446744073709551615u64);
    std.test::equal(settings.get_bool("on"), true);
    std.test::equal(settings.get_bool("off"), false);
    /* Any other text is invalid_value with the index of the declaration. */
    try {
        settings.get_u64("count") as void;
        std.test::fail("-42 is not a u64");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 0usize);
    }
    try {
        settings.get_i64("largest") as void;
        std.test::fail("the largest u64 is not an i64");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 2usize);
    }
    try {
        settings.get_i64("word") as void;
        std.test::fail("yes is not a number");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 5usize);
    }
    try {
        settings.get_bool("word") as void;
        std.test::fail("yes is not a Boolean");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 5usize);
    }
    try {
        settings.get_bool("upper") as void;
        std.test::fail("only the exact spelling true is a Boolean");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 6usize);
    }
    try {
        settings.get_i64("fraction") as void;
        std.test::fail("2.5 is not an integer");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 7usize);
    }
    try {
        settings.get_u64("empty") as void;
        std.test::fail("an empty text is not a number");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 8usize);
    }
    try {
        settings.get_bool("size") as void;
        std.test::fail("42 is not a Boolean");
    } catch (std.config::config_error failure) {
        expect_error(failure, std.config::error_code::invalid_value, 1usize);
    }
}

@test(allocations)
void describes_values_and_sources()
    throws std.config::config_error, std.test::failure, std.alloc::alloc_error {
    std.config::config empty = std.config::config::create();
    std.string::string nothing = empty.describe();
    std.test::equal_text(nothing, "");
    std.config::config settings = declared();
    settings.define("db.password", "hunter2", "database password");
    settings.define("client_secret", "abc", "client secret");
    settings.define("tokens", "3", "a key that contains token");
    settings.define("tok.en", "visible", "no sensitive word");
    settings.set("port", "9000", std.config::source::file);
    settings.set("log.level", "warn", std.config::source::environment);
    settings.set("api.token", "xyz", std.config::source::arguments);
    std.string::string text = settings.describe();
    std.test::equal_text(text,
                         "port=9000 (file)\n"
                         "log.level=warn (environment)\n"
                         "log.json=false (default_value)\n"
                         "limits.max_clients=16 (default_value)\n"
                         "api.token=*** (arguments)\n"
                         "db.password=*** (default_value)\n"
                         "client_secret=*** (default_value)\n"
                         "tokens=*** (default_value)\n"
                         "tok.en=visible (default_value)\n");
}

@test
void loads_arguments() throws std.config::config_error, std.args::args_error, std.test::failure,
                               std.alloc::alloc_error {
    std.config::config settings = declared();
    settings.define("verbose", "false", "more output");
    settings.define("mode", "serve", "what to do");
    settings.define("quiet", "false", "less output");
    std.args::parser parser = std.args::parser::create("tool", "layers");
    parser.option("log.level", "", "LEVEL", "lowest level written");
    parser.option("port", "p", "PORT", "listening port");
    parser.flag("verbose", "v", "more output");
    parser.flag("quiet", "q", "less output");
    parser.positional("mode", "what to do", false);
    str[5] arguments = {"tool", "--log.level", "debug", "-v", "check"};
    std.args::matches found = parser.parse(arguments[0usize..5usize]);
    settings.load_arguments(&found);
    /* An option or positional argument gives its value, a flag `true`. */
    expect_key(&settings, "log.level", "debug", std.config::source::arguments);
    expect_key(&settings, "verbose", "true", std.config::source::arguments);
    expect_key(&settings, "mode", "check", std.config::source::arguments);
    /* Names that were not given, or not declared by the parser, change nothing. */
    expect_key(&settings, "port", "8080", std.config::source::default_value);
    expect_key(&settings, "quiet", "false", std.config::source::default_value);
    expect_key(&settings, "limits.max_clients", "16", std.config::source::default_value);
    std.test::equal(settings.get_bool("verbose"), true);
}

@test
void loads_the_environment() throws std.config::config_error, std.test::failure, std.error::fault {
    std.config::config settings = declared();
    std.env::set("RTEST_CONFIG_LOG_LEVEL", "error");
    std.env::set("RTEST_CONFIG_LIMITS_MAX_CLIENTS", "64");
    std.env::set("RTEST_CONFIG_API_TOKEN", "");
    std.env::set("rtest_config_log_json", "true");
    std.env::remove("RTEST_CONFIG_PORT");
    std.env::remove("RTEST_CONFIG_LOG_JSON");
    settings.load_environment("RTEST_CONFIG");
    expect_key(&settings, "log.level", "error", std.config::source::environment);
    expect_key(&settings, "limits.max_clients", "64", std.config::source::environment);
    std.test::equal(settings.get_u64("limits.max_clients"), 64u64);
    expect_key(&settings, "api.token", "", std.config::source::environment);
    /* Unset variables, and names in another case, change nothing. */
    expect_key(&settings, "port", "8080", std.config::source::default_value);
    expect_key(&settings, "log.json", "false", std.config::source::default_value);
    /* Another prefix names other variables. */
    std.env::set("RTEST_OTHER_PORT", "7");
    settings.load_environment("RTEST_OTHER");
    expect_key(&settings, "port", "7", std.config::source::environment);
    expect_key(&settings, "log.level", "error", std.config::source::environment);
    std.env::remove("RTEST_CONFIG_LOG_LEVEL");
    std.env::remove("RTEST_CONFIG_LIMITS_MAX_CLIENTS");
    std.env::remove("RTEST_CONFIG_API_TOKEN");
    std.env::remove("rtest_config_log_json");
    std.env::remove("RTEST_OTHER_PORT");
}

@test
async void loads_a_file_under_later_layers()
    throws std.config::config_error, std.args::args_error, std.test::failure, std.error::fault {
    std.config::config settings = declared();
    settings.define("a.b.c", "shallow", "a nested key");
    settings.define("ratio", "1", "a number");
    settings.define("enabled", "true", "a flag");
    std.fs::path path = std.fs::path_from_utf8("rtest_config_layers.json");
    task_scope(1) writing {
        await write_text(&path, "{\"port\": 9000, \"log\": {\"level\": \"warn\", \"json\": true},"
                                " \"api\": {\"token\": null},"
                                " \"a\": {\"b\": {\"c\": \"d\\u0065ep\"}}, \"ratio\": 2.50,"
                                " \"limits.max_clients\": 32, \"enabled\": false}");
    }
    task_scope(1) loading { await settings.load_file(&path); }
    /* Members name keys, objects prefix their members, strings give their text, numbers keep
       their spelling, true and false give theirs, and null changes nothing. */
    expect_key(&settings, "port", "9000", std.config::source::file);
    expect_key(&settings, "log.level", "warn", std.config::source::file);
    expect_key(&settings, "log.json", "true", std.config::source::file);
    expect_key(&settings, "enabled", "false", std.config::source::file);
    expect_key(&settings, "a.b.c", "deep", std.config::source::file);
    expect_key(&settings, "ratio", "2.50", std.config::source::file);
    expect_key(&settings, "limits.max_clients", "32", std.config::source::file);
    expect_key(&settings, "api.token", "none", std.config::source::default_value);
    /* The environment replaces the file, and the arguments the environment. */
    std.env::set("RTEST_LAYERS_LOG_LEVEL", "error");
    std.env::set("RTEST_LAYERS_PORT", "9100");
    settings.load_environment("RTEST_LAYERS");
    std.env::remove("RTEST_LAYERS_LOG_LEVEL");
    std.env::remove("RTEST_LAYERS_PORT");
    expect_key(&settings, "log.level", "error", std.config::source::environment);
    expect_key(&settings, "port", "9100", std.config::source::environment);
    std.args::parser parser = std.args::parser::create("tool", "layers");
    parser.option("log.level", "", "LEVEL", "lowest level written");
    str[3] arguments = {"tool", "--log.level", "debug"};
    std.args::matches found = parser.parse(arguments[0usize..3usize]);
    settings.load_arguments(&found);
    expect_key(&settings, "log.level", "debug", std.config::source::arguments);
    expect_key(&settings, "port", "9100", std.config::source::environment);
    std.test::equal(settings.get_i64("port"), 9100i64);
    std.test::equal(settings.get_bool("log.json"), true);
}

@test
async void keeps_every_key_when_a_file_fails()
    throws std.config::config_error, std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_config_failures.json");
    /* A member that names no key, an array, and a document that is not an object. */
    task_scope(1) unknown {
        await expect_file_error(&path,
                                "{\"port\": 1, \"log\": {\"level\": \"warn\"},"
                                " \"colour\": \"red\"}",
                                std.config::error_code::unknown_key, 0usize);
    }
    task_scope(1) nested_unknown {
        await expect_file_error(&path, "{\"port\": 1, \"log\": {\"colour\": \"red\"}}",
                                std.config::error_code::unknown_key, 0usize);
    }
    task_scope(1) listed {
        await expect_file_error(&path, "{\"port\": 2, \"log\": {\"json\": [1, 2]}}",
                                std.config::error_code::invalid_value, 2usize);
    }
    task_scope(1) scalar {
        await expect_file_error(&path, "[1]", std.config::error_code::invalid_file, 0usize);
    }
    task_scope(1) text {
        await expect_file_error(&path, "\"port\"", std.config::error_code::invalid_file, 0usize);
    }
    /* A text that is not JSON reports the offset of std.json::error. */
    str broken = "{\"port\": }";
    usize broken_offset = json_offset(broken);
    task_scope(1) not_json {
        await expect_file_error(&path, broken, std.config::error_code::invalid_file, broken_offset);
    }
    str trailing = "{\"port\": 1} x";
    usize trailing_offset = json_offset(trailing);
    task_scope(1) not_json_again {
        await expect_file_error(&path, trailing, std.config::error_code::invalid_file,
                                trailing_offset);
    }
    usize empty_offset = json_offset("");
    task_scope(1) empty {
        await expect_file_error(&path, "", std.config::error_code::invalid_file, empty_offset);
    }
}

@test(expect = std.config::config_error)
async void rejects_an_unknown_file_key() throws std.config::config_error, std.error::fault {
    std.config::config settings = declared();
    std.fs::path path = std.fs::path_from_utf8("rtest_config_unknown.json");
    task_scope(1) writing { await write_text(&path, "{\"colour\": \"red\"}"); }
    task_scope(1) loading { await settings.load_file(&path); }
}

@test
async void reads_at_most_one_mebibyte()
    throws std.config::config_error, std.test::failure, std.error::fault {
    std.config::config settings = declared();
    std.fs::path path = std.fs::path_from_utf8("rtest_config_large.json");
    std.string::string largest = padded_object(1048576usize);
    std.test::equal(std.string::len(&largest), 1048576usize);
    task_scope(1) writing { await write_text(&path, largest); }
    task_scope(1) loading { await settings.load_file(&path); }
    expect_key(&settings, "port", "1", std.config::source::file);
    std.config::config fresh = declared();
    std.string::string larger = padded_object(1048577usize);
    task_scope(1) writing_more { await write_text(&path, larger); }
    try {
        task_scope(1) loading_more { await fresh.load_file(&path); }
        std.test::fail("a file above 1 MiB loaded");
    } catch (std.fs::fs_error failure) {
        std.test::equal_text(core::enum_name(failure.code), "file_too_large");
    }
    expect_key(&fresh, "port", "8080", std.config::source::default_value);
}

/* The settings of a server from its environment variables. */
struct Server {
    @json(name = "PG_HOST") std.string::string host;
    @json(name = "PG_PORT", optional) u16 port = 5432u16;
    @json(name = "PROD") bool production;
    @json(name = "API_KEY") o<std.string::string> api_key;
    @json(name = "ORIGINS", optional) array<std.string::string> origins;
    @json(name = "RATIO", optional) f64 ratio = 0.5;
};

/* Checks the code and name of a field error. */
protected void expect_field(std.config::field_error failure, std.config::error_code code, str name)
    throws std.test::failure, std.alloc::alloc_error {
    std.test::check(failure.code == code, "error code");
    std.test::equal_text(failure.name, name);
}

// R-SLIB-CONFIG-0004: variables convert by the type of their field; absent optional fields keep
// their defaults, an absent o<T> is none, and a missing or invalid value names its variable.
@test
void reads_a_struct_from_the_environment() throws std.test::failure, std.error::fault {
    std.env::set("ARENA_PG_HOST", "db.local");
    std.env::set("ARENA_PROD", "Yes");
    std.env::set("ARENA_ORIGINS", "a.test, b.test");
    std.env::remove("ARENA_PG_PORT");
    std.env::remove("ARENA_API_KEY");
    std.env::remove("ARENA_RATIO");
    try {
        Server read = std.config::from_environment::<Server>("ARENA_");
        std.test::equal_text(read.host, "db.local");
        std.test::equal(read.port, 5432u16);
        std.test::check(read.production, "production");
        std.test::equal(len(read.origins), 2usize);
        std.test::equal_text(read.origins[1usize], "b.test");
        switch (read.api_key) {
        case variant o::some(_): std.test::fail("no API key");
        case variant o::none: break;
        }
        std.env::set("ARENA_PG_PORT", "6543");
        std.env::set("ARENA_API_KEY", "k-1");
        std.env::set("ARENA_RATIO", "0.25");
        Server second = std.config::from_environment::<Server>("ARENA_");
        std.test::equal(second.port, 6543u16);
        std.test::check(second.ratio == 0.25, "ratio");
        switch (second.api_key) {
        case variant o::some(key): std.test::equal_text(*key, "k-1");
        case variant o::none: std.test::fail("an API key");
        }
    } catch (std.config::field_error failure) {
        std.test::fail(failure.name);
    }
    std.env::set("ARENA_PG_PORT", "70000");
    try {
        Server refused = std.config::from_environment::<Server>("ARENA_");
        drop refused;
        std.test::fail("a port beyond u16");
    } catch (std.config::field_error failure) {
        expect_field(move failure, std.config::error_code::invalid_value, "ARENA_PG_PORT");
    }
    std.env::set("ARENA_PG_PORT", "1");
    std.env::set("ARENA_PROD", "maybe");
    try {
        Server refused = std.config::from_environment::<Server>("ARENA_");
        drop refused;
        std.test::fail("a boolean that is not one");
    } catch (std.config::field_error failure) {
        expect_field(move failure, std.config::error_code::invalid_value, "ARENA_PROD");
    }
    std.env::remove("ARENA_PG_HOST");
    try {
        Server refused = std.config::from_environment::<Server>("ARENA_");
        drop refused;
        std.test::fail("a missing host");
    } catch (std.config::field_error failure) {
        expect_field(move failure, std.config::error_code::missing_value, "ARENA_PG_HOST");
    }
}

struct Limits {
    u64 max_clients;
    std.string::string level;
};

// R-SLIB-CONFIG-0004: a struct from the declared keys of a configuration after its layers.
@test
void decodes_a_configuration() throws std.test::failure, std.config::config_error, std.alloc::alloc_error {
    std.config::config settings = std.config::config::create();
    settings.define("max_clients", "16", "connections at once");
    settings.define("level", "warn", "lowest level written");
    settings.set("max_clients", "64", std.config::source::arguments);
    try {
        Limits read = settings.decode::<Limits>();
        std.test::equal(read.max_clients, 64u64);
        std.test::equal_text(read.level, "warn");
    } catch (std.config::field_error failure) {
        std.test::fail(failure.name);
    }
    settings.set("max_clients", "many", std.config::source::environment);
    try {
        Limits refused = settings.decode::<Limits>();
        drop refused;
        std.test::fail("a count that is not a number");
    } catch (std.config::field_error failure) {
        expect_field(move failure, std.config::error_code::invalid_value, "max_clients");
    }
}

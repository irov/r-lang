module test.codegen.library_config;
import std.args;
import std.config;
import std.text;

// R-SLIB-CONFIG-0001..0003 (M23): declarations, the layers default < file < environment <
// arguments with their sources, the flattening of a JSON file, typed reads and every error.

error Failed { i32 code; };
struct Status { i32 value; };

std.config::config declared() throws std.config::config_error, std.alloc::alloc_error {
    std.config::config settings = std.config::config::create();
    settings.define("port", "8080", "listening port");
    settings.define("log.level", "info", "lowest level written");
    settings.define("log.json", "false", "JSON records");
    settings.define("limits.max_clients", "16", "connections at once");
    settings.define("api.token", "none", "access token");
    return move settings;
}

/* Writes the text to the file at the path, replacing it. */
@scoped
async void write_text(const std.fs::path* path, str text) throws std.error::fault {
    std.fs::open_file_options options = std.fs::open_file_options {
        .access = std.fs::access::write, .create = std.fs::create_mode::open_or_create,
        .truncate = true, .append = false, .follow_final_symlink = true};
    std.fs::path file = std.fs::path_clone(path);
    std.fs::file target = await std.fs::open_file(&file, options);
    task_scope(1) io { await std.fs::write_all_from(&target, text); }
    await std.fs::close_file(move target);
}

@scoped
async i32 load_error(const std.fs::path* path, str text)
    throws std.config::config_error, std.error::fault {
    task_scope(1) writing { await write_text(path, text); }
    std.config::config settings = declared();
    try {
        task_scope(1) loading { await settings.load_file(path); }
        return 0;
    } catch (std.config::config_error failure) {
        /* A file that fails changes no key. */
        if (std.bytes::equal(settings.get("port"), "8080") == false) { return 50; }
        return core::enum_ordinal(failure.code) as i32 + 1;
    }
}

async i32 checks() throws std.config::config_error, std.args::args_error, std.error::fault, Failed {
    std.config::config settings = declared();
    settings.define("verbose", "false", "more output");
    throw (std.bytes::equal(settings.get("port"), "8080") == false) Failed {.code = 1};
    throw (settings.source_of("port") != std.config::source::default_value) Failed {.code = 2};
    std.fs::path path = std.fs::path_from_utf8("library_config_fixture.json");
    task_scope(1) writing {
        await write_text(&path, "{\"port\": 9000, \"log\": {\"level\": \"warn\", \"json\": true}, \"api\": {\"token\": null}}");
    }
    task_scope(1) loading { await settings.load_file(&path); }
    throw (settings.get_i64("port") != 9000i64) Failed {.code = 3};
    throw (std.bytes::equal(settings.get("log.level"), "warn") == false) Failed {.code = 4};
    throw (settings.get_bool("log.json") == false) Failed {.code = 5};
    throw (settings.source_of("log.level") != std.config::source::file) Failed {.code = 6};
    throw (settings.source_of("api.token") != std.config::source::default_value) Failed {.code = 7};
    /* The environment replaces the file, the arguments the environment. */
    std.env::set("LIBRARY_CONFIG_LOG_LEVEL", "error");
    std.env::set("LIBRARY_CONFIG_LIMITS_MAX_CLIENTS", "64");
    settings.load_environment("LIBRARY_CONFIG");
    throw (std.bytes::equal(settings.get("log.level"), "error") == false) Failed {.code = 8};
    throw (settings.get_u64("limits.max_clients") != 64u64) Failed {.code = 9};
    throw (settings.source_of("limits.max_clients") != std.config::source::environment) Failed {.code = 10};
    std.args::parser parser = std.args::parser::create("fixture", "layers");
    parser.option("log.level", "", "LEVEL", "lowest level written");
    parser.flag("verbose", "v", "more output");
    str[4] arguments = {"fixture", "--log.level", "debug", "-v"};
    std.args::matches found = parser.parse(arguments[0usize..4usize]);
    settings.load_arguments(&found);
    throw (settings.get_bool("verbose") == false ||
           settings.source_of("verbose") != std.config::source::arguments) Failed {.code = 26};
    throw (std.bytes::equal(settings.get("log.level"), "debug") == false) Failed {.code = 11};
    throw (settings.source_of("log.level") != std.config::source::arguments) Failed {.code = 12};
    settings.set("api.token", "hunter2", std.config::source::arguments);
    std.string::string text = settings.describe();
    throw (std.text::contains(text.as_str(), "api.token=*** (arguments)\n") == false ||
           std.text::contains(text.as_str(), "hunter2") == true ||
           std.text::contains(text.as_str(), "port=9000 (file)\n") == false)
        Failed {.code = 13};
    /* Typed reads of a text that is not one report invalid_value with the declaration. */
    try {
        settings.get_bool("log.level") as void;
        throw Failed {.code = 14};
    } catch (std.config::config_error failure) {
        throw (failure.code != std.config::error_code::invalid_value || failure.index != 1usize)
            Failed {.code = 15};
    }
    try {
        settings.get("missing") as void;
        throw Failed {.code = 16};
    } catch (std.config::config_error failure) {
        throw (failure.code != std.config::error_code::unknown_key) Failed {.code = 17};
    }
    try {
        settings.define("Port", "1", "upper case");
        throw Failed {.code = 18};
    } catch (std.config::config_error failure) {
        throw (failure.code != std.config::error_code::invalid_key || failure.index != 6usize)
            Failed {.code = 19};
    }
    try {
        settings.define("port", "1", "twice");
        throw Failed {.code = 20};
    } catch (std.config::config_error failure) {
        throw (failure.code != std.config::error_code::duplicate_key) Failed {.code = 21};
    }
    /* File errors: an unknown key, an array, a document that is not an object or not JSON. */
    Status status = {.value = 0};
    task_scope(1) unknown { status.value = await load_error(&path, "{\"port\": 1, \"colour\": \"red\"}"); }
    throw (status.value != core::enum_ordinal(std.config::error_code::unknown_key) as i32 + 1)
        Failed {.code = 22};
    task_scope(1) listed { status.value = await load_error(&path, "{\"port\": 2, \"log\": {\"json\": [1, 2]}}"); }
    throw (status.value != core::enum_ordinal(std.config::error_code::invalid_value) as i32 + 1)
        Failed {.code = 23};
    task_scope(1) scalar { status.value = await load_error(&path, "[1]"); }
    throw (status.value != core::enum_ordinal(std.config::error_code::invalid_file) as i32 + 1)
        Failed {.code = 24};
    task_scope(1) broken { status.value = await load_error(&path, "{\"port\": }"); }
    throw (status.value != core::enum_ordinal(std.config::error_code::invalid_file) as i32 + 1)
        Failed {.code = 25};
    try {
        settings.set("missing", "1", std.config::source::arguments);
        throw Failed {.code = 27};
    } catch (std.config::config_error failure) {
        throw (failure.code != std.config::error_code::unknown_key || failure.index != 0usize)
            Failed {.code = 28};
    }
    return 0;
}

async i32 main() {
    try {
        return await checks();
    } catch (Failed failure) {
        return failure.code;
    } catch (std.config::config_error failure) {
        return 80 + core::enum_ordinal(failure.code) as i32;
    } catch (std.args::args_error failure) {
        failure as void;
        return 90;
    } catch (std.error::fault failure) {
        failure as void;
        return 99;
    }
}

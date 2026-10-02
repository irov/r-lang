module test.codegen.library_log;
import std.log;
import std.text;

// R-SLIB-LOG-0001..0003 (M23): records in both formats through a file writer, quoting,
// masking by kind, by default name and by mask(), thresholds, shared loggers, the task
// identifier, dropped records, a queue of capacity zero and the rewriting of field names.

error Failed { i32 code; };
struct Count { u64 value; };

@scoped
async void truncate(const std.fs::path* path) throws std.error::fault {
    std.fs::open_file_options options = std.fs::open_file_options {
        .access = std.fs::access::write, .create = std.fs::create_mode::open_or_create,
        .truncate = true, .append = false, .follow_final_symlink = true};
    std.fs::path file = std.fs::path_clone(path);
    std.fs::file target = await std.fs::open_file(&file, options);
    await std.fs::close_file(move target);
}

async std.string::string read_text(std.fs::path file) throws std.error::fault {
    bytes content = await std.fs::read_file(&file, 65536usize);
    return std.string::from_str(std.utf8::validate(content.as_slice()));
}

async i32 checks() throws std.error::fault, Failed {
    std.fs::path path = std.fs::path_from_utf8("library_log_fixture.log");
    task_scope(1) clearing { await truncate(&path); }
    std.log::writer sink = std.log::writer::create(8usize);
    std.log::logger text_log = sink.logger(std.log::level::info, std.log::format::text);
    std.log::logger json_log = sink.logger(std.log::level::debug, std.log::format::json);
    json_log.mask("session");
    throw (text_log.enabled(std.log::level::debug) == true ||
           json_log.enabled(std.log::level::debug) == false) Failed {.code = 1};
    std.log::fields extra = std.log::fields::create();
    extra.text("path", "/a b");
    extra.number("count", -3i64);
    extra.flag("ok", true);
    extra.secret("key");
    extra.text("password", "p");
    extra.text("Session", "s");
    throw (extra.count() != 6usize) Failed {.code = 2};
    text_log.log(std.log::level::info, "hello \"world\"", &extra);
    text_log.debug("hidden");
    json_log.log(std.log::level::debug, "json record", &extra);
    std.log::logger copy = text_log.share();
    copy.warn("from clone");
    throw (text_log.dropped() != 0u64) Failed {.code = 3};
    drop text_log;
    drop json_log;
    drop copy;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    throw (written.value != 3u64) Failed {.code = 4};
    std.string::string text = await read_text(std.fs::path_clone(&path));
    str lines = text.as_str();
    throw (std.text::contains(lines, " level=info task=") == false ||
           std.text::contains(lines, " message=\"hello \\\"world\\\"\" path=\"/a b\" count=-3 ok=true key=*** password=*** Session=s\n") == false)
        Failed {.code = 5};
    throw (std.text::contains(lines, "\"level\":\"debug\",\"task\":") == false ||
           std.text::contains(lines, "\"message\":\"json record\",\"path\":\"/a b\",\"count\":-3,\"ok\":true,\"key\":\"***\",\"password\":\"***\",\"Session\":\"***\"}\n") == false)
        Failed {.code = 6};
    throw (std.text::contains(lines, "level=warn") == false || std.text::contains(lines, "hidden") == true ||
           std.text::contains(lines, "task=0 ") == true)
        Failed {.code = 7};
    /* A full queue drops and counts; the writer reports the count last. */
    task_scope(1) clearing_again { await truncate(&path); }
    std.log::writer small = std.log::writer::create(1usize);
    std.log::logger busy = small.logger(std.log::level::trace, std.log::format::text);
    busy.info("first");
    busy.info("second");
    busy.trace("third");
    throw (busy.dropped() != 2u64) Failed {.code = 8};
    drop busy;
    Count small_written = {.value = 0u64};
    task_scope(1) draining { small_written.value = await std.log::writer::to_file(move small, &path); }
    throw (small_written.value != 1u64) Failed {.code = 9};
    std.string::string drained = await read_text(std.fs::path_clone(&path));
    throw (std.text::ends_with(drained.as_str(), "message=\"records dropped\" dropped=2\n") == false)
        Failed {.code = 10};
    /* A capacity of zero holds one line. */
    std.log::writer tiny = std.log::writer::create(0usize);
    std.log::logger tiny_log = tiny.logger(std.log::level::info, std.log::format::text);
    tiny_log.info("kept");
    tiny_log.info("dropped");
    throw (tiny_log.dropped() != 1u64) Failed {.code = 11};
    drop tiny_log;
    drop tiny;
    /* Names that could break a record are rewritten, and mask() compares the rewritten name. */
    task_scope(1) clearing_names { await truncate(&path); }
    std.log::writer names = std.log::writer::create(2usize);
    std.log::logger named = names.logger(std.log::level::info, std.log::format::text);
    named.mask("odd name");
    std.log::fields odd = std.log::fields::create();
    odd.text("time", "t");
    odd.text("bad name", "b");
    odd.text("", "e");
    odd.text("é", "u");
    odd.text("odd name", "hidden");
    odd.text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "long");
    named.log(std.log::level::info, "names", &odd);
    drop named;
    Count names_written = {.value = 0u64};
    task_scope(1) naming { names_written.value = await std.log::writer::to_file(move names, &path); }
    std.string::string rewritten = await read_text(std.fs::path_clone(&path));
    throw (names_written.value != 1u64 ||
           std.text::ends_with(rewritten.as_str(), " message=names _time=t bad_name=b _=e __=u odd_name=*** aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa=long\n") == false)
        Failed {.code = 12};
    return 0;
}

async i32 main() {
    try {
        return await checks();
    } catch (Failed failure) {
        return failure.code;
    } catch (std.error::fault failure) {
        failure as void;
        return 99;
    }
}

module tests.std.log;
import std.test;
import std.log;
import std.text;
import std.time;

// The tests of std.log (Library R-SLIB-LOG-0001..0003), run in test mode (Core R-FUNC-0025).
// The records go through std.log::writer::to_file into files of the current directory and are
// read back; each line is checked exactly except for its time, which is checked to be RFC 3339
// with three fractional digits.

protected struct Count { u64 value; };

/* Creates or empties the file at the path. */
@scoped
protected async void truncate(const std.fs::path* path) throws std.error::fault {
    std.fs::open_file_options options = std.fs::open_file_options {
        .access = std.fs::access::write, .create = std.fs::create_mode::open_or_create,
        .truncate = true, .append = false, .follow_final_symlink = true};
    std.fs::path file = std.fs::path_clone(path);
    std.fs::file target = await std.fs::open_file(&file, options);
    await std.fs::close_file(move target);
}

/* Removes the file at a name of the current directory when it exists. */
@scoped
protected async void remove_file(const std.fs::path* name)
    throws std.test::failure, std.error::fault {
    std.fs::path here = std.fs::path_from_utf8(".");
    std.fs::directory root = await here.open_directory();
    try {
        await root.remove_file_beneath(name);
    } catch (std.fs::fs_error failure) {
        std.test::equal_text(core::enum_name(failure.code), "not_found");
    }
    await (move root).close();
}

/* The text of the file. */
protected async std.string::string read_text(std.fs::path file) throws std.error::fault {
    bytes content = await std.fs::read_file(&file, 65536usize);
    return std.string::from_str(std.utf8::validate(content.as_slice()));
}

/* The line at `wanted` of a text, without its line feed. */
protected str line_at(str text, usize wanted) {
    usize index = 0usize;
    for (str line in std.text::split(text, "\n")) {
        if (index == wanted) { return line; }
        index += 1usize;
    }
    return "";
}

/* `before`, the task identifier and `after`. */
protected std.string::string with_task(str before, u64 id, str after)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str(before);
    std.string::string number = f"{id}";
    text.append(number.as_str());
    text.append(after);
    return move text;
}

/* Checks a record: `head`, the time in RFC 3339 with three fractional digits, then `rest`
   exactly, or ignoring ASCII case. */
protected void expect_record(str line, str head, str rest, bool ignore_case)
    throws std.test::failure, std.error::fault {
    const u8[] bytes = line;
    if (std.text::starts_with(line, head) == false ||
        len(bytes) != len(head) + 24usize + len(rest)) {
        std.string::string message = f"unexpected record {line}";
        std.test::fail(message.as_str());
        return;
    }
    usize start = len(head);
    str stamp = core::validate_utf8(bytes[start..start + 24usize]);
    std.time::system_time parsed = std.time::parse_rfc3339(stamp);
    parsed as void;
    std.test::equal(bytes[start + 19usize], 46u8);
    std.test::equal(bytes[start + 23usize], 90u8);
    str tail = core::validate_utf8(bytes[start + 24usize..len(bytes)]);
    if (ignore_case == true) {
        std.test::check(std.text::equal_ignore_ascii_case(tail, rest), "the rest of the record");
    } else {
        std.test::equal_text(tail, rest);
    }
}

/* Parses a JSON record and checks the text of one member. */
protected void expect_json_member(str line, str name, str value)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        std.json::value document = std.json::parse(line);
        std.test::equal_text(core::enum_name(std.json::kind(&document)), "object");
        o<const std.json::value*> member = std.json::find(&document, name);
        switch (member) {
        case variant o::some(found): std.test::equal_text(std.json::text(*found), value);
        case variant o::none: std.test::fail(name);
        }
    } catch (std.json::error failure) {
        std.string::string message = f"a record is not JSON at {failure.offset}";
        std.test::fail(message.as_str());
    }
}

/* The allocation runs of this test show that building fields fails only with alloc_error. A
   test that creates a writer cannot run them: an allocation failure of the shared state of
   std.log::writer::create is the allocation_failure panic of `new arc` (Core R-OBJ-0006). */
@test(allocations)
void collects_fields() throws std.test::failure, std.alloc::alloc_error {
    std.log::fields extra = std.log::fields::create();
    std.test::equal(extra.count(), 0usize);
    extra.text("path", "/a b");
    extra.number("count", -3i64);
    extra.flag("ok", true);
    extra.secret("key");
    extra.text("path", "again");
    extra.text("odd name", "rewritten");
    std.test::equal(extra.count(), 6usize);
    std.log::fields empty = std.log::fields::create();
    std.test::equal(empty.count(), 0usize);
}

@test
void queues_by_threshold_and_capacity() throws std.test::failure, std.error::fault {
    std.log::writer sink = std.log::writer::create(1usize);
    std.log::logger warnings = sink.logger(std.log::level::warn, std.log::format::text);
    std.test::check(warnings.enabled(std.log::level::trace) == false, "trace is below warn");
    std.test::check(warnings.enabled(std.log::level::debug) == false, "debug is below warn");
    std.test::check(warnings.enabled(std.log::level::info) == false, "info is below warn");
    std.test::check(warnings.enabled(std.log::level::warn), "warn reaches warn");
    std.test::check(warnings.enabled(std.log::level::error), "error is above warn");
    std.log::logger everything = sink.logger(std.log::level::trace, std.log::format::json);
    std.test::check(everything.enabled(std.log::level::trace), "trace reaches trace");
    std.test::check(everything.enabled(std.log::level::error), "error is above trace");
    std.log::logger errors = sink.logger(std.log::level::error, std.log::format::text);
    std.test::check(errors.enabled(std.log::level::warn) == false, "warn is below error");
    std.test::check(errors.enabled(std.log::level::error), "error reaches error");
    /* Records below the threshold are not formatted, so they allocate nothing, and not queued:
       the queue of one line takes the warn record, and only the error record after it is
       dropped, counted for every logger of the queue. */
    std.log::fields extra = std.log::fields::create();
    extra.text("path", "/a b");
    std.test::fail_allocation_at(0u64);
    warnings.trace("a");
    warnings.debug("b");
    warnings.log(std.log::level::info, "c", &extra);
    errors.warn("d");
    u64 attempts = std.test::allocation_attempts();
    std.test::equal(attempts, 0u64);
    std.test::equal(warnings.dropped(), 0u64);
    warnings.log(std.log::level::warn, "e", &extra);
    std.test::equal(warnings.dropped(), 0u64);
    warnings.error("f");
    std.test::equal(warnings.dropped(), 1u64);
    std.test::equal(everything.dropped(), 1u64);
    std.test::equal(errors.dropped(), 1u64);
    /* A queue of three lines; a shared logger counts with the original. */
    std.log::writer wider = std.log::writer::create(3usize);
    std.log::logger first = wider.logger(std.log::level::info, std.log::format::text);
    std.log::logger second = first.share();
    for (u32 index = 0u32; index < 5u32; index += 1u32) { first.info("record"); }
    std.test::equal(first.dropped(), 2u64);
    std.test::equal(second.dropped(), 2u64);
    second.warn("one more");
    std.test::equal(first.dropped(), 3u64);
    std.test::equal(warnings.dropped(), 1u64);
    /* A capacity of zero holds one line. */
    std.log::writer tiny = std.log::writer::create(0usize);
    std.log::logger tiny_log = tiny.logger(std.log::level::info, std.log::format::json);
    tiny_log.info("kept");
    std.test::equal(tiny_log.dropped(), 0u64);
    tiny_log.info("dropped");
    std.test::equal(tiny_log.dropped(), 1u64);
    /* A record whose writer has ended is dropped and counted. */
    std.log::writer gone = std.log::writer::create(4usize);
    std.log::logger orphan = gone.logger(std.log::level::info, std.log::format::text);
    drop gone;
    orphan.info("nobody reads this");
    std.test::equal(orphan.dropped(), 1u64);
}

@test
async void writes_text_records() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_text.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer sink = std.log::writer::create(16usize);
    std.log::logger everything = sink.logger(std.log::level::trace, std.log::format::text);
    std.log::logger quiet = sink.logger(std.log::level::info, std.log::format::text);
    everything.trace("t");
    everything.debug("d");
    everything.info("hello");
    everything.warn("hello world");
    everything.error("");
    quiet.debug("hidden");
    quiet.trace("hidden");
    std.log::fields extra = std.log::fields::create();
    extra.text("path", "/a b");
    extra.number("count", -3i64);
    extra.number("big", 9223372036854775807i64);
    extra.flag("ok", true);
    extra.flag("off", false);
    extra.text("plain", "value");
    extra.text("empty", "");
    extra.text("eq", "a=b");
    extra.text("controls", "l\nt\tr\rz\0b\x07");
    extra.text("unicode", "é€");
    quiet.log(std.log::level::info, "say \"hi\" \\ now", &extra);
    quiet.info("\x7f");
    drop everything;
    drop quiet;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(written.value, 7u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 7usize);
    std.test::check(std.text::ends_with(text, "\n"), "each line ends with a line feed");
    std.string::string trace = with_task(" level=trace task=", id, " message=t");
    expect_record(line_at(text, 0usize), "time=", trace.as_str(), false);
    std.string::string debug = with_task(" level=debug task=", id, " message=d");
    expect_record(line_at(text, 1usize), "time=", debug.as_str(), false);
    std.string::string info = with_task(" level=info task=", id, " message=hello");
    expect_record(line_at(text, 2usize), "time=", info.as_str(), false);
    std.string::string warn = with_task(" level=warn task=", id, " message=\"hello world\"");
    expect_record(line_at(text, 3usize), "time=", warn.as_str(), false);
    std.string::string error_record = with_task(" level=error task=", id, " message=\"\"");
    expect_record(line_at(text, 4usize), "time=", error_record.as_str(), false);
    /* Quoting and escapes of text values; numbers and flags as they are. */
    std.string::string fields = with_task(" level=info task=", id,
                                          " message=\"say \\\"hi\\\" \\\\ now\" path=\"/a b\""
                                          " count=-3 big=9223372036854775807 ok=true off=false"
                                          " plain=value empty=\"\" eq=\"a=b\""
                                          " controls=\"l\\nt\\tr\\rz\\u0000b\\u0007\" unicode=é€");
    expect_record(line_at(text, 5usize), "time=", fields.as_str(), false);
    /* DEL is a control character; the case of the hexadecimal digits is not specified. */
    std.string::string deleted = with_task(" level=info task=", id, " message=\"\\u007f\"");
    expect_record(line_at(text, 6usize), "time=", deleted.as_str(), true);
}

@test
async void writes_json_records() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_json.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer sink = std.log::writer::create(8usize);
    std.log::logger json = sink.logger(std.log::level::debug, std.log::format::json);
    json.trace("hidden");
    json.debug("json record");
    std.log::fields extra = std.log::fields::create();
    extra.text("path", "/a b");
    extra.number("count", -3i64);
    extra.flag("ok", true);
    extra.flag("off", false);
    extra.secret("key");
    extra.text("empty", "");
    extra.text("password", "p");
    extra.text("plain", "value");
    extra.text("controls", "\t\x01");
    json.log(std.log::level::warn, "say \"hi\" \\ now\n", &extra);
    json.error("");
    drop json;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(written.value, 3u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 3usize);
    std.string::string debug = with_task("\",\"level\":\"debug\",\"task\":", id,
                                         ",\"message\":\"json record\"}");
    expect_record(line_at(text, 0usize), "{\"time\":\"", debug.as_str(), false);
    std.string::string warn = with_task("\",\"level\":\"warn\",\"task\":", id,
                                        ",\"message\":\"say \\\"hi\\\" \\\\ now\\n\","
                                        "\"path\":\"/a b\",\"count\":-3,\"ok\":true,\"off\":false,"
                                        "\"key\":\"***\",\"empty\":\"\",\"password\":\"***\","
                                        "\"plain\":\"value\",\"controls\":\"\\t\\u0001\"}");
    expect_record(line_at(text, 1usize), "{\"time\":\"", warn.as_str(), false);
    std.string::string error_record = with_task("\",\"level\":\"error\",\"task\":", id,
                                                ",\"message\":\"\"}");
    expect_record(line_at(text, 2usize), "{\"time\":\"", error_record.as_str(), false);
    /* Each record is one JSON object whose members read back as they were given. */
    std.string::string task_text = f"{id}";
    expect_json_member(line_at(text, 1usize), "message", "say \"hi\" \\ now\n");
    expect_json_member(line_at(text, 1usize), "controls", "\t\x01");
    expect_json_member(line_at(text, 1usize), "count", "-3");
    expect_json_member(line_at(text, 1usize), "task", task_text.as_str());
    expect_json_member(line_at(text, 1usize), "level", "warn");
    expect_json_member(line_at(text, 2usize), "message", "");
}

@test
async void rewrites_field_names() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_names.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer sink = std.log::writer::create(2usize);
    std.log::logger named = sink.logger(std.log::level::info, std.log::format::text);
    std.log::fields odd = std.log::fields::create();
    odd.text("time", "t");
    odd.text("level", "l");
    odd.text("task", "k");
    odd.text("message", "m");
    odd.text("bad name", "b");
    odd.text("", "e");
    odd.text("é", "u");
    odd.text("a=b", "c");
    odd.text("Time", "T");
    odd.text("x.y-z_1", "ok");
    odd.text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "long");
    odd.text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaé", "cut");
    odd.text("quo\"te", "q");
    odd.text("tab\there", "x");
    std.test::equal(odd.count(), 14usize);
    named.log(std.log::level::info, "names", &odd);
    drop named;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(written.value, 1u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 1usize);
    std.string::string expected = with_task(" level=info task=", id,
                                            " message=names _time=t _level=l _task=k _message=m"
                                            " bad_name=b _=e __=u a_b=c Time=T x.y-z_1=ok"
                                            " aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                            "aaaaaaaaaaaa=long"
                                            " aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                            "aaaaaaaaaaa_=cut quo_te=q tab_here=x");
    expect_record(line_at(text, 0usize), "time=", expected.as_str(), false);
}

@test
async void masks_sensitive_fields() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_masks.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer sink = std.log::writer::create(8usize);
    std.log::logger masked = sink.logger(std.log::level::info, std.log::format::text);
    masked.mask("session");
    masked.mask("odd name");
    masked.mask("time");
    std.log::logger other = sink.logger(std.log::level::info, std.log::format::text);
    std.log::fields extra = std.log::fields::create();
    extra.secret("key");
    extra.text("password", "p");
    extra.text("PassWord", "p");
    extra.text("SECRET", "s");
    extra.number("token", 42i64);
    extra.flag("authorization", true);
    extra.text("Authorization", "Bearer x");
    extra.text("tokens", "t");
    extra.text("api_token", "t");
    extra.text("Session", "s");
    extra.text("odd name", "o");
    extra.text("odd_name", "n");
    extra.text("time", "t");
    extra.text("user", "bob");
    masked.log(std.log::level::info, "masked", &extra);
    other.log(std.log::level::info, "other", &extra);
    /* A shared logger keeps the masked names; a name masked later is its own. */
    std.log::logger copy = masked.share();
    copy.mask("USER");
    copy.log(std.log::level::info, "copy", &extra);
    masked.log(std.log::level::info, "again", &extra);
    drop masked;
    drop other;
    drop copy;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(written.value, 4u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 4usize);
    std.string::string first = with_task(" level=info task=", id,
                                         " message=masked key=*** password=*** PassWord=***"
                                         " SECRET=*** token=*** authorization=***"
                                         " Authorization=*** tokens=t api_token=t Session=***"
                                         " odd_name=*** odd_name=*** _time=*** user=bob");
    expect_record(line_at(text, 0usize), "time=", first.as_str(), false);
    std.string::string second = with_task(" level=info task=", id,
                                          " message=other key=*** password=*** PassWord=***"
                                          " SECRET=*** token=*** authorization=***"
                                          " Authorization=*** tokens=t api_token=t Session=s"
                                          " odd_name=o odd_name=n _time=t user=bob");
    expect_record(line_at(text, 1usize), "time=", second.as_str(), false);
    std.string::string third = with_task(" level=info task=", id,
                                         " message=copy key=*** password=*** PassWord=***"
                                         " SECRET=*** token=*** authorization=***"
                                         " Authorization=*** tokens=t api_token=t Session=***"
                                         " odd_name=*** odd_name=*** _time=*** user=***");
    expect_record(line_at(text, 2usize), "time=", third.as_str(), false);
    std.string::string fourth = with_task(" level=info task=", id,
                                          " message=again key=*** password=*** PassWord=***"
                                          " SECRET=*** token=*** authorization=***"
                                          " Authorization=*** tokens=t api_token=t Session=***"
                                          " odd_name=*** odd_name=*** _time=*** user=bob");
    expect_record(line_at(text, 3usize), "time=", fourth.as_str(), false);
}

@test
async void reports_dropped_records_last() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_dropped.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer small = std.log::writer::create(2usize);
    std.log::logger busy = small.logger(std.log::level::trace, std.log::format::json);
    busy.info("one");
    busy.info("two");
    busy.info("three");
    busy.warn("four");
    busy.trace("five");
    std.test::equal(busy.dropped(), 3u64);
    std.log::logger copy = busy.share();
    copy.info("six");
    std.test::equal(busy.dropped(), 4u64);
    std.test::equal(copy.dropped(), 4u64);
    drop busy;
    drop copy;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move small, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    /* The writer counts the queued lines it wrote, not the last line. */
    std.test::equal(written.value, 2u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 3usize);
    std.string::string one = with_task("\",\"level\":\"info\",\"task\":", id,
                                       ",\"message\":\"one\"}");
    expect_record(line_at(text, 0usize), "{\"time\":\"", one.as_str(), false);
    std.string::string two = with_task("\",\"level\":\"info\",\"task\":", id,
                                       ",\"message\":\"two\"}");
    expect_record(line_at(text, 1usize), "{\"time\":\"", two.as_str(), false);
    std.test::equal_text(line_at(text, 2usize), "message=\"records dropped\" dropped=4");
    std.test::check(std.text::ends_with(text, "\n"), "the last line ends with a line feed");
}

@test
async void creates_and_appends_to_the_file() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_append.log");
    task_scope(1) removing { await remove_file(&path); }
    u64 id = std.async::task_id();
    std.log::writer first_sink = std.log::writer::create(4usize);
    std.log::logger first = first_sink.logger(std.log::level::info, std.log::format::text);
    first.info("first");
    drop first;
    Count created = {.value = 0u64};
    task_scope(1) creating {
        created.value = await std.log::writer::to_file(move first_sink, &path);
    }
    std.log::writer second_sink = std.log::writer::create(4usize);
    std.log::logger second = second_sink.logger(std.log::level::info, std.log::format::text);
    second.info("second");
    second.warn("third");
    drop second;
    Count appended = {.value = 0u64};
    task_scope(1) appending {
        appended.value = await std.log::writer::to_file(move second_sink, &path);
    }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(created.value, 1u64);
    std.test::equal(appended.value, 2u64);
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 3usize);
    std.string::string one = with_task(" level=info task=", id, " message=first");
    expect_record(line_at(text, 0usize), "time=", one.as_str(), false);
    std.string::string two = with_task(" level=info task=", id, " message=second");
    expect_record(line_at(text, 1usize), "time=", two.as_str(), false);
    std.string::string three = with_task(" level=warn task=", id, " message=third");
    expect_record(line_at(text, 2usize), "time=", three.as_str(), false);
}

/* Logs with a logger shared into its own task and returns the identifier of that task. */
protected async u64 log_elsewhere(std.log::logger shared) throws std.error::fault {
    std.log::fields extra = std.log::fields::create();
    extra.text("session", "abc");
    extra.text("user", "bob");
    shared.log(std.log::level::info, "from a task", &extra);
    shared.debug("hidden");
    return std.async::task_id();
}

@test
async void shares_a_logger_with_another_task() throws std.test::failure, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("rtest_log_share.log");
    task_scope(1) clearing { await truncate(&path); }
    u64 id = std.async::task_id();
    std.log::writer sink = std.log::writer::create(8usize);
    std.log::logger own_log = sink.logger(std.log::level::info, std.log::format::text);
    own_log.mask("session");
    std.log::logger copy = own_log.share();
    own_log.info("before");
    Count other = {.value = 0u64};
    other.value = await log_elsewhere(move copy);
    own_log.info("after");
    drop own_log;
    Count written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    std.string::string content = await read_text(std.fs::path_clone(&path));
    std.test::equal(written.value, 3u64);
    std.test::check(other.value != id && other.value != 0u64,
                    "the other task has its own identifier");
    str text = content.as_str();
    std.test::equal(std.text::count_byte(text, 10u8), 3usize);
    std.string::string before = with_task(" level=info task=", id, " message=before");
    expect_record(line_at(text, 0usize), "time=", before.as_str(), false);
    std.string::string elsewhere = with_task(" level=info task=", other.value,
                                             " message=\"from a task\" session=*** user=bob");
    expect_record(line_at(text, 1usize), "time=", elsewhere.as_str(), false);
    std.string::string after = with_task(" level=info task=", id, " message=after");
    expect_record(line_at(text, 2usize), "time=", after.as_str(), false);
}

@test
async void writes_to_stderr_until_the_loggers_end() throws std.test::failure, std.error::fault {
    /* Records below the threshold are not queued, so the writer has nothing to write. */
    std.log::writer idle = std.log::writer::create(4usize);
    std.log::logger unused = idle.logger(std.log::level::warn, std.log::format::text);
    unused.info("below the threshold");
    std.test::equal(unused.dropped(), 0u64);
    drop unused;
    u64 written = await std.log::writer::to_stderr(move idle);
    std.test::equal(written, 0u64);
    /* A writer without loggers ends at once. */
    std.log::writer alone = std.log::writer::create(0usize);
    u64 none = await std.log::writer::to_stderr(move alone);
    std.test::equal(none, 0u64);
}

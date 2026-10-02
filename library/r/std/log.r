module std.log;
import std.time;

/* R-SLIB-LOG-0001: the severity of a record, lowest first. */
enum level { trace, debug, info, warn, error };

/* R-SLIB-LOG-0001: a record as one line of `name=value` pairs or as one JSON object. */
enum format { text, json };

/* The kind of a field value; a secret keeps no value and is written masked. */
protected enum field_kind { string, number, flag, secret };

/* One named field of a record. */
protected struct field {
    std.string::string name;
    std.string::string value;
    field_kind kind;
};

/* R-SLIB-LOG-0001: the fields of one record, in the order they were added. */
struct fields {
    protected array<field> entries;
};

/* The state that the loggers and the writer of one queue share: the records dropped because
   the queue was full. */
protected struct shared {
    au64 dropped;
};

/* R-SLIB-LOG-0002: a logger formats each record on the calling task and hands the line to the
   bounded queue of its writer without waiting. */
struct logger {
    protected std.sync::sync_sender<std.string::string> lines;
    protected arc shared state;
    protected level threshold;
    protected format style;
    protected array<std.string::string> masked;
};

/* R-SLIB-LOG-0003: the single consumer of the queue; it writes the lines to one destination
   until every logger of the queue is gone. */
struct writer {
    protected std.sync::receiver<std.string::string> lines;
    protected o<std.sync::sync_sender<std.string::string>> source;
    protected arc shared state;
};

@generic<T>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected bool same(const u8[] left, const u8[] right) {
    return std.bytes::equal(left, right);
}

/* The bytes of a text cut at ASCII bytes, which are valid UTF-8. */
protected void append_bytes(std.string::string* line, const u8[] bytes)
    throws std.alloc::alloc_error {
    try {
        str text = std.utf8::validate(bytes);
        line->append(text);
    } catch (core::utf8_error failure) {
        failure as void;
    }
}

protected bool name_character(u8 byte) {
    return (byte >= 97u8 && byte <= 122u8) || (byte >= 65u8 && byte <= 90u8) ||
           (byte >= 48u8 && byte <= 57u8) || byte == 95u8 || byte == 46u8 || byte == 45u8;
}

/* R-SLIB-LOG-0001: the name a field is written with. Every byte other than an ASCII letter,
   digit, `_`, `.` or `-` becomes `_`, the name keeps at most 64 bytes, an empty name is `_`, and
   a name that every record carries gets a leading `_`, so no name can break a record. */
protected std.string::string field_name(str name) throws std.alloc::alloc_error {
    const u8[] bytes = name;
    std.string::string written = std.string::create();
    if (same(name, "time") == true || same(name, "level") == true ||
        same(name, "task") == true || same(name, "message") == true) {
        written.append("_");
    }
    usize start = 0usize;
    for (usize index = 0usize; index < len(bytes) && index < 64usize; index += 1usize) {
        if (name_character(bytes[index]) == false) {
            append_bytes(&written, bytes[start..index]);
            written.append("_");
            start = index + 1usize;
        }
    }
    usize end = len(bytes);
    if (end > 64usize) { end = 64usize; }
    if (start < end) { append_bytes(&written, bytes[start..end]); }
    if (std.string::len(&written) == 0usize) { written.append("_"); }
    return move written;
}

/* An empty field list. */
fields fields::create() {
    return fields {.entries = std.array::create()};
}

protected void fields::push(fields* this, str name, std.string::string value, field_kind kind)
    throws std.alloc::alloc_error {
    append(&this->entries, field {.name = field_name(name), .value = move value, .kind = kind});
}

/* R-SLIB-LOG-0001: a text field. */
void fields::text(fields* this, str name, str value) throws std.alloc::alloc_error {
    this->push(name, std.string::from_str(value), field_kind::string);
}

/* R-SLIB-LOG-0001: a number field, written without quotes. */
void fields::number(fields* this, str name, i64 value) throws std.alloc::alloc_error {
    this->push(name, f"{value}", field_kind::number);
}

/* R-SLIB-LOG-0001: a Boolean field, written as true or false. */
void fields::flag(fields* this, str name, bool value) throws std.alloc::alloc_error {
    this->push(name, f"{value}", field_kind::flag);
}

/* R-SLIB-LOG-0001: a field whose value is never kept and is always written as `***`. */
void fields::secret(fields* this, str name) throws std.alloc::alloc_error {
    this->push(name, std.string::create(), field_kind::secret);
}

/* The number of fields. */
usize fields::count(const fields* this) {
    return len(this->entries);
}

/* R-SLIB-LOG-0003: a writer and the queue of `capacity` lines behind it, at least one. */
writer writer::create(usize capacity) throws std.alloc::alloc_error {
    usize held = capacity;
    if (held == 0usize) { held = 1usize; }
    std.sync::sync_channel<std.string::string> channel =
        std.sync::sync_channel::<std.string::string>(held);
    std.sync::sync_sender<std.string::string> source = std.sync::sync_sender(&channel);
    std.sync::receiver<std.string::string> lines = std.sync::sync_receiver(move channel);
    arc shared state = new arc shared {.dropped = 0u64};
    return writer {.lines = move lines, .source = o::some(move source), .state = move state};
}

protected array<std.string::string> default_masked() throws std.alloc::alloc_error {
    array<std.string::string> names = std.array::create();
    append(&names, std.string::from_str("password"));
    append(&names, std.string::from_str("secret"));
    append(&names, std.string::from_str("token"));
    append(&names, std.string::from_str("authorization"));
    return move names;
}

/* R-SLIB-LOG-0002: a logger that queues the records of `threshold` and above. The operations
   that write take the writer, so its sender is always present here; the other branch only
   keeps the logger well defined. */
logger writer::logger(const writer* this, level threshold, format style)
    throws std.alloc::alloc_error {
    switch (this->source) {
    case variant o::some(source):
        std.sync::sync_sender<std.string::string> lines = std.sync::clone_sync_sender(source);
        arc shared state = std.arc::clone(&this->state);
        return logger {.lines = move lines, .state = move state, .threshold = threshold,
                       .style = style, .masked = default_masked()};
    case variant o::none:
        std.sync::sync_channel<std.string::string> closed =
            std.sync::sync_channel::<std.string::string>(1usize);
        std.sync::sync_sender<std.string::string> lines = std.sync::sync_sender(&closed);
        drop closed;
        arc shared state = std.arc::clone(&this->state);
        return logger {.lines = move lines, .state = move state, .threshold = threshold,
                       .style = style, .masked = default_masked()};
    }
}

/* R-SLIB-LOG-0002: another logger of the same queue, threshold, format and masked names, for
   another task. */
logger logger::share(const logger* this) throws std.alloc::alloc_error {
    array<std.string::string> masked = std.array::create();
    for (const std.string::string* name in &this->masked) {
        append(&masked, std.string::from_str(name->as_str()));
    }
    return logger {.lines = std.sync::clone_sync_sender(&this->lines),
                   .state = std.arc::clone(&this->state), .threshold = this->threshold,
                   .style = this->style, .masked = move masked};
}

/* R-SLIB-LOG-0002: fields written with this name, compared ignoring ASCII case, are masked. */
void logger::mask(logger* this, str name) throws std.alloc::alloc_error {
    append(&this->masked, field_name(name));
}

/* R-SLIB-LOG-0002: whether a record of `value` would be queued. */
bool logger::enabled(const logger* this, level value) {
    return core::enum_ordinal(value) >= core::enum_ordinal(this->threshold);
}

/* The records dropped because the queue was full, by every logger of the queue. */
u64 logger::dropped(const logger* this) {
    return core::atomic_load(&this->state->dropped, core::memory_order::relaxed);
}

protected bool ascii_equal_ignoring_case(const u8[] a, const u8[] b) {
    if (len(a) != len(b)) { return false; }
    for (usize index = 0usize; index < len(a); index += 1usize) {
        u8 x = a[index];
        u8 y = b[index];
        if (x >= 65u8 && x <= 90u8) { x += 32u8; }
        if (y >= 65u8 && y <= 90u8) { y += 32u8; }
        if (x != y) { return false; }
    }
    return true;
}

protected bool logger::masks(const logger* this, str name) {
    for (const std.string::string* masked in &this->masked) {
        if (ascii_equal_ignoring_case(masked->as_str(), name) == true) { return true; }
    }
    return false;
}

/* A text value in quotes when it is empty or holds a space, quote, `=`, backslash or control
   character; in JSON always in quotes. */
protected void append_quoted(std.string::string* line, str value, bool always) throws std.alloc::alloc_error {
    const u8[] bytes = value;
    bool plain = always == false && len(bytes) != 0usize;
    for (usize index = 0usize; index < len(bytes) && plain == true; index += 1usize) {
        u8 byte = bytes[index];
        if (byte <= 32u8 || byte == 34u8 || byte == 61u8 || byte == 92u8 || byte == 127u8) {
            plain = false;
        }
    }
    if (plain == true) {
        line->append(value);
        return;
    }
    line->append("\"");
    usize start = 0usize;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 byte = bytes[index];
        if (byte == 34u8 || byte == 92u8 || byte < 32u8 || byte == 127u8) {
            append_bytes(line, bytes[start..index]);
            if (byte == 34u8) { line->append("\\\""); }
            else {
                if (byte == 92u8) { line->append("\\\\"); }
                else {
                    if (byte == 10u8) { line->append("\\n"); }
                    else {
                        if (byte == 9u8) { line->append("\\t"); }
                        else {
                            if (byte == 13u8) { line->append("\\r"); }
                            else {
                                u32 code = byte as u32;
                                std.string::string escape = f"\\u00{code:02x}";
                                line->append(escape.as_str());
                            }
                        }
                    }
                }
            }
            start = index + 1usize;
        }
    }
    append_bytes(line, bytes[start..len(bytes)]);
    line->append("\"");
}

protected void append_pair(std.string::string* line, format style, str name, str value,
                           bool quoted) throws std.alloc::alloc_error {
    if (style == format::json) {
        line->append(",\"");
        line->append(name);
        line->append("\":");
        if (quoted == true) { append_quoted(line, value, true); }
        else { line->append(value); }
        return;
    }
    line->append(" ");
    line->append(name);
    line->append("=");
    append_quoted(line, value, false);
}

/* R-SLIB-LOG-0002: the line of one record: time, level, task and message, then the fields. */
protected std.string::string logger::render(const logger* this, level value, str message,
                                             const fields* extra) throws std.error::fault {
    std.time::system_time now = std.time::system_now();
    std.string::string time = std.time::format_rfc3339(now, 3u32);
    str name = core::enum_name(value);
    u64 running = std.async::task_id();
    std.string::string line = std.string::create();
    if (this->style == format::json) {
        line.append("{\"time\":\"");
        line.append(time.as_str());
        line.append("\",\"level\":\"");
        line.append(name);
        std.string::string task_text = f"\",\"task\":{running},\"message\":";
        line.append(task_text.as_str());
        append_quoted(&line, message, true);
    } else {
        std.string::string head = f"time={time} level={name} task={running} message=";
        line.append(head.as_str());
        append_quoted(&line, message, false);
    }
    for (const field* entry in &extra->entries) {
        str written = entry->name.as_str();
        if (entry->kind == field_kind::secret || this->masks(written) == true) {
            append_pair(&line, this->style, written, "***", true);
        } else {
            append_pair(&line, this->style, written, entry->value.as_str(),
                        entry->kind == field_kind::string);
        }
    }
    if (this->style == format::json) { line.append("}"); }
    line.append("\n");
    return move line;
}

/* R-SLIB-LOG-0002: queues one record of `value` with its fields when the level reaches the
   threshold; a record that finds the queue full or its writer gone is dropped and counted. It
   never waits. */
void logger::log(const logger* this, level value, str message, const fields* extra)
    throws std.error::fault {
    if (this->enabled(value) == false) { return; }
    std.string::string line = this->render(value, message, extra);
    std.sync::try_send_result<std.string::string> sent = std.sync::try_send(&this->lines, move line);
    switch (move sent) {
    case variant std.sync::try_send_result::sent: return;
    case variant std.sync::try_send_result::full(move rejected):
        drop rejected;
    case variant std.sync::try_send_result::disconnected(move rejected):
        drop rejected;
    }
    core::atomic_fetch_add(&this->state->dropped, 1u64, core::memory_order::relaxed) as void;
}

protected void logger::plain(const logger* this, level value, str message)
    throws std.error::fault {
    fields none = fields::create();
    this->log(value, message, &none);
}

/* R-SLIB-LOG-0002: one record without fields at each level. */
void logger::trace(const logger* this, str message) throws std.error::fault {
    this->plain(level::trace, message);
}

void logger::debug(const logger* this, str message) throws std.error::fault {
    this->plain(level::debug, message);
}

void logger::info(const logger* this, str message) throws std.error::fault {
    this->plain(level::info, message);
}

void logger::warn(const logger* this, str message) throws std.error::fault {
    this->plain(level::warn, message);
}

void logger::error(const logger* this, str message) throws std.error::fault {
    this->plain(level::error, message);
}

/* The final line of a writer that dropped records. */
protected std.string::string drop_line(u64 dropped) throws std.alloc::alloc_error {
    return f"message=\"records dropped\" dropped={dropped}\n";
}

/* R-SLIB-LOG-0003: writes every queued line to standard error until every logger of the queue
   is gone, then a last line with the number of dropped records when there were any; returns the
   number of lines written. */
async u64 writer::to_stderr(writer this) throws std.error::fault {
    o<std.sync::sync_sender<std.string::string>> source = core::take(&this.source);
    drop source;
    u64 written = 0u64;
    while (true) {
        o<std.string::string> next = await std.sync::receive(&this.lines);
        switch (move next) {
        case variant o::some(move line):
            std.io::output target = std.io::stderr();
            task_scope(1) io { await std.io::write_all_from(&target, line.as_bytes()); }
            written += 1u64;
        case variant o::none:
            u64 dropped = core::atomic_load(&this.state->dropped, core::memory_order::relaxed);
            if (dropped != 0u64) {
                std.io::output target = std.io::stderr();
                std.string::string last = drop_line(dropped);
                task_scope(1) io { await std.io::write_all_from(&target, last.as_bytes()); }
            }
            return written;
        }
    }
}

/* R-SLIB-LOG-0003: the same, appending to the file at `path`, which is created when absent. */
@scoped
async u64 writer::to_file(writer this, const std.fs::path* path) throws std.error::fault {
    o<std.sync::sync_sender<std.string::string>> source = core::take(&this.source);
    drop source;
    std.fs::open_file_options options = std.fs::open_file_options {
        .access = std.fs::access::write, .create = std.fs::create_mode::open_or_create,
        .truncate = false, .append = true, .follow_final_symlink = true};
    std.fs::path file = std.fs::path_clone(path);
    std.fs::file target = await std.fs::open_file(&file, options);
    u64 written = 0u64;
    while (true) {
        o<std.string::string> next = await std.sync::receive(&this.lines);
        switch (move next) {
        case variant o::some(move line):
            task_scope(1) io { await std.fs::write_all_from(&target, line.as_bytes()); }
            written += 1u64;
        case variant o::none:
            u64 dropped = core::atomic_load(&this.state->dropped, core::memory_order::relaxed);
            if (dropped != 0u64) {
                std.string::string last = drop_line(dropped);
                task_scope(1) io { await std.fs::write_all_from(&target, last.as_bytes()); }
            }
            await std.fs::close_file(move target);
            return written;
        }
    }
}

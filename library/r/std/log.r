module std.log;
import std.time;

/* R-SLIB-LOG-0001: the severity of a record, lowest first. */
enum level { trace, debug, info, warn, error };

/* R-SLIB-LOG-0001: a record as one line of `name=value` pairs or as one JSON object. */
enum format { text, json };

/* R-SLIB-LOG-0004: the order of the members of a record: the time, level, task and message
   before the fields, or the level first, then the fields, the time, the task and the message. */
enum order { time_first, level_first };

/* R-SLIB-LOG-0004: the members every record of a logger carries: the names of the time, the
   level and the message, the name of the task or none, the fractional digits of the time and
   whether its trailing zeros are left out, whether an empty message is left out, and their
   order. */
struct layout {
    std.string::string time_name;
    std.string::string level_name;
    std.string::string message_name;
    o<std.string::string> task_name;
    u32 time_digits;
    bool trim_time;
    bool omit_empty_message;
    order sequence;
};

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
    protected layout shape;
    protected array<field> bound;
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
   digit, `_`, `.` or `-` becomes `_`, the name keeps at most 64 bytes and an empty name is `_`,
   so no name can break a record; a name that every record of the logger carries gets a leading
   `_` when it is written (R-SLIB-LOG-0004). */
protected std.string::string field_name(str name) throws std.alloc::alloc_error {
    const u8[] bytes = name;
    std.string::string written = std.string::create();
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

/* The fields of a list, copied. */
protected array<field> copy_entries(const array<field>* entries) throws std.alloc::alloc_error {
    array<field> copied = std.array::create();
    for (const field* entry in entries) {
        append(&copied, field {.name = std.string::from_str(entry->name.as_str()),
                               .value = std.string::from_str(entry->value.as_str()), .kind = entry->kind});
    }
    return move copied;
}

/* R-SLIB-LOG-0004: the layout of R-SLIB-LOG-0002: `time` with three fractional digits, `level`,
   `task` and `message`, in that order before the fields. */
layout layout::standard() throws std.alloc::alloc_error {
    return layout {.time_name = std.string::from_str("time"), .level_name = std.string::from_str("level"),
                   .message_name = std.string::from_str("message"), .task_name = o::some(std.string::from_str("task")),
                   .time_digits = 3u32, .trim_time = false, .omit_empty_message = false,
                   .sequence = order::time_first};
}

protected layout copy_layout(const layout* source) throws std.alloc::alloc_error {
    o<std.string::string> task_field = o::none;
    switch (source->task_name) {
    case variant o::some(name): task_field = o::some(std.string::from_str(name->as_str()));
    case variant o::none: break;
    }
    return layout {.time_name = std.string::from_str(source->time_name.as_str()),
                   .level_name = std.string::from_str(source->level_name.as_str()),
                   .message_name = std.string::from_str(source->message_name.as_str()), .task_name = move task_field,
                   .time_digits = source->time_digits, .trim_time = source->trim_time,
                   .omit_empty_message = source->omit_empty_message, .sequence = source->sequence};
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
                       .style = style, .masked = default_masked(), .shape = layout::standard(),
                       .bound = std.array::create()};
    case variant o::none:
        std.sync::sync_channel<std.string::string> closed =
            std.sync::sync_channel::<std.string::string>(1usize);
        std.sync::sync_sender<std.string::string> lines = std.sync::sync_sender(&closed);
        drop closed;
        arc shared state = std.arc::clone(&this->state);
        return logger {.lines = move lines, .state = move state, .threshold = threshold,
                       .style = style, .masked = default_masked(), .shape = layout::standard(),
                       .bound = std.array::create()};
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
                   .style = this->style, .masked = move masked, .shape = copy_layout(&this->shape),
                   .bound = copy_entries(&this->bound)};
}

/* R-SLIB-LOG-0004: the logger writes its records with the layout; its names are rewritten as
   field names, and more than nine fractional digits are nine. */
void logger::set_layout(logger* this, layout value) throws std.alloc::alloc_error {
    o<std.string::string> task_field = o::none;
    switch (value.task_name) {
    case variant o::some(name): task_field = o::some(field_name(name->as_str()));
    case variant o::none: break;
    }
    u32 digits = value.time_digits;
    if (digits > 9u32) { digits = 9u32; }
    layout written = layout {.time_name = field_name(value.time_name.as_str()),
                             .level_name = field_name(value.level_name.as_str()),
                             .message_name = field_name(value.message_name.as_str()), .task_name = move task_field,
                             .time_digits = digits, .trim_time = value.trim_time,
                             .omit_empty_message = value.omit_empty_message, .sequence = value.sequence};
    drop value;
    layout old = core::replace(&this->shape, move written);
    drop old;
}

/* R-SLIB-LOG-0004: another logger of the same queue, as share gives, whose records carry the
   bound fields of this logger and then those of `extra`, before the fields of each record; this
   logger is unchanged. */
logger logger::with(const logger* this, const fields* extra) throws std.alloc::alloc_error {
    logger child = this->share();
    for (const field* entry in &extra->entries) {
        append(&child.bound, field {.name = std.string::from_str(entry->name.as_str()),
                                    .value = std.string::from_str(entry->value.as_str()), .kind = entry->kind});
    }
    return move child;
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

/* A control byte other than \n, \t and \r: \b, \f or \u00XX with lowercase digits. */
protected void append_control(std.string::string* line, u8 byte) throws std.alloc::alloc_error {
    if (byte == 8u8) {
        line->append("\\b");
        return;
    }
    if (byte == 12u8) {
        line->append("\\f");
        return;
    }
    u32 code = byte as u32;
    std.string::string escape = f"\\u00{code:02x}";
    line->append(escape.as_str());
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
                                append_control(line, byte);
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

/* Whether a field name is the name of a member that every record of the logger carries. */
protected bool logger::carries(const logger* this, str name) {
    if (same(name, this->shape.time_name.as_str()) == true || same(name, this->shape.level_name.as_str()) == true ||
        same(name, this->shape.message_name.as_str()) == true) {
        return true;
    }
    switch (this->shape.task_name) {
    case variant o::some(task_field): return same(name, task_field->as_str());
    case variant o::none: break;
    }
    return false;
}

/* The fields of a list as members of the line; a name that the record carries gets a leading
   `_`, and a secret or masked field is `***`. */
protected void logger::put_fields(const logger* this, std.string::string* line, const array<field>* entries)
    throws std.alloc::alloc_error {
    for (const field* entry in entries) {
        std.string::string written = std.string::create();
        if (this->carries(entry->name.as_str()) == true) { written.append("_"); }
        written.append(entry->name.as_str());
        if (entry->kind == field_kind::secret || this->masks(entry->name.as_str()) == true ||
            this->masks(written.as_str()) == true) {
            append_pair(line, this->style, written.as_str(), "***", true);
        } else {
            append_pair(line, this->style, written.as_str(), entry->value.as_str(), entry->kind == field_kind::string);
        }
    }
}

/* The time of a record in RFC 3339 with the digits of the layout, without the trailing zeros of
   the fraction, and its dot, when the layout leaves them out. */
protected std.string::string logger::stamp(const logger* this, std.time::system_time now) throws std.error::fault {
    std.string::string full = std.time::format_rfc3339(now, this->shape.time_digits);
    if (this->shape.trim_time == false || this->shape.time_digits == 0u32) { return move full; }
    const u8[] bytes = full.as_bytes();
    usize end = len(bytes) - 1usize;
    while (bytes[end - 1usize] == 48u8) { end -= 1usize; }
    if (bytes[end - 1usize] == 46u8) { end -= 1usize; }
    std.string::string trimmed = std.string::with_capacity(end + 1usize);
    append_bytes(&trimmed, bytes[0usize..end]);
    trimmed.append("Z");
    return move trimmed;
}

/* One member that every record carries; the first one of a JSON line opens the object. */
protected void logger::put_member(const logger* this, std.string::string* line, bool first, str name, str value,
                                  bool quoted) throws std.alloc::alloc_error {
    if (first == false) {
        append_pair(line, this->style, name, value, quoted);
        return;
    }
    if (this->style == format::json) {
        line->append("{\"");
        line->append(name);
        line->append("\":");
        if (quoted == true) { append_quoted(line, value, true); }
        else { line->append(value); }
        return;
    }
    line->append(name);
    line->append("=");
    append_quoted(line, value, false);
}

/* The message member, left out when it is empty and the layout says so. */
protected void logger::put_message(const logger* this, std.string::string* line, str message)
    throws std.alloc::alloc_error {
    const u8[] bytes = message;
    if (len(bytes) == 0usize && this->shape.omit_empty_message == true) { return; }
    this->put_member(line, false, this->shape.message_name.as_str(), message, true);
}

/* R-SLIB-LOG-0002, R-SLIB-LOG-0004: the line of one record in the order of the layout. */
protected std.string::string logger::render(const logger* this, level value, str message,
                                             const fields* extra) throws std.error::fault {
    std.string::string time = this->stamp(std.time::system_now());
    str name = core::enum_name(value);
    std.string::string line = std.string::create();
    if (this->shape.sequence == order::time_first) {
        this->put_member(&line, true, this->shape.time_name.as_str(), time.as_str(), true);
        this->put_member(&line, false, this->shape.level_name.as_str(), name, true);
        switch (this->shape.task_name) {
        case variant o::some(task_field):
            u64 running = std.async::task_id();
            std.string::string task_text = f"{running}";
            this->put_member(&line, false, task_field->as_str(), task_text.as_str(), false);
        case variant o::none: break;
        }
        this->put_message(&line, message);
        this->put_fields(&line, &this->bound);
        this->put_fields(&line, &extra->entries);
    } else {
        this->put_member(&line, true, this->shape.level_name.as_str(), name, true);
        this->put_fields(&line, &this->bound);
        this->put_fields(&line, &extra->entries);
        this->put_member(&line, false, this->shape.time_name.as_str(), time.as_str(), true);
        switch (this->shape.task_name) {
        case variant o::some(task_field):
            u64 running = std.async::task_id();
            std.string::string task_text = f"{running}";
            this->put_member(&line, false, task_field->as_str(), task_text.as_str(), false);
        case variant o::none: break;
        }
        this->put_message(&line, message);
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

/* ---- Unobserved panics (R-SLIB-LOG-0005) ---- */

@link(name = "std.log.native", kind = "static")
@header("r_std_log_native.h")
extern "C" {
    @safety("LOG-LISTEN", "takes no pointer; the queue is owned by the provider")
    c_uint64 r_std_log_native_listen(c_size capacity);
    @safety("LOG-PANICS", "category and text address 32 and 256 writable bytes, the other outputs one each")
    c_int32 r_std_log_native_take(c_uint64 generation,
                                  raw c_uint8* category,
                                  raw c_size* category_length,
                                  raw c_uint8* text,
                                  raw c_size* text_length,
                                  raw c_uint32* source_module,
                                  raw c_uint32* start,
                                  raw c_uint32* end);
    @safety("LOG-DROPPED", "takes no pointer")
    c_uint64 r_std_log_native_dropped(c_uint64 generation);
    @safety("LOG-STOP", "takes no pointer; a generation that is not the last one changes nothing")
    void r_std_log_native_stop(c_uint64 generation);
}

/* R-SLIB-LOG-0005: the report of a panic that nothing observed: its category, its text and its
   place as the runtime names them. */
struct panic_record {
    std.string::string category;
    std.string::string text;
    std.string::string place;
};

/* R-SLIB-LOG-0005: the reports of the panics that nothing observes, queued for the program
   instead of written as lines, from the listener made last. */
struct panic_reports {
    protected u64 generation;
};

drop(panic_reports* self) {
    unsafe { r_std_log_native_stop(self->generation as c_uint64); }
}

/* R-SLIB-LOG-0005: a listener with a queue of capacity reports, at least one. */
protected u64 native_listen(usize capacity) {
    unsafe { return r_std_log_native_listen(capacity as c_size) as u64; }
}

panic_reports panic_reports::listen(usize capacity) throws std.alloc::alloc_error {
    u64 generation = native_listen(capacity);
    throw (generation == 0u64) std.alloc::alloc_error::out_of_memory;
    return panic_reports {.generation = generation};
}

protected raw c_uint8* bytes_out(u8[] data) {
    unsafe {
        raw u8* first = &data[0usize] as raw u8*;
        raw void* erased = first as raw void*;
        return erased as raw c_uint8*;
    }
}

/* The text of bytes that the runtime wrote, cut at the last whole scalar. */
protected std.string::string text_of(const u8[] bytes) throws std.alloc::alloc_error {
    usize end = len(bytes);
    while (end > 0usize) {
        try {
            return std.string::from_str(core::validate_utf8(bytes[0usize..end]));
        } catch (core::utf8_error rejected) {
            rejected as void;
        }
        end -= 1usize;
    }
    return std.string::create();
}

/* R-SLIB-LOG-0005: the oldest queued report, or none. */
o<panic_record> panic_reports::take(const panic_reports* this) throws std.alloc::alloc_error {
    u8[32] category = {};
    u8[256] text = {};
    c_size category_length = 0usize as c_size;
    c_size text_length = 0usize as c_size;
    c_uint32 source_module = 0u32 as c_uint32;
    c_uint32 start = 0u32 as c_uint32;
    c_uint32 end = 0u32 as c_uint32;
    unsafe {
        c_int32 status = r_std_log_native_take(this->generation as c_uint64, bytes_out(category[..]),
                                               &category_length as raw c_size*, bytes_out(text[..]),
                                               &text_length as raw c_size*, &source_module as raw c_uint32*,
                                               &start as raw c_uint32*, &end as raw c_uint32*);
        if (status as i32 == 0i32) { return o::none; }
    }
    u32 module_index = source_module as u32;
    u32 first = start as u32;
    u32 last = end as u32;
    usize named = category_length as usize;
    usize written = text_length as usize;
    return o::some(panic_record {.category = text_of(category[0usize..named]), .text = text_of(text[0usize..written]),
                                 .place = f"module {module_index} bytes [{first},{last})"});
}

/* R-SLIB-LOG-0005: the next report; until one is queued it looks again after a pause that starts
   at one millisecond and doubles up to 128 milliseconds. */
@scoped
async panic_record panic_reports::next(const panic_reports* this) throws std.error::fault {
    u32 pause = 1000000u32;
    while (true) {
        o<panic_record> taken = this->take();
        switch (move taken) {
        case variant o::some(move record): return move record;
        case variant o::none: break;
        }
        await std.time::sleep_for(std.time::duration_from_parts(0i64, pause));
        if (pause < 128000000u32) { pause *= 2u32; }
    }
}

/* R-SLIB-LOG-0005: the reports that found the queue full and were written as lines. */
u64 panic_reports::dropped(const panic_reports* this) {
    unsafe { return r_std_log_native_dropped(this->generation as c_uint64) as u64; }
}

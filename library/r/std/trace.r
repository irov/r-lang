module std.trace;

/* R-SLIB-TRACE-0001: spans of work, each with a name, the task that started it, its start and
   duration on the monotonic clock, its parent and its attributes. A tracer keeps the finished
   spans of a program up to its capacity, dropping the oldest and counting them; it is shared
   by its handles from any task or thread. */

/* R-SLIB-TRACE-0001: one attribute of a span. */
struct attribute { std.string::string key; std.string::string value; };

/* R-SLIB-TRACE-0003: a finished span: start is measured from the creation of its tracer. */
struct record {
    u64 id;
    o<u64> parent;
    u64 task_id;
    std.string::string name;
    std.time::duration start;
    std.time::duration duration;
    array<std.trace::attribute> attributes;
};

protected struct tracer_core {
    std.time::instant epoch;
    atomic u64 next_id;
    atomic u64 dropped;
    usize capacity;
    std.sync::mutex<array<std.trace::record>> finished;
};

/* R-SLIB-TRACE-0002: the collector of finished spans. */
struct tracer { protected arc tracer_core core; };

/* R-SLIB-TRACE-0002: a span in progress; finish records it in its tracer. */
struct span {
    protected arc tracer_core core;
    protected u64 serial;
    protected o<u64> parent;
    protected u64 task_id;
    protected std.string::string name;
    protected std.time::instant started;
    protected array<std.trace::attribute> attributes;
};

tracer tracer::create(usize capacity) throws std.time::time_error, std.alloc::alloc_error {
    array<std.trace::record> none = [];
    return tracer {.core = new arc tracer_core {
        .epoch = std.time::monotonic_now(), .next_id = 0u64, .dropped = 0u64, .capacity = capacity,
        .finished = std.sync::mutex_new(move none),
    }};
}

tracer tracer::share(const tracer* this) {
    return tracer {.core = std.arc::clone(&this->core)};
}

protected span begin(const (arc tracer_core)* core, str name, o<u64> parent)
    throws std.time::time_error, std.alloc::alloc_error {
    u64 serial = core::atomic_fetch_add(&(*core)->next_id, 1u64, core::memory_order::relaxed) + 1u64;
    array<std.trace::attribute> none = [];
    return span {.core = std.arc::clone(core), .serial = serial, .parent = parent, .task_id = std.async::task_id(),
                 .name = std.string::from_str(name), .started = std.time::monotonic_now(),
                 .attributes = move none};
}

/* R-SLIB-TRACE-0002: a span without a parent, started now by the current task. */
span tracer::start(const tracer* this, str name) throws std.time::time_error, std.alloc::alloc_error {
    return begin(&this->core, name, o::none);
}

/* R-SLIB-TRACE-0002: a span whose parent is this one. */
span span::child(const span* this, str name) throws std.time::time_error, std.alloc::alloc_error {
    return begin(&this->core, name, o::some(this->serial));
}

u64 span::id(const span* this) {
    return this->serial;
}

void span::attribute(span* this, str key, str value) throws std.alloc::alloc_error {
    std.trace::attribute added = std.trace::attribute {.key = std.string::from_str(key),
                                                       .value = std.string::from_str(value)};
    try {
        this->attributes.push(move added);
    } catch (std.array::push_error<std.trace::attribute> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected array<std.trace::attribute> copy_attributes(const array<std.trace::attribute>* source)
    throws std.alloc::alloc_error {
    array<std.trace::attribute> copied = [];
    const std.trace::attribute[] listed = std.array::as_slice(source);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        std.trace::attribute item = std.trace::attribute {.key = std.string::from_str(listed[index].key),
                                                          .value = std.string::from_str(listed[index].value)};
        try {
            copied.push(move item);
        } catch (std.array::push_error<std.trace::attribute> rejected) {
            drop rejected;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    return move copied;
}

protected std.trace::record copy_record(const std.trace::record* source) throws std.alloc::alloc_error {
    return std.trace::record {.id = source->id, .parent = source->parent, .task_id = source->task_id,
                              .name = std.string::from_str(source->name), .start = source->start,
                              .duration = source->duration, .attributes = copy_attributes(&source->attributes)};
}

/* Keeps a record, dropping the oldest at capacity; returns whether one was dropped. */
protected bool keep(array<std.trace::record>* finished, std.trace::record item, usize capacity)
    throws std.alloc::alloc_error {
    if (capacity == 0usize) {
        drop item;
        return true;
    }
    bool dropped = false;
    if (len(*finished) >= capacity) {
        o<std.trace::record> oldest = finished->remove(0usize);
        drop oldest;
        dropped = true;
    }
    try {
        finished->push(move item);
    } catch (std.array::push_error<std.trace::record> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
    return dropped;
}

/* R-SLIB-TRACE-0002: ends the span now, keeps its record in the tracer and returns it. */
std.trace::record span::finish(span this) throws std.time::time_error, std.alloc::alloc_error {
    std.time::instant ended = std.time::monotonic_now();
    std.trace::record made = std.trace::record {
        .id = this.serial, .parent = this.parent, .task_id = this.task_id,
        .name = std.string::from_str(this.name),
        .start = std.time::instant_duration(this.started, this.core->epoch),
        .duration = std.time::instant_duration(ended, this.started), .attributes = copy_attributes(&this.attributes),
    };
    std.trace::record kept = copy_record(&made);
    const tracer_core* core = &*this.core;
    bool dropped = false;
    std.sync::lock_result<array<std.trace::record>> locked = std.sync::lock(&core->finished);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        dropped = keep(std.sync::mutex_guard_mut(&guard), move kept, core->capacity);
    case variant std.sync::lock_result::poisoned(move guard):
        dropped = keep(std.sync::mutex_guard_mut(&guard), move kept, core->capacity);
    case variant std.sync::lock_result::would_deadlock:
        drop kept;
        dropped = true;
    }
    if (dropped == true) { core::atomic_fetch_add(&core->dropped, 1u64, core::memory_order::relaxed) as void; }
    return move made;
}

protected array<std.trace::record> take_all(array<std.trace::record>* finished) {
    array<std.trace::record> none = [];
    return core::replace(finished, move none);
}

/* R-SLIB-TRACE-0002: takes the kept records, oldest first. */
array<std.trace::record> tracer::drain(const tracer* this) {
    std.sync::lock_result<array<std.trace::record>> locked = std.sync::lock(&this->core->finished);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return take_all(std.sync::mutex_guard_mut(&guard));
    case variant std.sync::lock_result::poisoned(move guard): return take_all(std.sync::mutex_guard_mut(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
    array<std.trace::record> none = [];
    return move none;
}

/* R-SLIB-TRACE-0002: the number of records dropped at capacity. */
u64 tracer::dropped(const tracer* this) {
    return core::atomic_load(&this->core->dropped, core::memory_order::relaxed);
}

/* ---- JSON ---- */

protected u8 hex_digit(u8 number) {
    if (number < 10u8) { return (number + 48u8) as u8; }
    return (number + 87u8) as u8;
}

protected void append_text(std.string::string* out, const u8[] piece) throws std.alloc::alloc_error {
    try {
        std.string::append_utf8(out, piece);
    } catch (std.string::string_error failure) {
        failure as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Appends a JSON string: quotes, backslashes and control characters escaped. */
protected void append_json_string(std.string::string* out, str text) throws std.alloc::alloc_error {
    std.string::append_str(out, "\"");
    const u8[] raw_bytes = text;
    usize start = 0usize;
    for (usize index = 0usize; index < len(raw_bytes); index += 1usize) {
        u8 c = raw_bytes[index];
        if (c == 34u8 || c == 92u8 || c < 32u8) {
            append_text(out, raw_bytes[start..index]);
            if (c == 34u8) { std.string::append_str(out, "\\\""); }
            if (c == 92u8) { std.string::append_str(out, "\\\\"); }
            if (c < 32u8) {
                std.string::append_str(out, "\\u00");
                std.string::push_scalar(out, hex_digit((c >> 4u8) as u8) as char);
                std.string::push_scalar(out, hex_digit((c & 15u8) as u8) as char);
            }
            start = index + 1usize;
        }
    }
    append_text(out, raw_bytes[start..len(raw_bytes)]);
    std.string::append_str(out, "\"");
}

/* Whole microseconds of a duration that is not negative. */
protected i64 microseconds(std.time::duration value) {
    return std.time::duration_seconds(value) * 1000000i64 + (std.time::duration_nanoseconds(value) / 1000u32) as i64;
}

/* R-SLIB-TRACE-0003: the record as one JSON object, its times in microseconds. */
std.string::string record::json(const std.trace::record* this) throws std.alloc::alloc_error {
    u64 number = this->id;
    u64 started_by = this->task_id;
    i64 offset = microseconds(this->start);
    i64 length = microseconds(this->duration);
    std.string::string out = f"{{\"id\":{number},\"parent\":";
    switch (this->parent) {
    case variant o::some(parent):
        u64 number = *parent;
        std.string::string text = f"{number}";
        std.string::append_str(&out, text);
    case variant o::none: std.string::append_str(&out, "null");
    }
    std.string::string middle = f",\"task\":{started_by},\"name\":";
    std.string::append_str(&out, middle);
    append_json_string(&out, this->name);
    std.string::string times = f",\"start_us\":{offset},\"duration_us\":{length},\"attributes\":{{";
    std.string::append_str(&out, times);
    const std.trace::attribute[] listed = std.array::as_slice(&this->attributes);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        if (index > 0usize) { std.string::append_str(&out, ","); }
        append_json_string(&out, listed[index].key);
        std.string::append_str(&out, ":");
        append_json_string(&out, listed[index].value);
    }
    std.string::append_str(&out, "}}");
    return move out;
}

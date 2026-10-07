module std.metrics;

/* R-SLIB-METRICS-0001: counters, gauges and histograms kept in a registry and rendered in the
   text exposition format 0.0.4 of Prometheus. A metric is updated through a handle from any
   task or thread: counters and gauges with atomic operations, histograms under a lock of their
   own; the registry takes its lock only to register and to render. */

/* R-SLIB-METRICS-0001: why a metric was refused. */
@derive(format)
enum error_code { invalid_name, invalid_label, conflict, invalid_buckets };

error metrics_error { error_code code; };

protected metrics_error refusal(error_code code) {
    return metrics_error {.code = code};
}

/* R-SLIB-METRICS-0004: the media type of the rendered text. */
const constexpr str content_type = "text/plain; version=0.0.4; charset=utf-8";

/* R-SLIB-METRICS-0002: one label of a metric. */
struct label { std.string::string name; std.string::string value; };

label label::of(str name, str value) throws std.alloc::alloc_error {
    return label {.name = std.string::from_str(name), .value = std.string::from_str(value)};
}

protected enum metric_kind { counter, gauge, histogram };

/* The observations of a histogram: the count of each bucket (not cumulative), the count of all
   and their sum. */
protected struct histogram_state { array<u64> buckets; u64 count; f64 sum; };

/* One metric of a family: the family name, help and kind, the rendered labels and the value. */
protected struct metric {
    metric_kind kind;
    std.string::string name;
    std.string::string help;
    std.string::string labels;
    atomic u64 total;
    atomic i64 level;
    array<f64> bounds;
    std.sync::mutex<histogram_state> observed;
};

protected struct registry_core { std.sync::mutex<array<arc metric>> metrics; };

/* R-SLIB-METRICS-0002: the metrics of a program, in the order of their registration. */
struct registry { protected arc registry_core core; };

/* R-SLIB-METRICS-0003: handles of one metric; share gives another handle of the same metric. */
struct counter { protected arc metric core; };
struct gauge { protected arc metric core; };
struct histogram { protected arc metric core; };

registry registry::create() throws std.alloc::alloc_error {
    array<arc metric> none = [];
    return registry {.core = new arc registry_core {.metrics = std.sync::mutex_new(move none)}};
}

/* ---- Names and text ---- */

/* A metric name ([a-zA-Z_:][a-zA-Z0-9_:]*) or, without colons, a label name. */
protected bool valid_name(str name, bool colons) {
    const u8[] raw_bytes = name;
    if (len(raw_bytes) == 0usize) { return false; }
    for (usize index = 0usize; index < len(raw_bytes); index += 1usize) {
        u8 c = raw_bytes[index];
        bool letter = (c >= 97u8 && c <= 122u8) || (c >= 65u8 && c <= 90u8) || c == 95u8 ||
                      (colons == true && c == 58u8);
        bool digit = c >= 48u8 && c <= 57u8;
        if (letter == false && (digit == false || index == 0usize)) { return false; }
    }
    return true;
}

protected bool same_text(str left, str right) {
    const u8[] first = left;
    const u8[] second = right;
    if (len(first) != len(second)) { return false; }
    for (usize index = 0usize; index < len(first); index += 1usize) {
        if (first[index] != second[index]) { return false; }
    }
    return true;
}

/* Appends a piece of valid UTF-8 text; a split at an ASCII byte keeps it valid. */
protected void append_piece(std.string::string* out, const u8[] piece) throws std.alloc::alloc_error {
    try {
        std.string::append_utf8(out, piece);
    } catch (std.string::string_error failure) {
        failure as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* Appends text with a backslash before each backslash and, for a label value, each quote, and
   with \n for each line feed. */
protected void append_escaped(std.string::string* out, str text, bool quotes) throws std.alloc::alloc_error {
    const u8[] raw_bytes = text;
    usize start = 0usize;
    for (usize index = 0usize; index < len(raw_bytes); index += 1usize) {
        u8 c = raw_bytes[index];
        if (c == 92u8 || c == 10u8 || (quotes == true && c == 34u8)) {
            append_piece(out, raw_bytes[start..index]);
            if (c == 10u8) { std.string::append_str(out, "\\n"); }
            if (c == 92u8) { std.string::append_str(out, "\\\\"); }
            if (c == 34u8) { std.string::append_str(out, "\\\""); }
            start = index + 1usize;
        }
    }
    append_piece(out, raw_bytes[start..len(raw_bytes)]);
}

/* The labels as name="value" pairs joined by commas; a name shall be valid, not reserved and
   not repeated, and a histogram shall not name le. */
protected std.string::string labels_text(const array<label>* labels, metric_kind kind)
    throws metrics_error, std.alloc::alloc_error {
    std.string::string text = std.string::create();
    const label[] listed = std.array::as_slice(labels);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        str name = listed[index].name;
        const u8[] raw_bytes = name;
        bool reserved = len(raw_bytes) >= 2usize && raw_bytes[0usize] == 95u8 && raw_bytes[1usize] == 95u8;
        if (valid_name(name, false) == false || reserved == true ||
            (kind == metric_kind::histogram && same_text(name, "le") == true)) {
            throw refusal(error_code::invalid_label);
        }
        for (usize earlier = 0usize; earlier < index; earlier += 1usize) {
            if (same_text(listed[earlier].name, name) == true) {
                throw refusal(error_code::invalid_label);
            }
        }
        if (index > 0usize) { std.string::append_str(&text, ","); }
        std.string::append_str(&text, name);
        std.string::append_str(&text, "=\"");
        append_escaped(&text, listed[index].value, true);
        std.string::append_str(&text, "\"");
    }
    return move text;
}

protected std.string::string copy_text(const u8[] text) throws std.alloc::alloc_error {
    std.string::string out = std.string::create();
    append_piece(&out, text);
    return move out;
}

protected void push_digit(array<u8>* digits, u8 digit) throws std.alloc::alloc_error {
    try {
        digits->push(digit);
    } catch (std.array::push_error<u8> rejected) {
        rejected as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The shortest digits of a number in plain decimal notation when its exponent lies in -5..20,
   as clients of Prometheus write it (0.005, not 5e-3); text is the shortest form of the number
   with an exponent. */
protected std.string::string expanded(const u8[] text) throws std.alloc::alloc_error {
    usize mark = len(text);
    for (usize index = 0usize; index < len(text); index += 1usize) {
        if (text[index] == 101u8 || text[index] == 69u8) { mark = index; }
    }
    if (mark == len(text)) { return copy_text(text); }
    bool negative_exponent = mark + 1usize < len(text) && text[mark + 1usize] == 45u8;
    i32 exponent = 0i32;
    for (usize index = mark + 1usize; index < len(text); index += 1usize) {
        if (text[index] >= 48u8 && text[index] <= 57u8) {
            exponent = exponent * 10i32 + (text[index] - 48u8) as i32;
        }
    }
    if (negative_exponent == true) { exponent = -exponent; }
    if (exponent < -5i32 || exponent > 20i32) { return copy_text(text); }
    std.string::string out = std.string::create();
    usize first = 0usize;
    if (len(text) > 0usize && text[0usize] == 45u8) {
        std.string::append_str(&out, "-");
        first = 1usize;
    }
    // The digits of the mantissa and the place of its point among them.
    array<u8> digits = [];
    i32 point = 0i32;
    bool seen_point = false;
    for (usize index = first; index < mark; index += 1usize) {
        if (text[index] == 46u8) { seen_point = true; }
        if (text[index] != 46u8) {
            push_digit(&digits, text[index]);
            if (seen_point == false) { point += 1i32; }
        }
    }
    i32 place = point + exponent;
    const u8[] all = std.array::as_slice(&digits);
    i32 count = len(all) as i32;
    if (place <= 0i32) {
        std.string::append_str(&out, "0.");
        for (i32 index = place; index < 0i32; index += 1i32) { std.string::append_str(&out, "0"); }
        append_piece(&out, all);
        return move out;
    }
    if (place >= count) {
        append_piece(&out, all);
        for (i32 index = count; index < place; index += 1i32) { std.string::append_str(&out, "0"); }
        return move out;
    }
    append_piece(&out, all[0usize..place as usize]);
    std.string::append_str(&out, ".");
    append_piece(&out, all[place as usize..len(all)]);
    return move out;
}

/* A number as the format writes it: +Inf, -Inf and NaN by name. */
protected std.string::string number_text(f64 value) throws std.alloc::alloc_error {
    if (value != value) { return std.string::from_str("NaN"); }
    if (value > 1.7976931348623157e308) { return std.string::from_str("+Inf"); }
    if (value < -1.7976931348623157e308) { return std.string::from_str("-Inf"); }
    std.string::string shortest = f"{value}";
    return expanded(shortest);
}

/* ---- Registration ---- */

protected void push_metric(array<arc metric>* listed, arc metric item) throws std.alloc::alloc_error {
    try {
        listed->push(move item);
    } catch (std.array::push_error<arc metric> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Adds a metric to the list unless its family has another kind or help, or the same labels. */
protected arc metric admit(array<arc metric>* listed, metric made) throws metrics_error, std.alloc::alloc_error {
    const (arc metric)[] present = std.array::as_slice(listed);
    for (usize index = 0usize; index < len(present); index += 1usize) {
        const metric* other = &*present[index];
        if (same_text(other->name, made.name) == true) {
            if (other->kind != made.kind || same_text(other->help, made.help) == false ||
                same_text(other->labels, made.labels) == true) {
                throw refusal(error_code::conflict);
            }
        }
    }
    arc metric shared = new arc metric(move made);
    push_metric(listed, std.arc::clone(&shared));
    return move shared;
}

protected arc metric register(const registry* owner, metric_kind kind, str name, str help,
                              const array<label>* labels, array<f64> bounds)
    throws metrics_error, std.alloc::alloc_error {
    if (valid_name(name, true) == false) { throw refusal(error_code::invalid_name); }
    array<u64> buckets = std.array::filled(len(bounds) + 1usize, 0u64);
    metric made = metric {
        .kind = kind, .name = std.string::from_str(name), .help = std.string::from_str(help),
        .labels = labels_text(labels, kind), .total = 0u64, .level = 0i64, .bounds = move bounds,
        .observed = std.sync::mutex_new(histogram_state {.buckets = move buckets, .count = 0u64, .sum = 0.0}),
    };
    std.sync::lock_result<array<arc metric>> locked = std.sync::lock(&owner->core->metrics);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return admit(std.sync::mutex_guard_mut(&guard), move made);
    case variant std.sync::lock_result::poisoned(move guard): return admit(std.sync::mutex_guard_mut(&guard), move made);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    throw refusal(error_code::conflict);
}

protected array<f64> no_bounds() {
    array<f64> none = [];
    return move none;
}

/* R-SLIB-METRICS-0003: a counter, a gauge or a histogram of the registry. */
counter registry::counter(const registry* this, str name, str help, array<label> labels)
    throws metrics_error, std.alloc::alloc_error {
    return counter {.core = register(this, metric_kind::counter, name, help, &labels, no_bounds())};
}

gauge registry::gauge(const registry* this, str name, str help, array<label> labels)
    throws metrics_error, std.alloc::alloc_error {
    return gauge {.core = register(this, metric_kind::gauge, name, help, &labels, no_bounds())};
}

/* The upper bounds shall be finite and increase strictly; +Inf is added by the format. */
histogram registry::histogram(const registry* this, str name, str help, array<label> labels, array<f64> bounds)
    throws metrics_error, std.alloc::alloc_error {
    const f64[] listed = std.array::as_slice(&bounds);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        f64 bound = listed[index];
        if (bound != bound || bound > 1.7976931348623157e308 || bound < -1.7976931348623157e308 ||
            (index > 0usize && bound <= listed[index - 1usize])) {
            throw refusal(error_code::invalid_buckets);
        }
    }
    return histogram {.core = register(this, metric_kind::histogram, name, help, &labels, move bounds)};
}

protected void push_bound(array<f64>* bounds, f64 bound) throws std.alloc::alloc_error {
    try {
        bounds->push(bound);
    } catch (std.array::push_error<f64> rejected) {
        rejected as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* R-SLIB-METRICS-0003: the bounds that clients of Prometheus use for durations in seconds. */
array<f64> seconds_buckets() throws std.alloc::alloc_error {
    array<f64> bounds = [];
    push_bound(&bounds, 0.005);
    push_bound(&bounds, 0.01);
    push_bound(&bounds, 0.025);
    push_bound(&bounds, 0.05);
    push_bound(&bounds, 0.1);
    push_bound(&bounds, 0.25);
    push_bound(&bounds, 0.5);
    push_bound(&bounds, 1.0);
    push_bound(&bounds, 2.5);
    push_bound(&bounds, 5.0);
    push_bound(&bounds, 10.0);
    return move bounds;
}

/* ---- Updates ---- */

void counter::increment(const counter* this) {
    core::atomic_fetch_add(&this->core->total, 1u64, core::memory_order::relaxed) as void;
}

void counter::add(const counter* this, u64 amount) {
    core::atomic_fetch_add(&this->core->total, amount, core::memory_order::relaxed) as void;
}

u64 counter::value(const counter* this) {
    return core::atomic_load(&this->core->total, core::memory_order::relaxed);
}

counter counter::share(const counter* this) {
    return counter {.core = std.arc::clone(&this->core)};
}

void gauge::set(const gauge* this, i64 value) {
    core::atomic_store(&this->core->level, value, core::memory_order::relaxed);
}

void gauge::add(const gauge* this, i64 amount) {
    core::atomic_fetch_add(&this->core->level, amount, core::memory_order::relaxed) as void;
}

i64 gauge::value(const gauge* this) {
    return core::atomic_load(&this->core->level, core::memory_order::relaxed);
}

gauge gauge::share(const gauge* this) {
    return gauge {.core = std.arc::clone(&this->core)};
}

protected void note(histogram_state* state, const f64[] bounds, f64 value) {
    usize slot = len(bounds);
    for (usize index = 0usize; index < len(bounds); index += 1usize) {
        if (value <= bounds[index] && slot == len(bounds)) { slot = index; }
    }
    state->buckets[slot] += 1u64;
    state->count += 1u64;
    state->sum += value;
}

/* R-SLIB-METRICS-0003: counts value in the first bucket whose bound is not below it. */
void histogram::observe(const histogram* this, f64 value) {
    const metric* shared = &*this->core;
    std.sync::lock_result<histogram_state> locked = std.sync::lock(&shared->observed);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        note(std.sync::mutex_guard_mut(&guard), std.array::as_slice(&shared->bounds), value);
    case variant std.sync::lock_result::poisoned(move guard):
        note(std.sync::mutex_guard_mut(&guard), std.array::as_slice(&shared->bounds), value);
    case variant std.sync::lock_result::would_deadlock: break;
    }
}

protected void copy_state(const histogram_state* source, histogram_state* target) throws std.alloc::alloc_error {
    const u64[] counts = std.array::as_slice(&source->buckets);
    for (usize index = 0usize; index < len(counts); index += 1usize) {
        try {
            target->buckets.push(counts[index]);
        } catch (std.array::push_error<u64> rejected) {
            rejected as void;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    target->count = source->count;
    target->sum = source->sum;
}

/* The count and the sum of the observations of a histogram. */
protected histogram_state snapshot(const metric* shared) throws std.alloc::alloc_error {
    array<u64> copied = [];
    histogram_state taken = histogram_state {.buckets = move copied, .count = 0u64, .sum = 0.0};
    std.sync::lock_result<histogram_state> locked = std.sync::lock(&shared->observed);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): copy_state(std.sync::mutex_guard_ref(&guard), &taken);
    case variant std.sync::lock_result::poisoned(move guard): copy_state(std.sync::mutex_guard_ref(&guard), &taken);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return move taken;
}

u64 histogram::count(const histogram* this) throws std.alloc::alloc_error {
    histogram_state taken = snapshot(&*this->core);
    return taken.count;
}

f64 histogram::sum(const histogram* this) throws std.alloc::alloc_error {
    histogram_state taken = snapshot(&*this->core);
    return taken.sum;
}

histogram histogram::share(const histogram* this) {
    return histogram {.core = std.arc::clone(&this->core)};
}

/* ---- Rendering ---- */

/* One sample line: name, suffix, the labels and an extra label, and the value. */
protected void sample(std.string::string* out, const metric* item, str suffix, str extra, str value)
    throws std.alloc::alloc_error {
    std.string::append_str(out, item->name);
    std.string::append_str(out, suffix);
    const u8[] labels = item->labels;
    const u8[] added = extra;
    if (len(labels) > 0usize || len(added) > 0usize) {
        std.string::append_str(out, "{");
        std.string::append_str(out, item->labels);
        if (len(labels) > 0usize && len(added) > 0usize) { std.string::append_str(out, ","); }
        std.string::append_str(out, extra);
        std.string::append_str(out, "}");
    }
    std.string::append_str(out, " ");
    std.string::append_str(out, value);
    std.string::append_str(out, "\n");
}

protected void render_metric(std.string::string* out, const metric* item) throws std.alloc::alloc_error {
    if (item->kind == metric_kind::counter) {
        u64 total = core::atomic_load(&item->total, core::memory_order::relaxed);
        std.string::string value = f"{total}";
        sample(out, item, "", "", value);
        return;
    }
    if (item->kind == metric_kind::gauge) {
        i64 level = core::atomic_load(&item->level, core::memory_order::relaxed);
        std.string::string value = f"{level}";
        sample(out, item, "", "", value);
        return;
    }
    histogram_state taken = snapshot(item);
    const f64[] bounds = std.array::as_slice(&item->bounds);
    u64 cumulative = 0u64;
    for (usize index = 0usize; index < len(bounds); index += 1usize) {
        cumulative += taken.buckets[index];
        std.string::string bound = number_text(bounds[index]);
        std.string::string extra = f"le=\"{bound}\"";
        std.string::string value = f"{cumulative}";
        sample(out, item, "_bucket", extra, value);
    }
    u64 count = taken.count;
    std.string::string all = f"{count}";
    sample(out, item, "_bucket", "le=\"+Inf\"", all);
    std.string::string sum = number_text(taken.sum);
    sample(out, item, "_sum", "", sum);
    sample(out, item, "_count", "", all);
}

protected str kind_name(metric_kind kind) {
    if (kind == metric_kind::counter) { return "counter"; }
    if (kind == metric_kind::gauge) { return "gauge"; }
    return "histogram";
}

protected void render_list(std.string::string* out, const array<arc metric>* listed) throws std.alloc::alloc_error {
    const (arc metric)[] present = std.array::as_slice(listed);
    for (usize index = 0usize; index < len(present); index += 1usize) {
        const metric* first = &*present[index];
        bool seen = false;
        for (usize earlier = 0usize; earlier < index; earlier += 1usize) {
            if (same_text(present[earlier]->name, first->name) == true) { seen = true; }
        }
        if (seen == false) {
            std.string::append_str(out, "# HELP ");
            std.string::append_str(out, first->name);
            std.string::append_str(out, " ");
            append_escaped(out, first->help, false);
            std.string::append_str(out, "\n# TYPE ");
            std.string::append_str(out, first->name);
            std.string::append_str(out, " ");
            std.string::append_str(out, kind_name(first->kind));
            std.string::append_str(out, "\n");
            for (usize member = index; member < len(present); member += 1usize) {
                if (same_text(present[member]->name, first->name) == true) {
                    render_metric(out, &*present[member]);
                }
            }
        }
    }
}

/* R-SLIB-METRICS-0004: every family in the order of its first registration, each with its help
   and type line and then its metrics in the order of their registration. */
std.string::string registry::render(const registry* this) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    std.sync::lock_result<array<arc metric>> locked = std.sync::lock(&this->core->metrics);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): render_list(&text, std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::poisoned(move guard): render_list(&text, std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return move text;
}

/* R-SLIB-METRICS-0002: another handle of the same registry. */
registry registry::share(const registry* this) {
    return registry {.core = std.arc::clone(&this->core)};
}

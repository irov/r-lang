module tests.std.metrics;
import std.test;
import std.metrics;
import std.text;

// The tests of std.metrics (Library R-SLIB-METRICS-0001..0004): registration and its refusals,
// counters, gauges and histograms updated from several tasks, and the text exposition format
// with help, type, escaped labels, cumulative buckets, +Inf, the sum and the count. Run in test
// mode (Core R-FUNC-0025).

protected array<std.metrics::label> labels() {
    array<std.metrics::label> none = [];
    return move none;
}

protected array<std.metrics::label> one(str name, str value) throws std.alloc::alloc_error {
    array<std.metrics::label> listed = labels();
    try {
        listed.push(std.metrics::label::of(name, value));
    } catch (std.array::push_error<std.metrics::label> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
    return move listed;
}

protected array<f64> bounds_of(f64 first, f64 second) throws std.alloc::alloc_error {
    try {
        array<f64> listed = [first, second];
        return move listed;
    } catch (std.array::push_error<f64> rejected) {
        rejected as void;
    }
    throw std.alloc::alloc_error::out_of_memory;
}

protected std.metrics::error_code refused(const std.metrics::registry* registry, str name, array<std.metrics::label> listed)
    throws std.alloc::alloc_error {
    try {
        std.metrics::counter made = registry->counter(name, "help", move listed);
        drop made;
    } catch (std.metrics::metrics_error failure) {
        return failure.code;
    }
    return std.metrics::error_code::invalid_buckets;
}

@test
void renders_every_kind() throws std.test::failure, std.metrics::metrics_error, std.alloc::alloc_error {
    std.metrics::registry registry = std.metrics::registry::create();
    std.metrics::counter served = registry.counter("http_requests_total", "Requests served.", one("route", "/health"));
    std.metrics::counter other = registry.counter("http_requests_total", "Requests served.", one("route", "say \"hi\"\n"));
    std.metrics::gauge active = registry.gauge("connections_active", "Open connections.\\now", labels());
    array<f64> bounds = bounds_of(0.5, 1.0);
    try {
        bounds.push(2.5);
    } catch (std.array::push_error<f64> rejected) {
        rejected as void;
    }
    std.metrics::histogram took = registry.histogram("request_seconds", "Time to answer.", one("route", "/"), move bounds);
    served.increment();
    served.add(2u64);
    other.increment();
    active.set(5i64);
    active.add(-2i64);
    took.observe(0.25);
    took.observe(1.0);
    took.observe(3.0);
    std.test::equal(served.value(), 3u64);
    std.test::equal(active.value(), 3i64);
    std.test::equal(took.count(), 3u64);
    std.test::check(took.sum() == 4.25, "the sum of the observations");
    std.string::string text = registry.render();
    std.test::equal_text(text.as_str(),
        "# HELP http_requests_total Requests served.\n"
        "# TYPE http_requests_total counter\n"
        "http_requests_total{route=\"/health\"} 3\n"
        "http_requests_total{route=\"say \\\"hi\\\"\\n\"} 1\n"
        "# HELP connections_active Open connections.\\\\now\n"
        "# TYPE connections_active gauge\n"
        "connections_active 3\n"
        "# HELP request_seconds Time to answer.\n"
        "# TYPE request_seconds histogram\n"
        "request_seconds_bucket{route=\"/\",le=\"0.5\"} 1\n"
        "request_seconds_bucket{route=\"/\",le=\"1\"} 2\n"
        "request_seconds_bucket{route=\"/\",le=\"2.5\"} 2\n"
        "request_seconds_bucket{route=\"/\",le=\"+Inf\"} 3\n"
        "request_seconds_sum{route=\"/\"} 4.25\n"
        "request_seconds_count{route=\"/\"} 3\n");
    std.test::equal_text(std.metrics::content_type, "text/plain; version=0.0.4; charset=utf-8");
}

@test
void refuses_invalid_metrics() throws std.test::failure, std.metrics::metrics_error, std.alloc::alloc_error {
    std.metrics::registry registry = std.metrics::registry::create();
    std.metrics::counter first = registry.counter("jobs_total", "help", one("queue", "a"));
    first.increment();
    std.test::check(refused(&registry, "9lives", labels()) == std.metrics::error_code::invalid_name, "digit first");
    std.test::check(refused(&registry, "", labels()) == std.metrics::error_code::invalid_name, "empty name");
    std.test::check(refused(&registry, "ok_name", one("__reserved", "x")) == std.metrics::error_code::invalid_label,
                    "reserved label");
    std.test::check(refused(&registry, "ok_name", one("bad-label", "x")) == std.metrics::error_code::invalid_label,
                    "label with a dash");
    std.test::check(refused(&registry, "jobs_total", one("queue", "a")) == std.metrics::error_code::conflict,
                    "the same labels again");
    try {
        std.metrics::gauge wrong = registry.gauge("jobs_total", "help", one("queue", "b"));
        drop wrong;
        std.test::check(false, "another kind in the family");
    } catch (std.metrics::metrics_error failure) {
        std.test::check(failure.code == std.metrics::error_code::conflict, "conflict of kinds");
    }
    try {
        array<f64> unsorted = bounds_of(1.0, 0.5);
        std.metrics::histogram wrong = registry.histogram("latency", "help", labels(), move unsorted);
        drop wrong;
        std.test::check(false, "decreasing bounds");
    } catch (std.metrics::metrics_error failure) {
        std.test::check(failure.code == std.metrics::error_code::invalid_buckets, "invalid_buckets");
    }
    try {
        std.metrics::histogram wrong = registry.histogram("latency", "help", one("le", "1"), std.metrics::seconds_buckets());
        drop wrong;
        std.test::check(false, "le on a histogram");
    } catch (std.metrics::metrics_error failure) {
        std.test::check(failure.code == std.metrics::error_code::invalid_label, "le is reserved");
    }
}

/* Counts from a task that owns another handle of the counter. */
protected async void count_up(std.metrics::counter counter, std.metrics::histogram took) throws std.error::fault {
    for (u32 index = 0u32; index < 1000u32; index += 1u32) {
        counter.increment();
        took.observe(0.001);
    }
}

@test
async void counts_from_several_tasks() throws std.test::failure, std.metrics::metrics_error, std.error::fault {
    std.metrics::registry registry = std.metrics::registry::create();
    std.metrics::counter counter = registry.counter("events_total", "Events.", labels());
    std.metrics::histogram took = registry.histogram("event_seconds", "Event time.", labels(), std.metrics::seconds_buckets());
    task_scope(4) workers {
        auto a = count_up(counter.share(), took.share());
        auto b = count_up(counter.share(), took.share());
        auto c = count_up(counter.share(), took.share());
        auto d = count_up(counter.share(), took.share());
        await move a;
        await move b;
        await move c;
        await move d;
    }
    std.test::equal(counter.value(), 4000u64);
    std.test::equal(took.count(), 4000u64);
    std.metrics::registry same = registry.share();
    std.string::string text = same.render();
    std.test::check(std.text::contains(text.as_str(), "event_seconds_bucket{le=\"0.005\"} 4000\n"), "first bucket");
    std.test::check(std.text::contains(text.as_str(), "events_total 4000\n"), "the counter");
}

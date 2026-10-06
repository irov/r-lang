module example.telemetry.service;
import std.http;
import std.metrics;
import std.trace;
import std.time;

/* The state that the handlers share: the metrics of the requests and the tracer of their work.
   Every field is a handle of a metric or tracer that the program also keeps. */
struct Telemetry {
    std.metrics::counter hello;
    std.metrics::counter items;
    std.metrics::gauge in_flight;
    std.metrics::histogram latency;
    std.trace::tracer tracer;
};

protected array<std.metrics::label> route(str path) throws std.alloc::alloc_error {
    array<std.metrics::label> labels = [];
    try {
        labels.push(std.metrics::label::of("route", path));
    } catch (std.array::push_error<std.metrics::label> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
    return move labels;
}

/* Registers the metrics of the service in the registry. */
Telemetry create(const std.metrics::registry* registry, std.trace::tracer tracer)
    throws std.metrics::metrics_error, std.alloc::alloc_error {
    array<std.metrics::label> none = [];
    array<std.metrics::label> nothing = [];
    std.metrics::counter greeted = registry->counter("telemetry_requests_total", "Requests answered by route.",
                                                     route("/hello"));
    std.metrics::counter listed = registry->counter("telemetry_requests_total", "Requests answered by route.",
                                                    route("/items"));
    std.metrics::gauge busy = registry->gauge("telemetry_requests_in_flight", "Requests being answered.", move none);
    std.metrics::histogram took = registry->histogram("telemetry_request_seconds", "Time to answer a request.",
                                                      move nothing, std.metrics::seconds_buckets());
    return Telemetry {.hello = move greeted, .items = move listed, .in_flight = move busy,
                      .latency = move took, .tracer = move tracer};
}

protected f64 seconds(std.time::duration value) {
    return std.time::duration_seconds(value) as f64 + std.time::duration_nanoseconds(value) as f64 / 1000000000.0;
}

/* Counts a request in flight and in the latency histogram, and keeps its span. */
protected void measured(const Telemetry* shared, std.trace::span span, std.time::instant started)
    throws std.time::time_error, std.alloc::alloc_error {
    shared->latency.observe(seconds(std.time::instant_duration(std.time::monotonic_now(), started)));
    std.trace::record done = (move span).finish();
    drop done;
    shared->in_flight.add(-1i64);
}

/* GET /hello */
async std.http::response hello(arc Telemetry state, std.http::request incoming) throws std.error::fault {
    const Telemetry* shared = &*state;
    shared->in_flight.add(1i64);
    std.time::instant started = std.time::monotonic_now();
    std.trace::span span = shared->tracer.start("GET /hello");
    span.attribute("target", incoming.target);
    shared->hello.increment();
    std.http::response answer = std.http::response::text(200u16, "hello\n");
    measured(shared, move span, started);
    return move answer;
}

/* GET /items/{id}: a JSON object, with a child span for the lookup. */
async std.http::response item(arc Telemetry state, std.http::request incoming) throws std.error::fault {
    const Telemetry* shared = &*state;
    shared->in_flight.add(1i64);
    std.time::instant started = std.time::monotonic_now();
    std.trace::span span = shared->tracer.start("GET /items");
    std.string::string id = std.string::create();
    switch (incoming.param("id")) {
    case variant o::some(value): std.string::append_str(&id, *value);
    case variant o::none: break;
    }
    std.trace::span lookup = span.child("lookup");
    lookup.attribute("id", id);
    std.trace::record found = (move lookup).finish();
    drop found;
    shared->items.increment();
    std.string::string body = f"{{\"id\":\"{id}\"}}";
    std.http::response answer = std.http::response::json(200u16, body);
    measured(shared, move span, started);
    return move answer;
}

/* GET /traces: the finished spans, one JSON object per line, oldest first. */
async std.http::response traces(arc Telemetry state, std.http::request incoming) throws std.error::fault {
    drop incoming;
    array<std.trace::record> kept = state->tracer.drain();
    std.string::string text = std.string::create();
    usize attributes = 0usize;
    for (usize index = 0usize; index < len(kept); index += 1usize) {
        std.string::string line = kept[index].json();
        std.string::append_str(&text, line);
        std.string::append_str(&text, "\n");
        const std.trace::attribute[] listed = std.array::as_slice(&kept[index].attributes);
        attributes += len(listed);
    }
    u64 dropped = state->tracer.dropped();
    std.http::response answer = std.http::response::text(200u16, text);
    std.string::string count = f"{dropped}";
    std.string::string described = f"{attributes}";
    // Decimal numbers are valid field values.
    try {
        answer.headers.add("X-Dropped-Spans", count);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    try {
        answer.headers.add("X-Span-Attributes", described);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move answer;
}

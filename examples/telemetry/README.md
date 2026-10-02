# HTTP service with metrics, traces, health and signals

Serve one HTTP application on TCP, TLS and a Unix-domain socket at once, export its metrics in the
text format of Prometheus at `/metrics`, report its health at `/health` and its spans at
`/traces`, close connections that stay idle, and stop with a drain on SIGTERM or SIGINT
(Library R-SLIB-SERVICE-0004..0007, R-SLIB-HTTP-0013..0014, R-SLIB-METRICS-0001..0004,
R-SLIB-TRACE-0001..0003).

```sh
ctest --test-dir build/debug -R 'example_telemetry' --output-on-failure
build/debug/tests/codegen_example_telemetry demo tests/fixtures/tls/authority.pem \
    tests/fixtures/tls/server.pem tests/fixtures/tls/server_key.pem demo.sock
build/debug/tests/codegen_example_telemetry serve 8080 8443 service.sock \
    tests/fixtures/tls/server.pem tests/fixtures/tls/server_key.pem
build/debug/tests/codegen_example_telemetry probe probe.sock
```

`demo` runs the service and a client of it in one program. The client asks over HTTP, HTTPS and
the Unix socket, reads the health, the metrics and the spans, and then sends SIGTERM to the
program, which drains the service:

```text
http GET /hello -> 200 hello
https GET /items/7 -> 200 {"id":"7"}
unix GET /hello -> HTTP/1.1 200 OK
http GET /health -> 200 ok
metrics in the exposition format: true
telemetry_requests_total{route="/hello"} 2
telemetry_requests_total{route="/items"} 1
telemetry_request_seconds_count 3
spans: 4
serving: true, accepted so far: 3
stopped: accepted 3, completed 3, failed 0, rejected 0, cancelled 0
serving after the signal: false
```

[main.r](src/main.r) builds one listener of each kind and serves them with
`std.http::serve_all`. The router answers `/health` and `/metrics` itself, before every route:

```r
table.add(std.http::method::get, "/hello", example.telemetry.service::hello);
table.health("/health", status->share());
table.metrics("/metrics", registry->share());
```

The options give every connection two seconds of idle time and make SIGTERM and SIGINT stop the
service with a drain (`stop_on_signals`); `std.service::health` tells the client whether the
service still accepts connections. A TLS connection performs its handshake in its own handler
task, so a slow client does not hold up the others.

[service.r](src/service.r) has the state of the handlers: two counters of one family with a
`route` label, a gauge of the requests in flight, a histogram of their latency and a tracer. Each
handler starts a span; the item handler also starts a child span for its lookup, and `/traces`
returns the finished spans as JSON lines.

`probe` serves plain connections of a Unix-domain socket with `std.service::serve_all` itself: its
handler receives a `std.service::connection`, a stream over the transport of its listener, and
answers with the index of that listener.

The behaviour test starts `serve` on free ports, asks over all three transports with Python
clients, parses the exposition and the spans, waits for the idle timeout to close a silent
connection and stops the service with SIGTERM.

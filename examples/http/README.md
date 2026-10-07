# JSON service over HTTP and HTTPS

A catalogue of items served as JSON by `std.http` (Library R-SLIB-HTTP-0001..0010) over plain
HTTP and over HTTPS, and requested by the `std.http` client of the same process; its prices also
arrive as server-sent events. Three smaller commands show `std.url`, `std.mime` and the message
functions of `std.http` on their own. Both servers
run on free loopback ports on top of `std.service`; the client keeps one connection to each and
reuses it for every request.

```sh
ctest --test-dir build/debug -R 'example_http' --output-on-failure
F=tests/fixtures/tls
build/debug/tests/codegen_example_http demo $F/authority.pem $F/server.pem $F/server_key.pem
build/debug/tests/codegen_example_http url 'HTTP://Example.COM:80/a/./b/../c%7e?x=1&y=a+b#f' '../d?q'
build/debug/tests/codegen_example_http mime 'Text/HTML; Charset="utf-8"'
build/debug/tests/codegen_example_http wire
```

The routes live in a `std.http::router` with the shared state `Catalog`. Each handler is an
ordinary async function that takes the state and the request and returns a response; `{id}`
captures a path segment:

```r
std.http::router<Catalog> result = std.http::router<Catalog>::create();
result.add(std.http::method::get, "/health", health);
result.add(std.http::method::get, "/items", list_items);
result.add(std.http::method::get, "/items/{id}", item);
result.add(std.http::method::post, "/items", add);
result.add(std.http::method::get, "/old-items", old_items);
result.add_stream(std.http::method::get, "/prices", prices);
result.before(require_key);
result.after(name_service);
```

`add` decodes the body with `std.json::unmarshal` and answers 201 with the new item, or 400 when
the JSON is not an item. The before hook `require_key` answers 401 to a POST without
`X-Api-Key: secret`, and the after hook adds `X-Service: catalog` to every response.
`/old-items` answers 301 with `Location: /items`, and the client follows it.

`/prices` is a streaming route: its handler gets a `std.http::body_writer`, sends the head with
`Content-Type: text/event-stream`, then one `std.http::sse_event` per item. The server runs the
handler next to the writer of the connection and writes each event as a chunk when it arrives:

```r
task_scope(1) begin {
    bool open = await writer.start(move head);
    if (open == false) { return; }
}
for (usize index = 0usize; index < len(state->items); index += 1usize) {
    ...
    std.string::string text = std.http::sse_event("price", event_id, data);
    task_scope(1) io {
        bool sent = await writer.send_text(text);
        if (sent == false) { return; }
    }
}
```

The client opens the stream with `std.http::client::open` (R-SLIB-HTTP-0012), which returns a
`std.http::streamed` once the head has arrived, on a connection of its own. Each
`streamed::next` returns the bytes that have arrived, which go to a `std.http::sse_parser`, so
every event is printed as soon as it is complete. That extra HTTPS connection is why the demo
counts two. The parser is drained by a loop whose condition is a pattern test (Core
R-STMT-0002), which ends at the first `o::none`:

```r
while (parser->next() is variant o::some(move message)) { ... }
```

The same router serves HTTP with `std.http::serve` and HTTPS with `std.http::serve_tls`; the client
verifies the server certificate against the authority it was given:

```r
auto http_server = std.http::serve(move plain, settings, move plain_stop,
                                   std.arc::clone(&state), example.http.catalog::routes(), bounds);
auto https_server = std.http::serve_tls(move secure, settings, move secure_stop,
                                        std.arc::clone(&state), example.http.catalog::routes(),
                                        bounds, move shared_tls);
```

```text
http GET /health -> 200 {"status":"ok"}
http GET /items/2 -> 200 {"id":2,"name":"lamp","price":30}
http GET /items/9 -> 404 {"error":"no item 9"}
http POST /items -> 401 {"error":"missing API key"}
http POST /items -> 201 {"id":4,"name":"desk","price":120}
http POST /items -> 400 {"error":"invalid item"}
http GET /old-items -> 200 [{"id":1,"name":"chair","price":45},{"id":2,"name":"lamp","price":30},{"id":3,"name":"shelf","price":80}]
https GET /health -> 200 {"status":"ok"}
https GET /items/3 -> 200 {"id":3,"name":"shelf","price":80}
https event price id=1 chair=45
https event price id=2 lamp=30
https event price id=3 shelf=80
connections: http 1, https 2
```

With an authority that did not sign the server certificate, the HTTPS requests fail with
`tls: untrusted_certificate` and status 65.

`url` parses a URL, prints its parts, its normal form, the pairs of its query and a reference
resolved against it, and encodes its path; `mime` parses a media type, or finds the media type of
a file name; `wire` runs a client and a server over one loopback TCP connection with the message
functions: the server reads the body of a POST in two steps (`read_body`, then
`read_whole_body`), answers in chunks (`write_chunked_head`, `write_chunk`, `finish_chunks`), then
reads a whole PUT (`read_request`) and answers 204 with `Connection: close`.

```text
$ http wire
server: POST /upload body 11 bytes in 2 reads
client: 200 Content-Type=text/plain body chunked reply
server: second request body 2, answering 204 No Content
client: 204 close=true
```

# WebSocket chat room found through DNS

A chat room served over WebSocket (`std.websocket`, Library R-SLIB-WS-0001..0005) on an upgrade
route of `std.http` (R-SLIB-HTTP-0011), and found by its clients through the SRV, TXT and PTR
records (`std.dns`, R-SLIB-DNS-0001..0006) of a name server that the same process runs on a
free UDP port of the loopback interface. The same server also switches a connection to a line
protocol of its own on another upgrade route. Smaller commands show the accept value of the
handshake, the wire form of a frame, the wire form of DNS queries and answers, reverse names and
the name servers of a resolv.conf file.

```sh
ctest --test-dir build/debug -R 'example_realtime' --output-on-failure
build/debug/tests/codegen_example_realtime demo
build/debug/tests/codegen_example_realtime accept dGhlIHNhbXBsZSBub25jZQ==
build/debug/tests/codegen_example_realtime frame Hello
build/debug/tests/codegen_example_realtime query _chat._tcp.realtime.test srv
build/debug/tests/codegen_example_realtime answer _chat._tcp.realtime.test 7001
build/debug/tests/codegen_example_realtime reverse 2001:db8::1
build/debug/tests/codegen_example_realtime resolv /etc/resolv.conf
```

`demo` prints:

```text
srv _chat._tcp.realtime.test -> localhost priority 10 weight 5
txt path=/chat
txt protocol=chat
ptr 127.0.0.1 -> localhost
alice <- * alice joined
alice <- * bob joined
bob <- * bob joined
alice <- alice: hello
bob <- alice: hello
alice <- bob: hi alice
bob <- bob: hi alice
bob closed 1000
alice <- * bob left
alice closed 1000
upgrade /line -> 101 QUIET WORDS
upgrade /chat -> 400
connections: 4
```

## Finding the service

`names.r` is the name server of the example. It reads each datagram with `std.dns::parse_query`,
looks the question up in its zone and answers with `std.dns::encode_answer`: an SRV record that
names `localhost` and the port of the HTTP server, two TXT strings with the path and the
subprotocol, and a PTR record for 127.0.0.1. Other questions get NXDOMAIN.

The client uses a `std.dns::resolver` over that one server (`resolver::system()` would read
`/etc/resolv.conf` instead):

```r
std.dns::resolver resolving = std.dns::resolver::with_servers(one(name_server));
array<std.dns::srv_record> services = await resolving.lookup_srv("_chat._tcp.realtime.test");
array<std.dns::txt_record> texts = await resolving.lookup_txt("_chat._tcp.realtime.test");
array<std.string::string> names = await resolving.reverse(std.net::parse_ip("127.0.0.1"));
```

`lookup_srv` sorts the records by priority, then by weight from the largest; a lookup asks the
servers in order over UDP, each within its timeout, and asks again over TCP when an answer is
truncated.

## The room

The room is shared state with a `std.async::broadcast<std.string::string>`. The route is an
upgrade route: its handler gets the request and a `std.http::upgrade`, the connection before any
response.

```r
result.add_upgrade(std.http::method::get, "/chat", join);
```

The handler subscribes to the room first, so a member sees every line published after its
connect returns, then answers the handshake with `std.websocket::accept`, which checks the
request (GET, `Upgrade: websocket`, version 13, a 16-byte key and the subprotocol `chat`) and
writes the 101 response with the accept value. Then two tasks run for the member: one forwards
the lines of the room to the socket, the other reads the socket and publishes each text message
as `name: text`. When the member closes, the reader ends, the forwarder is cancelled and the room
hears `* name left`.

```r
task_scope(2) session {
    auto sending = forward(&socket, move updates);
    await listen(&socket, hall, name);
    session.cancel_all();
    await session.all();
}
```

A `std.websocket::websocket` has one lock for reading and one for writing, so one task may wait in
`receive` while another sends. `receive` joins fragments, checks that text is UTF-8, answers pings
and returns the close of the peer, which it answers; a frame that breaks RFC 6455 is answered with
a close of status 1002, 1007 or 1009.

## The clients

Alice and Bob connect with `std.websocket::connect` through one `std.http::client`; the client
sends a random key and checks the 101 answer and its accept value. Each prints what it receives;
both close with status 1000 and receive the close of the server in answer.

## A protocol of its own

`/line` shows the upgrade without WebSocket. Its handler accepts the upgrade with the fields of
the 101 answer and gets a `std.http::upgraded`: the transport and the bytes the server already
read after the request, which come first. It reads one line, answers it in upper case and ends the
connection:

```r
std.http::upgraded switched = await (move connection).accept(move fields);
task_scope(1) io { await shout(&switched); }
```

The client sends its request with `std.http::client::upgrade`, which opens a connection outside the
pool and returns a `std.http::handshake`: the answer and, for 101, the connection. Asking `/chat`
for the line protocol gets the 400 of `std.websocket::accept`, and no connection.


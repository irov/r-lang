# Local services over Unix-domain sockets

A line service, a datagram collector and a signal check for programs that talk on one host.
Build and check with:

```sh
ctest --test-dir build-debug -R 'example_ipc' --output-on-failure
```

Use `build-debug/tests/codegen_example_ipc` in place of `ipc`:

```sh
ipc serve lines.sock 3 &
ipc call lines.sock 'Message for the local service'
ipc collect events.sock 2 64 &
ipc notify events.sock 'disk almost full'
ipc signal 3
```

`serve` listens at a socket path with `std.net::unix_listen`, replacing a socket file that a
previous run left behind, and prints `listening PATH` once clients can connect. It answers up
to LIMIT clients, one at a time: it reads the client's line with `unix_read_into` until the
client half-closes, asks the socket who connected with `unix_peer_credentials`, and writes back
`uid=... process_known=... length=... LINE` through `write_from`, which reports how much of the
reply each call accepted. It prints `served N` when it stops. SIGTERM and SIGINT stop it
between clients: the listeners for both signals exist before the ready line is printed, and
each turn waits in one `select` for the next client or a signal, cancelling the other waits.
The socket file stays after the listener closes.

`call` connects with `unix_connect`, writes its message with `unix_write_all_from` and the
newline through the `std.stream::Writer` implementation of `std.net::unix_stream`, half-closes
with `shutdown`, and reads the answer as one line with a `std.bufio::reader` over the stream.
The user identifier in the answer is the caller's own: the kernel records it when the
connection is made, so a service can decide what a local client may do.

`collect` binds a datagram socket at a path with `unix_datagram_bind`, prints
`collecting PATH`, and receives COUNT datagrams into a buffer of CAPACITY bytes with
`unix_receive_into`. Each line reports the received prefix, whether the datagram was cut and
the CRC-32 of the prefix. `notify` sends one datagram from an unnamed socket connected to the
path with `unix_datagram_connect`; an empty message is a real empty datagram. A datagram
receiver learns no sender; a reply channel is a stream connection.

`signal` raises SIGUSR1 at the program itself COUNT times, once through the receiver form
`std.signal::kind::user1.raise()`, and waits for the deliveries with a listener created
before the first one. The target may merge deliveries that arrive before the wait, so the
wait completes with at least one and at most COUNT.

Paths are file-system paths of at most 103 bytes; a longer one reports `invalid_address`. A
path without a socket reports `address_not_available`, a socket without a listener
`connection_refused`, and a datagram sent to a stream socket `unsupported`. Invalid arguments
exit with 64, invalid numbers with 65 and every network or signal failure with 69 and its
portable diagnostic. The tests start `serve` and `collect` as background processes in a
temporary directory, use relative socket paths, compare the answers with the test's own user
identifier and payload checksums with `zlib`, and stop the service with SIGTERM and SIGINT.

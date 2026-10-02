# Local network laboratory

A small network workbench for address handling and message delivery. Build and check with:

```sh
ctest --test-dir build-debug -R 'example_netlab' --output-on-failure
```

Use `build-debug/tests/codegen_example_netlab` in place of `netlab`:

```sh
netlab ip '2001:0db8:0:0:0:0:0:1'
netlab resolve localhost 8080 any
netlab tcp 'Message for the local receiver'
netlab handoff 'Message for the local receiver'
netlab udp 'A telemetry datagram' 64
netlab udp 'A telemetry datagram' 5
netlab options 1500 64
netlab dial localhost
```

`ip` parses IPv4 or IPv6 and prints its canonical spelling. `resolve` prints each address
with its requested port and scope; filters are `any`, `v4`, `v6`. The command accepts a host
chosen by the caller. Automated checks use only localhost and numeric loopback addresses.

`tcp` creates a loopback listener on an ephemeral port, connects a client and starts a receiving
task in the same task group. The client writes a one-byte protocol version followed by the UTF-8
message, then half-closes its write side. The receiver reads until EOF into a buffer that the
group lends it and computes the message length and CRC32. The report also verifies the observed
endpoint relationship. Every read and write is one line: the scoped operations
`tcp_read_into`, `tcp_write_all_from`, `udp_send_from` and `udp_receive_into` borrow the
caller's buffer while they run and throw their `net_error` (Library R-SLIB-ASYNC-0012), so no
call unpacks a tagged result.

`handoff` performs the same exchange with owned buffers, the style that the scoped operations
replace: each read or write takes its buffer, gives it back in a tagged result such as
`tcp_read_result::read(part)` or `tcp_write_all_result::failed(failure)`, and the caller unpacks
that result with a `switch` of four to six lines. Its output is the output of `tcp`. The two
`deliver` functions compare the styles: [tcp.r](src/tcp.r) needs 18 lines, one per operation,
[handoff.r](src/handoff.r) 37. The receiving loop of `handoff` is labeled `reading`, and the
`end` clause of its `switch` leaves it with `break reading;` (Core R-STMT-0004) instead of a
completion flag.

`udp` binds two loopback endpoints and sends one datagram, including an empty one. It prints
received bytes, CRC32 of the received prefix, the truncation flag and peer-address agreement.
Receive capacity may be zero. A target may impose a smaller native UDP send limit and report
`message_too_large`. Messages and capacities are limited to 60000 bytes; TCP messages
are limited to 1 MiB. No service listens on external interfaces.

`options` tunes a loopback TCP connection and a UDP socket. It turns off write coalescing,
sets the keepalive idle time from KEEPALIVE_MS milliseconds and the hop limit, and reads every
record back: the keepalive rounds up to whole seconds and at least one second, and a record
with keepalive none turns the probes off. On the UDP socket it permits broadcast, sets the
multicast hop limit and turns off the local copy of multicast datagrams, then joins the group
239.1.2.3 on the loopback interface, which reports `address_in_use` for a second join and
`address_not_available` for leaving a group it never joined. A hop limit outside 1..255 changes
nothing and reports `unsupported`.

`dial` connects to a loopback listener by name with `std.net::tcp_connect_name`, which tries
the resolver's addresses in order, so `localhost` reaches the IPv4 listener even when the
resolver lists `::1` first. It then connects with `std.net::tcp_connect_any` through a
candidate list whose first address has no listener and reports that the second one was used.

All network operations in a command share a five-second monotonic deadline. Each command runs
its exchange inside one `deadline (...) { ... }` block, and no call passes a deadline argument:
every operation started in the block, including those of the receiving task started there,
takes the deadline of the block, and an operation that outlives it fails with `timed_out`.
Handles close through ordinary cleanup; a failure in the middle of an exchange leaves the task
group, which cancels the receiving task and waits for it.

Invalid arguments are checked before any work starts and answered with the usage text and exit
code 64. Every standard failure reaches the one clause of `main`, `catch (std.error::fault
failure)`, which prints its portable diagnostic; the rethrow in `explain` picks the exit code:
65 for a number or address conversion, 69 for a network failure and 70 for any other failure.
Tests compare IP spellings with Python `ipaddress`, payload checksums with `zlib`, and the
resolver's local results with the requested family and port.

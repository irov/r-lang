# TCP service with a bounded handler group

Run a TCP service on a loopback port with `std.service` (Library R-SLIB-SERVICE-0001..0003), send
it one request per connection and print its account. The service accepts connections, runs one
handler task per connection in a bounded group, closes each connection after its timeout and
counts every handler outcome; the client stops it with a drain request. The command line is read
with `std.args`, the settings come in layers from `std.config`, and the service writes structured
records with `std.log` (see [Command line, settings and records](#command-line-settings-and-records)).

```sh
ctest --test-dir build-debug -R 'example_service' --output-on-failure
build-debug/tests/codegen_example_service count 5 7 x hold 11
```

`service echo REQUEST...` returns every request as it is. `service count REQUEST...` adds each
decimal request to one running total that all connections share, replies with the new total and
reports the amount to an auditor task. `service watch REQUEST...` keeps the total with a limit
and publishes every new total to a watcher (see [Shared state across await](#shared-state-across-await)). `service repeat REQUEST...` builds a reply of the
requested size inside a resource budget (see [A handler within a budget](#a-handler-within-a-budget)). The request `hold` sends nothing and keeps its side of the
connection open. Every request prints `REQUEST -> REPLY`, or `no reply` when the service closed
the connection without one; the account of the service follows, and for `count` the auditor's
summary:

```text
5 -> 5
7 -> 12
x -> no reply
hold -> no reply
11 -> 23
accepted=5 rejected=0 completed=3 failed=2 cancelled=0 last_failure=timed_out
audit additions=3 sum=23
```

`x` is not a number, so its handler throws the conversion error; `hold` keeps its handler waiting
for data until the connection timeout of one second ends the read with `timed_out`. Both failures
reach the account, which keeps the portable error of the last one. Standard error receives the
records of the run, one line each:

```text
time=2026-09-29T06:43:45.892Z level=info task=2 message="service starting" mode=count capacity=64 timeout_ms=1000 requests=5
time=2026-09-29T06:43:46.899Z level=warn task=4 message="service stopped" accepted=5 rejected=0 completed=3 failed=2 cancelled=0 last_failure=timed_out
```

[main.r](src/main.r) starts the service. A handler is an ordinary async function that owns its
connection and may throw any standard error, the root `std.error::fault`. `serve` runs one for
each connection; `serve_with` also gives each one a clone of a shared `arc` state:

```r
auto server = std.service::serve(move listener, options, move stop,
                                 example.service.handlers::echo);
...
auto server = std.service::serve_with(move listener, options, move stop, move total,
                                      example.service.handlers::count);
```

At most `capacity` handlers run at once, 64 unless the settings say otherwise. The options name
only the capacity and the timeout; `overflow` keeps its declared initializer, `overflow::wait`, so a
connection beyond them stays with the listener until a handler ends and its slot is free;
`overflow::reject` would close it and count it as rejected. `std.service::options {}` alone is 64
handlers, a 30-second timeout and `overflow::wait`. The service stops
accepting when its stop receiver yields a request or loses every sender: `stop::drain` waits for
the running handlers at most one timeout, `stop::cancel` cancels them at once. Cancelling the task
that runs `serve` cancels the handlers too.

[handlers.r](src/handlers.r) needs no deadline and no error plumbing. Every read and write is one
line inside a task group, and the connection timeout set by the service bounds each of them
(Core R-STMT-0019):

```r
async void echo(std.net::tcp_connection connection) throws std.error::fault {
    array<u8> buffer = std.alloc::bytes(64usize, 0u8);
    task_scope(1) io {
        usize length = await example.service.wire::read_all(&connection.stream, buffer.as_slice_mut());
        const u8[] request = buffer.as_slice();
        await std.net::tcp_write_all_from(&connection.stream, request[0usize..length]);
    }
}
```

The shared state of `count` holds the total under a `std.async::mutex` and the sender of a
bounded channel of two amounts. The auditor started beside the service reads the amounts with
an asynchronous `for` over its receiver (Core R-STMT-0021): each iteration awaits the
asynchronous receive of the next amount, which suspends the task instead of blocking a thread
(Library R-LIB-0016), and the loop ends when the service has returned and its state with the
last sender is gone:

```r
for (u64 amount in &events) {
    additions += 1u64;
    sum += amount;
}
```

`main` catches `std.error::fault` and prints its portable name. The helpers declare the same
root, `throws std.error::fault`: an error that a function does not catch leaves it on one path,
whichever standard error it is, and a catch of the root receives every member on one path too,
so the root costs no more code or stack than an exact set of errors. The errors of the command
line and of the settings are not standard errors; `main` catches them by name and ends with
status 64 or 78.

## Command line, settings and records

`std.args` (Library R-SLIB-ARGS-0001..0003) declares the options, the mode and the requests;
`-h` or no arguments at all print the help that it builds:

```text
service - a TCP service with a bounded handler group
usage: service [options] [mode] [request...]
  -c, --config FILE        JSON settings file
  --capacity N             handlers at once
  -t, --timeout_ms MS      connection timeout in milliseconds
  --log.level LEVEL        lowest level of the records
  --log.format FORMAT      text or json records
  --log.file FILE          append the records to FILE
  --budget_bytes N         bytes a repeat handler may hold
  -v, --verbose            records from level debug
  mode                     echo, count, watch, repeat, serve or show
  request                  requests to send
  -h, --help               print this help
```

An unknown option, an option without its value, an unknown mode or requests that the mode does
not take print the reason and the help and end with status 64:

```r
std.args::parser parser =
    std.args::parser::create("service", "a TCP service with a bounded handler group");
parser.option("config", "c", "FILE", "JSON settings file");
parser.option("capacity", "", "N", "handlers at once");
...
parser.flag("verbose", "v", "records from level debug");
parser.positional("mode", "echo, count, watch, repeat, serve or show", false);
parser.remaining("request", "requests to send");
```

`std.config` (Library R-SLIB-CONFIG-0001..0003) holds the settings `capacity`, `timeout_ms`,
`log.level`, `log.format`, `log.file` and `budget_bytes` with their defaults. Each later layer replaces the
settings it names: the JSON file of `--config`, where `{"log": {"level": "warn"}}` names
`log.level`, then the variables `SERVICE_CAPACITY`, `SERVICE_TIMEOUT_MS`, `SERVICE_LOG_LEVEL` and
so on, then the options, whose names are those of the settings; `-v` sets `log.level` to `debug`.
`service show` prints every setting with the layer it came from:

```sh
SERVICE_CAPACITY=8 build-debug/tests/codegen_example_service -c service.json --timeout_ms 250 show
```

```text
capacity=8 (environment)
timeout_ms=250 (arguments)
log.level=warn (file)
log.format=text (default_value)
log.file= (default_value)
```

```r
std.config::config settings = defaults();
...
task_scope(1) loading { await settings.load_file(&path); }
...
settings->load_environment("SERVICE");
settings->load_arguments(found);
if (found->has("verbose") == true) {
    settings->set("log.level", "debug", std.config::source::arguments);
}
```

A setting that is not a number where one is needed, a level or format that does not exist, or a
file that is not a JSON object of known settings ends the program with status 78; a file that
fails changes no setting.

`std.log` (Library R-SLIB-LOG-0001..0003) formats each record on the task that logs it and hands
the line to the bounded queue of one writer, which a task of its own drains to standard error,
or to the file of `log.file`, until every logger is gone. A record never waits: when the queue is
full, it is dropped and counted. `main` makes the writer and the first logger, and the count
handlers share another one through the state of the service:

```r
std.log::writer sink = std.log::writer::create(256usize);
std.log::logger journal = sink.logger(level_of(&settings), format_of(&settings));
...
task_scope(2) logging {
    auto records = write_records(move sink, move target);
    auto program = run(mode, move requests, options, move journal);
    status += await move program;
    u64 written = await move records;
    written as void;
}
```

A record is `time=... level=... task=... message=...` and its fields, or one JSON object with
`--log.format json`; `task` is the identifier of the task that wrote it (Library
R-SLIB-ASYNC-0018). The service logs its start with the mode and the options, its stop with the
account at `warn` when a handler failed, and a stop request with its signal. With `-v` the count
handlers also log each amount; `enabled` keeps them from building the fields when debug records
are not written:

```r
if (total->journal.enabled(std.log::level::debug) == true) {
    std.log::fields extra = std.log::fields::create();
    extra.number("amount", std.convert::checked_i64(amount));
    extra.number("total", std.convert::checked_i64(sum));
    total->journal.log(std.log::level::debug, "amount added", &extra);
}
```

## Shared state across await

A guard of `std.async::mutex` owns a reference to its lock, so it may stay live across `await`
(Library R-SLIB-ASYNC-0013); a `std.sync` guard may not, and the compiler names the `std.async`
lock to use instead. The count handler keeps the guard while it reports the amount, so the
auditor sees the amounts in the order they entered the total. `try_reserve` takes a free slot of
the channel at once; when the auditor is behind, `reserve` waits for room, so a slow auditor
slows the handlers instead of growing a queue. The permit sends into the slot it holds and never
waits:

```r
std.async::mutex_guard<u64> guard = await total->value.lock();
*(guard.get_mut()) += amount;
u64 sum = *(guard.get());
std.sync::try_reserve_result<u64> slot = total->audit.try_reserve();
switch (move slot) {
case variant std.sync::try_reserve_result::reserved(move permit): (move permit).send(amount);
case variant std.sync::try_reserve_result::full:
    std.sync::reserve_result<u64> room = await total->audit.reserve();
    ...
}
(move guard).unlock();
```

`watch` shares the state of [books.r](src/books.r): the total under a mutex, the limit under a
`std.async::rw_lock` that handlers read at once and `limit=N` replaces, a `std.async::semaphore`
gate and a `std.async::broadcast` of totals (R-SLIB-ASYNC-0013..0016). An amount above the limit
is refused; `status` reports the state with `try_lock`, `try_read` and `available_permits`, which
never wait:

```sh
build-debug/tests/codegen_example_service watch 5 7 status limit=6 9 4 status
```

```text
5 -> 5
7 -> 12
status -> total=12 limit=100 free=2
limit=6 -> was 100
9 -> refused above 6
4 -> 16
status -> total=16 limit=6 free=2
accepted=7 rejected=0 completed=7 failed=0 cancelled=0
watch lagged=1 12 16
books total=16 limit=6 free=2
```

The watcher subscribes, starts its wait for `closing` and only then tells main with `notify_one`
that it listens; main opens the gate with `add_permits`, runs the service and wakes the watcher
with `notify_all`, which wakes the waiters that exist and stores nothing. Every subscriber has a
queue of two totals here, so the watcher that reads late finds the oldest total gone: the first
receive reports `lagged(1)`, the kept totals follow, and `closed` ends the loop once no broadcast
handle is left:

```r
std.async::broadcast_result<u64> next = await inbox.receive();
switch (move next) {
case variant std.async::broadcast_result::received(move total): ...
case variant std.async::broadcast_result::lagged(move missed): ...
case variant std.async::broadcast_result::closed: open = false;
}
```

## A handler within a budget

`service repeat SIZE...` asks the handler of each connection to build a reply of SIZE bytes, as
lines of at most 64 bytes, and to answer with what it built. The build runs inside a budget
block (Core R-STMT-0020) of `budget_bytes` bytes, 4096 by default:

```r
budget (std.alloc::limits {.bytes = o::some(settings->budget_bytes)}) {
    try {
        array<array<u8>> lines = lines_of(size);
        core::replace(&built, o::some(len(lines))) as void;
        drop lines;
    } catch (std.alloc::alloc_error failure) {
        refused = o::some(failure);
    }
}
```

Every allocation that the handler makes inside the block, the lines and the array that holds
them, is charged to the budget, and the budget returns the bytes of each allocation when it is
released. An allocation that would take the budget beyond its limit fails with
`std.alloc::alloc_error::budget_exhausted`. The error leaves `lines_of` as any checked error
does, so the lines built so far are released and the budget is free again. The handler answers
with the refusal, and the service goes on with the next connection. The connection, its buffer
and the reply live outside the block and are not charged:

```text
$ service repeat 1000 100000 50 x
1000 -> 1000 bytes in 16 lines
100000 -> refused budget_exhausted
50 -> 50 bytes in 1 lines
x -> no reply
accepted=4 rejected=0 completed=3 failed=1 cancelled=0 last_failure=invalid_digit
```

The bytes held at once are more than the size of the reply. While the array of lines grows,
its old storage is held until the new one has taken the elements. With `--budget_bytes 200000`
the request 60000 fits. A budget also limits the tasks that the block starts, with
`.tasks = o::some(N)` and `std.async::start_error::budget_exhausted` for a start beyond it, and
a task started in the block keeps the budget of the block.

## Stop on a signal

`service serve` runs the echo service until the process receives SIGTERM or SIGINT, then drains
it: the service stops accepting, the handlers that run finish within one timeout, and the account
follows the signal that stopped it; standard error also receives the record
`message="stop requested" signal=SIGTERM`:

```text
$ service serve
listening 127.0.0.1:50120
stopped by SIGTERM
accepted=3 rejected=0 completed=3 failed=0 cancelled=0
```

The two listeners of `std.signal` (Library R-SLIB-SIGNAL-0001..0003) exist before the address is
printed, so a signal sent after it is never missed and never takes the default action of ending
the process. One task races their waits in a group and sends the drain request; the other wait is
cancelled and leaves its signal counted:

```r
task_scope(2) signals {
    auto term = terminate.next();
    auto intr = interrupt.next();
    select (signals) {
    case u64 count = await move term: count as void; break;
    case u64 count = await move intr: count as void; by_terminate = false; break;
    }
    signals.cancel_all();
    await signals.all();
}
```

`tests/run_service_examples.py` sends SIGTERM while one connection is still sending its request;
the drain lets its handler finish, so the request is echoed and counted as completed. It also
checks the help and the errors of the command line, the layers of the settings, the records in
text, in JSON and in a file, and the refusals of the budget in `repeat`.

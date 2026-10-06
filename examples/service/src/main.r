module example.service.main;
import std.service;
import std.console;
import std.args;
import std.config;
import std.log;
import example.service.handlers;
import example.service.books;
import example.service.client;

enum Mode { echo, count, watch, repeat, serve, show };

/* A mode that does not exist, or requests that the mode does not take. */
error Usage { constexpr str message; };

/* A setting whose text names no log level or record format. */
error Setting { constexpr str message; };

/* The command line (Library R-SLIB-ARGS-0001): every option except --config and -v has the name
   of a setting of defaults(), so load_arguments takes its value as the last layer. */
std.args::parser command_line() throws std.args::args_error, std.alloc::alloc_error {
    std.args::parser parser =
        std.args::parser::create("service", "a TCP service with a bounded handler group");
    parser.option("config", "c", "FILE", "JSON settings file");
    parser.option("capacity", "", "N", "handlers at once");
    parser.option("timeout_ms", "t", "MS", "connection timeout in milliseconds");
    parser.option("log.level", "", "LEVEL", "lowest level of the records");
    parser.option("log.format", "", "FORMAT", "text or json records");
    parser.option("log.file", "", "FILE", "append the records to FILE");
    parser.option("budget_bytes", "", "N", "bytes a repeat handler may hold");
    parser.flag("verbose", "v", "records from level debug");
    parser.positional("mode", "echo, count, watch, repeat, serve or show", false);
    parser.remaining("request", "requests to send");
    return move parser;
}

/* The settings with their defaults (Library R-SLIB-CONFIG-0001). */
std.config::config defaults() throws std.config::config_error, std.alloc::alloc_error {
    std.config::config settings = std.config::config::create();
    settings.define("capacity", "64", "handlers at once");
    settings.define("timeout_ms", "1000", "connection timeout in milliseconds");
    settings.define("log.level", "info", "lowest level of the records");
    settings.define("log.format", "text", "text or json records");
    settings.define("log.file", "", "file of the records, standard error when empty");
    settings.define("budget_bytes", "4096", "bytes a repeat handler may hold");
    return move settings;
}

/* The layers after the file: the SERVICE_* variables, then the options; -v lowers the level. */
void layer(std.config::config* settings, const std.args::matches* found)
    throws std.args::args_error, std.config::config_error, std.error::fault {
    settings->load_environment("SERVICE");
    settings->load_arguments(found);
    if (found->has("verbose") == true) {
        settings->set("log.level", "debug", std.config::source::arguments);
    }
}

/* The options of the service. overflow keeps its declared initializer, overflow::wait (Core
   R-INIT-0004). */
std.service::options options_of(const std.config::config* settings)
    throws std.config::config_error, Setting, std.error::fault {
    u64 handlers = settings->get_u64("capacity");
    throw (handlers == 0u64 || handlers > (std.service::max_capacity as u64))
        Setting {.message = "capacity is 1 to std.service::max_capacity"};
    u32 capacity = std.convert::checked_u32(handlers);
    u64 milliseconds = settings->get_u64("timeout_ms");
    i64 seconds = std.convert::checked_i64(milliseconds / 1000u64);
    u32 nanoseconds = std.convert::checked_u32((milliseconds % 1000u64) * 1000000u64);
    return std.service::options {.capacity = capacity,
                                 .timeout = std.time::duration_from_parts(seconds, nanoseconds)};
}

std.log::level level_of(const std.config::config* settings) throws std.config::config_error, Setting {
    o<std.log::level> named = core::enum_from_name::<std.log::level>(settings->get("log.level"));
    switch (named) {
    case variant o::some(value): return *value;
    case variant o::none: throw Setting {.message = "log.level names no level"};
    }
}

std.log::format format_of(const std.config::config* settings) throws std.config::config_error, Setting {
    o<std.log::format> named = core::enum_from_name::<std.log::format>(settings->get("log.format"));
    switch (named) {
    case variant o::some(value): return *value;
    case variant o::none: throw Setting {.message = "log.format is neither text nor json"};
    }
}

struct Written { u64 value; };

/* Write the records until every logger is gone (Library R-SLIB-LOG-0003): to the file of
   log.file, or to standard error when it is empty. */
async u64 write_records(std.log::writer sink, std.string::string file) throws std.error::fault {
    if (std.string::len(&file) == 0usize) {
        return await std.log::writer::to_stderr(move sink);
    }
    std.fs::path path = std.fs::path_from_utf8(file);
    Written written = {.value = 0u64};
    task_scope(1) writing { written.value = await std.log::writer::to_file(move sink, &path); }
    return written.value;
}

/* The account of a service as the fields of a record. */
std.log::fields outcome(const std.service::report* report) throws std.error::fault {
    std.log::fields extra = std.log::fields::create();
    extra.number("accepted", std.convert::checked_i64(report->accepted));
    extra.number("rejected", std.convert::checked_i64(report->rejected));
    extra.number("completed", std.convert::checked_i64(report->completed));
    extra.number("failed", std.convert::checked_i64(report->failed));
    extra.number("cancelled", std.convert::checked_i64(report->cancelled));
    switch (report->last_failure) {
    case variant o::some(error):
        auto name = std.error::name(*error);
        extra.text("last_failure", name);
    case variant o::none: break;
    }
    return move extra;
}

/* Log the account: info when every handler completed, warn otherwise. */
void stopped(const std.log::logger* journal, const std.service::report* report)
    throws std.error::fault {
    std.log::fields extra = outcome(report);
    std.log::level severity = std.log::level::info;
    if (report->failed != 0u64 || report->cancelled != 0u64) { severity = std.log::level::warn; }
    journal->log(severity, "service stopped", &extra);
}

/* Count the amounts that the count handlers report until the service and its shared state are
   gone, which ends the channel. The channel holds two amounts; a handler that finds it full
   waits for room while this task catches up. Each iteration awaits the next amount. */
async std.string::string audit(std.sync::receiver<u64> events) throws std.error::fault {
    u64 additions = 0u64;
    u64 sum = 0u64;
    for (u64 amount in &events) {
        additions += 1u64;
        sum += amount;
    }
    return f"audit additions={additions} sum={sum}\n";
}

std.string::string account(std.service::report report) throws std.error::fault {
    std.string::string line = f"accepted={report.accepted} rejected={report.rejected} completed={report.completed} failed={report.failed} cancelled={report.cancelled}";
    switch (report.last_failure) {
    case variant o::some(error):
        auto name = std.error::name(*error);
        line.append(" last_failure=");
        line.append(name);
    case variant o::none: break;
    }
    line.append("\n");
    return move line;
}

/* Send every request in order and print each reply, then ask the service to drain. */
async i32 send_all(std.net::socket_address endpoint, array<std.string::string> requests,
                   std.sync::sender<std.service::stop> stopper) throws std.error::fault {
    i32 status = 0;
    for (usize index = 0usize; index < len(requests); index += 1usize) {
        std.string::string request = std.string::from_str(requests[index]);
        std.string::string line = std.string::from_str(requests[index]);
        line.append(" -> ");
        o<std.string::string> reply = await example.service.client::exchange(endpoint, move request);
        switch (move reply) {
        case variant o::some(move text): line.append(text);
        case variant o::none: line.append("no reply");
        }
        line.append("\n");
        await std.console::print(move line);
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return status;
}

/* Run the watch mode. The watcher subscribes before main opens the gate, and main wakes it once
   the service has returned, so what the watcher reads depends only on its queue of two totals. */
async i32 watched(std.net::tcp_listener listener, std.net::socket_address endpoint,
                  array<std.string::string> requests, std.sync::sender<std.service::stop> stopper,
                  std.sync::receiver<std.service::stop> stop, std.service::options options,
                  std.log::logger journal) throws std.error::fault {
    std.async::mutex<u64> total = std.async::mutex_new(0u64);
    std.async::rw_lock<example.service.books::Limits> limits =
        std.async::rwlock_new(example.service.books::Limits {.most = 100u64});
    std.async::semaphore gate = std.async::semaphore_new(0usize);
    std.async::broadcast<u64> totals = std.async::broadcast(2usize);
    std.async::notify ready = std.async::notify_new();
    std.async::notify closing = std.async::notify_new();
    arc example.service.books::Books shared = new arc example.service.books::Books {
        .total = total.clone(), .limits = limits.clone(), .gate = gate.clone(),
        .totals = totals.clone()};
    i32 status = 0;
    task_scope(3) group {
        auto watcher = example.service.books::watch_totals(move totals, ready.clone(),
                                                           closing.clone());
        await ready.notified();
        gate.add_permits(2usize);
        auto server = std.service::serve_with(move listener, options, move stop, move shared,
                                              example.service.books::handle);
        auto client = send_all(endpoint, move requests, move stopper);
        status += await move client;
        std.service::report report = await move server;
        stopped(&journal, &report);
        await std.console::print(account(report));
        closing.notify_all();
        std.string::string seen = await move watcher;
        await std.console::print(move seen);
    }
    std.async::mutex_guard<u64> last = await total.lock();
    std.async::rw_read_guard<example.service.books::Limits> bound = await limits.read();
    u64 sum = *(last.get());
    u64 most = (bound.get())->most;
    usize free = gate.available_permits();
    (move bound).unlock();
    (move last).unlock();
    await std.console::print(f"books total={sum} limit={most} free={free}\n");
    return status;
}

/* Drain the service at SIGTERM or SIGINT (Library R-SLIB-SIGNAL-0002): the listeners exist before
   the address is printed, so no signal sent after it is missed. The other wait is cancelled. */
async bool drain_on_signal(std.signal::listener terminate, std.signal::listener interrupt,
                           std.sync::sender<std.service::stop> stopper) throws std.error::fault {
    bool by_terminate = true;
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
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return by_terminate;
}

/* Serve echo requests until SIGTERM or SIGINT, then drain the running handlers and print the
   account. */
async i32 serve_until_signal(std.net::tcp_listener listener, std.net::socket_address endpoint,
                             std.sync::sender<std.service::stop> stopper,
                             std.sync::receiver<std.service::stop> stop,
                             std.service::options options, std.log::logger journal)
    throws std.error::fault {
    std.signal::listener terminate = std.signal::kind::terminate.listen();
    std.signal::listener interrupt = std.signal::kind::interrupt.listen();
    await std.console::print(f"listening {endpoint}\n");
    task_scope(2) group {
        auto server = std.service::serve(move listener, options, move stop,
                                         example.service.handlers::echo);
        auto watcher = drain_on_signal(move terminate, move interrupt, move stopper);
        bool by_terminate = await move watcher;
        str name = "SIGINT";
        if (by_terminate == true) { name = "SIGTERM"; }
        std.log::fields cause = std.log::fields::create();
        cause.text("signal", name);
        journal.log(std.log::level::info, "stop requested", &cause);
        std.string::string line = f"stopped by {name}\n";
        std.service::report report = await move server;
        stopped(&journal, &report);
        await std.console::print(move line);
        await std.console::print(account(report));
    }
    return 0;
}

// Run the service on a loopback port, send it the requests, then print its account.
async i32 run(Mode mode, array<std.string::string> requests, std.service::options options,
              usize budget_bytes, std.log::logger journal) throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options listening = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(listening);
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    i32 status = 0;
    switch (mode) {
    case Mode::echo:
        task_scope(2) group {
            auto server = std.service::serve(move listener, options, move stop,
                                             example.service.handlers::echo);
            auto client = send_all(endpoint, move requests, move stopper);
            status += await move client;
            std.service::report report = await move server;
            stopped(&journal, &report);
            await std.console::print(account(report));
        }
        drop journal;
    case Mode::count:
        std.sync::sync_channel<u64> amounts = std.sync::sync_channel(2usize);
        arc example.service.handlers::Total total =
            new arc example.service.handlers::Total {.value = std.async::mutex_new(0u64),
                                                     .audit = std.sync::sync_sender(&amounts),
                                                     .journal = journal.share()};
        std.sync::receiver<u64> events = std.sync::sync_receiver(move amounts);
        task_scope(3) group {
            auto server = std.service::serve_with(move listener, options, move stop, move total,
                                                  example.service.handlers::count);
            auto auditor = audit(move events);
            auto client = send_all(endpoint, move requests, move stopper);
            status += await move client;
            std.service::report report = await move server;
            stopped(&journal, &report);
            await std.console::print(account(report));
            std.string::string summary = await move auditor;
            await std.console::print(move summary);
        }
        drop journal;
    case Mode::repeat:
        arc example.service.handlers::Repeat repeating =
            new arc example.service.handlers::Repeat {.budget_bytes = budget_bytes};
        task_scope(2) group {
            auto server = std.service::serve_with(move listener, options, move stop, move repeating,
                                                  example.service.handlers::repeat);
            auto client = send_all(endpoint, move requests, move stopper);
            status += await move client;
            std.service::report report = await move server;
            stopped(&journal, &report);
            await std.console::print(account(report));
        }
        drop journal;
    case Mode::watch:
        status += await watched(move listener, endpoint, move requests, move stopper, move stop,
                                options, move journal);
    case Mode::serve:
        drop requests;
        status += await serve_until_signal(move listener, endpoint, move stopper, move stop,
                                           options, move journal);
    case Mode::show:
        // launch prints the settings without a service.
        endpoint as void;
        drop requests;
        drop listener;
        drop stopper;
        drop stop;
        drop journal;
    }
    return status;
}

/* Build the settings in layers, then print them for show, or run the service and write its
   records beside it until every logger is gone. */
async i32 launch(Mode mode, std.args::matches found)
    throws std.args::args_error, std.config::config_error, Usage, Setting, std.error::fault {
    std.config::config settings = defaults();
    o<std.string::string> file = found.value("config");
    switch (move file) {
    case variant o::some(move name):
        std.fs::path path = std.fs::path_from_utf8(name);
        task_scope(1) loading { await settings.load_file(&path); }
    case variant o::none: break;
    }
    layer(&settings, &found);
    array<std.string::string> requests = found.remaining();
    if (mode == Mode::show) {
        throw (len(requests) != 0usize) Usage {.message = "show takes no requests"};
        await std.console::print(settings.describe());
        return 0;
    }
    throw (mode == Mode::serve && len(requests) != 0usize)
        Usage {.message = "serve takes no requests"};
    throw (mode != Mode::serve && len(requests) == 0usize)
        Usage {.message = "echo, count, watch and repeat need requests"};
    std.service::options options = options_of(&settings);
    usize budget_bytes = std.convert::checked_usize(settings.get_u64("budget_bytes"));
    std.log::writer sink = std.log::writer::create(256usize);
    std.log::logger journal = sink.logger(level_of(&settings), format_of(&settings));
    std.log::fields start = std.log::fields::create();
    start.text("mode", core::enum_name(mode));
    start.number("capacity", options.capacity as i64);
    start.number("timeout_ms", std.convert::checked_i64(settings.get_u64("timeout_ms")));
    start.number("requests", std.convert::checked_i64(len(requests)));
    journal.log(std.log::level::info, "service starting", &start);
    std.string::string target = std.string::from_str(settings.get("log.file"));
    i32 status = 0;
    task_scope(2) logging {
        auto records = write_records(move sink, move target);
        auto program = run(mode, move requests, options, budget_bytes, move journal);
        status += await move program;
        u64 written = await move records;
        written as void;
    }
    return status;
}

/* The help of the command line; its declarations are valid, so no args_error occurs. */
std.string::string help_text() throws std.alloc::alloc_error {
    try {
        std.args::parser parser = command_line();
        return parser.help();
    } catch (std.args::args_error failure) {
        failure as void;
    }
    return std.string::create();
}

/* Without a mode, print the help: status 0 when there are no arguments at all or -h asked for
   it, 64 otherwise. */
async i32 main(const str[] arguments) {
    i32 status = 0;
    try {
        std.args::parser parser = command_line();
        std.args::matches found = parser.parse(arguments);
        o<std.string::string> named = found.value("mode");
        o<Mode> mode = o::none;
        switch (move named) {
        case variant o::some(move text):
            o<Mode> parsed = core::enum_from_name::<Mode>(text);
            switch (parsed) {
            case variant o::some(chosen): mode = o::some(*chosen);
            case variant o::none: throw Usage {.message = "unknown mode"};
            }
        case variant o::none: break;
        }
        switch (mode) {
        case variant o::some(chosen):
            if (found.help_requested() == true) {
                drop found;
                await std.console::print(parser.help());
            } else {
                status += await launch(*chosen, move found);
            }
        case variant o::none:
            if (len(arguments) > 1usize && found.help_requested() == false) { status += 64; }
            drop found;
            await std.console::print(parser.help());
        }
    } catch (std.args::args_error failure) {
        str name = core::enum_name(failure.code);
        usize index = failure.index;
        await std.console::print(f"{name} at argument {index}\n");
        await std.console::print(help_text());
        status += 64;
    } catch (Usage failure) {
        await std.console::print(f"{failure.message}\n");
        await std.console::print(help_text());
        status += 64;
    } catch (Setting failure) {
        await std.console::print(f"{failure.message}\n");
        status += 78;
    } catch (std.config::config_error failure) {
        str name = core::enum_name(failure.code);
        usize index = failure.index;
        await std.console::print(f"invalid configuration: {name} at {index}\n");
        status += 78;
    } catch (std.error::fault failure) {
        auto name = std.error::name(std.error::from_fault(failure));
        std.string::string line = std.string::from_str("service failed: ");
        line.append(name);
        line.append("\n");
        await std.console::print(move line);
        status += 70;
    }
    return status;
}

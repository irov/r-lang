module std.service;
import std.stream;
import std.tls;

/* R-SLIB-SERVICE-0001: what happens to a connection that arrives while capacity handler tasks
   run: reject closes it, wait leaves it to the listener until a handler task has ended. */
enum overflow { reject, wait };

/* R-SLIB-SERVICE-0001: the settings of one serve call: how many handler tasks may run at once,
   how long each connection may take from its accept and what happens beyond the capacity. An
   initialization that omits a field takes its declared initializer (Core R-INIT-0004). */
struct options {
    u32 capacity = 64;
    std.time::duration timeout = std.time::duration_from_seconds(30i64);
    std.service::overflow overflow = std.service::overflow::wait;
    o<std.time::duration> idle_timeout = o::none;
    bool stop_on_signals = false;
};

/* R-SLIB-SERVICE-0001: the largest capacity, the size of the handler group. */
const u32 max_capacity = 1024u32;

/* R-SLIB-SERVICE-0001: a stop request: drain waits for the running handlers at most the timeout,
   cancel cancels them at once. */
enum stop { drain, cancel };

/* R-SLIB-SERVICE-0002: the account of one serve call. */
struct report {
    u64 accepted;
    u64 rejected;
    u64 completed;
    u64 failed;
    u64 cancelled;
    o<std.error::error> last_failure;
};

/* The outcome that a handler task sends to the loop when it returns or throws; a cancelled
   task sends nothing. */
protected enum outcome { completed, failed(std.error::error) };

protected void record(std.service::report* account, std.service::outcome result) {
    switch (result) {
    case variant std.service::outcome::completed: account->completed += 1u64;
    case variant std.service::outcome::failed(error):
        account->failed += 1u64;
        account->last_failure = o::some(*error);
    }
}

/* The deadline of a connection accepted now, none when the clock cannot express it. */
protected o<std.time::instant> limit_after(std.time::duration timeout) {
    try {
        std.time::instant now = std.time::monotonic_now();
        return o::some(std.time::instant_add(now, timeout));
    } catch (std.time::time_error failure) {
        return o::none;
    }
}

/* The channel holds max_capacity outcomes and at most one per running handler, so the send
   never finds it full. */
protected void report_outcome(const (std.sync::sync_sender<std.service::outcome>)* done,
                              std.service::outcome result) {
    std.sync::try_send_result<std.service::outcome> sent = std.sync::try_send(done, result);
    drop sent;
}

/* One connection: the handler runs under the connection deadline. */
@generic<S: send & sync & unborrowed,
         H: copy & async fn once(arc S, std.net::tcp_connection) -> void throws(std.error::fault)>
@scoped
protected async void run(const (std.sync::sync_sender<std.service::outcome>)* done,
                         H handler,
                         arc S state,
                         std.net::tcp_connection connection,
                         o<std.time::instant> limit) {
    try {
        deadline (limit) {
            await (move handler).call(move state, move connection);
        }
        report_outcome(done, std.service::outcome::completed);
    } catch (std.error::fault failure) {
        report_outcome(done, std.service::outcome::failed(std.error::from_fault(failure)));
    }
}

/* Count the outcomes that handler tasks have already sent, without waiting. */
protected void collect(const std.sync::receiver<std.service::outcome>* finished,
                       std.service::report* account,
                       u32* active) {
    bool pending = true;
    while (*active > 0u32 && pending == true) {
        std.sync::try_recv_result<std.service::outcome> received = std.sync::try_recv(finished);
        switch (move received) {
        case variant std.sync::try_recv_result::received(move value):
            record(account, value);
            *active -= 1u32;
        case variant std.sync::try_recv_result::empty: pending = false;
        case variant std.sync::try_recv_result::disconnected: pending = false;
        }
    }
}

/* The next connection; one that its peer abandoned before the accept completed is skipped. */
@scoped
protected async std.net::tcp_connection accept_next(const std.net::tcp_listener* listener)
    throws std.net::net_error, std.async::start_error {
    while (true) {
        try {
            std.net::tcp_connection connection = await listener->accept();
            return move connection;
        } catch (std.net::net_error failure) {
            std.net::error_code code = failure.code;
            throw (code != std.net::error_code::connection_aborted &&
                   code != std.net::error_code::connection_reset) failure;
        }
    }
}

/* R-SLIB-SERVICE-0003: accept connections and run one handler task for each, with a clone of the
   shared state, until a stop request arrives or every stop sender is gone. */
@generic<S: send & sync & unborrowed,
         H: copy & async fn once(arc S, std.net::tcp_connection) -> void throws(std.error::fault)>
async std.service::report serve_with(std.net::tcp_listener listener,
                                     std.service::options settings,
                                     std.sync::receiver<std.service::stop> stop,
                                     arc S state,
                                     H handler)
    throws std.async::start_error, std.net::net_error, std.alloc::alloc_error {
    throw (settings.capacity > max_capacity ||
           (settings.capacity == 0u32 && settings.overflow == std.service::overflow::wait))
        std.async::start_error::scope_full;
    std.sync::sync_channel<std.service::outcome> factory =
        std.sync::sync_channel::<std.service::outcome>(1024usize);
    std.sync::sync_sender<std.service::outcome> done = std.sync::sync_sender(&factory);
    std.sync::receiver<std.service::outcome> finished = std.sync::sync_receiver(move factory);
    std.service::report account = {.accepted = 0u64, .rejected = 0u64, .completed = 0u64,
                                   .failed = 0u64, .cancelled = 0u64, .last_failure = o::none};
    u32 active = 0u32;
    std.service::stop mode = std.service::stop::drain;
    task_scope(1024) handlers {
        bool serving = true;
        while (serving == true) {
            collect(&finished, &account, &active);
            if (settings.overflow == std.service::overflow::wait && active >= settings.capacity) {
                // At capacity nothing is accepted until a handler task ends. Its outcome arrives
                // before its slot in the group is free, so the round then waits for a vacancy.
                task_scope(2) full {
                    auto next = finished.receive();
                    auto signal = stop.receive();
                    select (full) {
                    case o<std.service::outcome> result = await move next:
                        switch (result) {
                        case variant o::some(value):
                            record(&account, *value);
                            active -= 1u32;
                        case variant o::none: break;
                        }
                    case o<std.service::stop> request = await move signal:
                        switch (request) {
                        case variant o::some(value): mode = *value;
                        case variant o::none: mode = std.service::stop::drain;
                        }
                        serving = false;
                    }
                    full.cancel_all();
                }
                if (serving == true) { await handlers.vacancy(); }
                continue;
            }
            // Each round races the next accept against the stop receiver; the loser is
            // cancelled, which loses no stop request.
            o<std.net::tcp_connection> accepted = o::none;
            task_scope(2) waiting {
                auto incoming = accept_next(&listener);
                auto signal = stop.receive();
                select (waiting) {
                case std.net::tcp_connection connection = await move incoming:
                    accepted = o::some(move connection);
                case o<std.service::stop> request = await move signal:
                    switch (request) {
                    case variant o::some(value): mode = *value;
                    case variant o::none: mode = std.service::stop::drain;
                    }
                    serving = false;
                }
                waiting.cancel_all();
            }
            collect(&finished, &account, &active);
            switch (move accepted) {
            case variant o::some(move connection):
                if (active < settings.capacity) {
                    o<std.time::instant> limit = limit_after(settings.timeout);
                    try {
                        auto member = run(&done, handler, std.arc::clone(&state), move connection,
                                          limit);
                        std.async::detach(move member);
                        active += 1u32;
                        account.accepted += 1u64;
                    } catch (std.async::start_error failure) {
                        account.rejected += 1u64;
                    }
                } else {
                    drop connection;
                    account.rejected += 1u64;
                }
            case variant o::none: break;
            }
        }
        // A drain waits for the outcomes of the running handlers until none runs or the timeout
        // has passed; each round races one receive against the limit in a group of its own,
        // which takes no handler slot.
        if (mode == std.service::stop::drain) {
            try {
                std.time::instant now = std.time::monotonic_now();
                std.time::instant limit = std.time::instant_add(now, settings.timeout);
                bool waiting = active > 0u32;
                while (waiting == true) {
                    task_scope(1) pending {
                        auto next = finished.receive();
                        select (pending) {
                        case o<std.service::outcome> result = await move next:
                            switch (result) {
                            case variant o::some(value):
                                record(&account, *value);
                                active -= 1u32;
                            case variant o::none: waiting = false;
                            }
                        case until (limit): waiting = false;
                        }
                        pending.cancel_all();
                    }
                    if (active == 0u32) { waiting = false; }
                }
                limit as void;
            } catch (std.time::time_error failure) {
                failure as void;
            }
        }
        handlers.cancel_all();
    }
    // Every handler is terminal: each one that returned or threw has sent its outcome.
    drop done;
    collect(&finished, &account, &active);
    account.cancelled += active as u64;
    return account;
}

/* The state of a handler that shares none. */
protected struct stateless { u8 unused; };

/* R-SLIB-SERVICE-0003: serve_with for a handler without shared state. */
@generic<H: copy & async fn once(std.net::tcp_connection) -> void throws(std.error::fault)>
async std.service::report serve(std.net::tcp_listener listener,
                                std.service::options settings,
                                std.sync::receiver<std.service::stop> stop,
                                H handler)
    throws std.async::start_error, std.net::net_error, std.alloc::alloc_error {
    async fn void adapter(arc std.service::stateless state, std.net::tcp_connection connection)
        move(handler) throws std.error::fault {
        H current = handler;
        await (move current).call(move connection);
    }
    arc std.service::stateless state = new arc std.service::stateless {.unused = 0u8};
    return await serve_with(move listener, settings, move stop, move state, adapter);
}

/* ---- Several listeners, other transports, idle connections, signals and health (M30) ---- */

/* R-SLIB-SERVICE-0004: a listener of serve_all: TCP, a Unix-domain socket, or TCP whose
   connections each begin with a TLS handshake under the configuration. */
enum listener {
    tcp(std.net::tcp_listener),
    unix(std.net::unix_listener),
    tls { std.net::tcp_listener socket; arc std.tls::config settings; },
};

/* R-SLIB-SERVICE-0004: the largest number of listeners of one serve_all call. */
const u32 max_listeners = 16u32;

/* The transport of a connection of serve_all. */
protected enum transport {
    tcp(std.net::tcp_connection),
    unix(std.net::unix_stream),
    tls(std.tls::stream<std.net::tcp_connection>),
};

/* R-SLIB-SERVICE-0005: a connection of serve_all: a stream over the transport of its listener
   whose reads and writes each fail when they wait longer than the idle timeout. */
struct connection {
    protected transport inner;
    protected o<std.time::duration> idle;
    protected u32 origin;
};

/* R-SLIB-SERVICE-0005: the index of the listener that accepted the connection. */
u32 connection::source(const connection* this) {
    return this->origin;
}

protected o<std.time::instant> idle_limit(o<std.time::duration> idle) {
    switch (idle) {
    case variant o::some(timeout): return limit_after(*timeout);
    case variant o::none: break;
    }
    return o::none;
}

impl std.stream::Reader for connection {
    @scoped
    async usize read_into(const connection* this, u8[] target) throws std.error::fault {
        deadline (idle_limit(this->idle)) {
            switch (this->inner) {
            case variant transport::tcp(stream): task_scope(1) io { return await stream->read_into(target); }
            case variant transport::unix(stream): task_scope(1) io { return await stream->read_into(target); }
            case variant transport::tls(stream): task_scope(1) io { return await stream->read_into(target); }
            }
        }
        return 0usize;
    }
};

impl std.stream::Writer for connection {
    @scoped
    async usize write_from(const connection* this, const u8[] source) throws std.error::fault {
        deadline (idle_limit(this->idle)) {
            switch (this->inner) {
            case variant transport::tcp(stream): task_scope(1) io { return await stream->write_from(source); }
            case variant transport::unix(stream): task_scope(1) io { return await stream->write_from(source); }
            case variant transport::tls(stream): task_scope(1) io { return await stream->write_from(source); }
            }
        }
        return 0usize;
    }
    @scoped
    async void write_all_from(const connection* this, const u8[] source) throws std.error::fault {
        deadline (idle_limit(this->idle)) {
            switch (this->inner) {
            case variant transport::tcp(stream): task_scope(1) io { await stream->write_all_from(source); }
            case variant transport::unix(stream): task_scope(1) io { await stream->write_all_from(source); }
            case variant transport::tls(stream): task_scope(1) io { await stream->write_all_from(source); }
            }
        }
    }
    @scoped
    async void flush(const connection* this) throws std.error::fault {
        deadline (idle_limit(this->idle)) {
            switch (this->inner) {
            case variant transport::tcp(stream): task_scope(1) io { await stream->flush(); }
            case variant transport::unix(stream): task_scope(1) io { await stream->flush(); }
            case variant transport::tls(stream): task_scope(1) io { await stream->flush(); }
            }
        }
    }
    @scoped
    async void shutdown(const connection* this) throws std.error::fault {
        deadline (idle_limit(this->idle)) {
            switch (this->inner) {
            case variant transport::tcp(stream): task_scope(1) io { await stream->shutdown(); }
            case variant transport::unix(stream):
                task_scope(1) io { await std.net::unix_shutdown(stream, std.net::shutdown_direction::write, o::none); }
            case variant transport::tls(stream): task_scope(1) io { await stream->shutdown(); }
            }
        }
    }
};

impl std.stream::Stream for connection {};

protected struct health_core { atomic u32 serving; atomic u32 active; atomic u64 accepted; };

/* R-SLIB-SERVICE-0006: the state of a service as serve_all updates it, read from any task: whether
   it accepts connections, how many handlers run and how many connections it accepted. */
struct health { protected arc health_core core; };

health health::create() throws std.alloc::alloc_error {
    return health {.core = new arc health_core {.serving = 0u32, .active = 0u32, .accepted = 0u64}};
}

health health::share(const health* this) {
    return health {.core = std.arc::clone(&this->core)};
}

bool health::serving(const health* this) {
    return core::atomic_load(&this->core->serving, core::memory_order::acquire) != 0u32;
}

u32 health::active(const health* this) {
    return core::atomic_load(&this->core->active, core::memory_order::relaxed);
}

u64 health::accepted(const health* this) {
    return core::atomic_load(&this->core->accepted, core::memory_order::relaxed);
}

/* What an acceptor hands to the loop: a connection (transport), a TCP connection whose TLS
   handshake its handler performs (socket and settings), or the failure that ended the acceptor. */
protected struct arrival {
    o<transport> ready;
    o<std.net::tcp_connection> socket;
    o<arc std.tls::config> settings;
    o<std.net::net_error> failure;
    u32 origin;
};

protected arrival plain_arrival(transport accepted, u32 origin) {
    return arrival {.ready = o::some(move accepted), .socket = o::none, .settings = o::none,
                    .failure = o::none, .origin = origin};
}

protected arrival broken_arrival(std.net::net_error failure, u32 origin) {
    return arrival {.ready = o::none, .socket = o::none, .settings = o::none,
                    .failure = o::some(failure), .origin = origin};
}

/* The next connection of one listener. */
@scoped
protected async arrival accept_one(const std.service::listener* source, u32 origin)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    switch (*source) {
    case variant std.service::listener::tcp(socket):
        task_scope(1) io {
            std.net::tcp_connection connection = await accept_next(socket);
            return plain_arrival(transport::tcp(move connection), origin);
        }
    case variant std.service::listener::unix(socket):
        task_scope(1) io {
            std.net::unix_stream stream = await std.net::unix_accept(socket, o::none);
            return plain_arrival(transport::unix(move stream), origin);
        }
    case variant std.service::listener::tls(secured):
        task_scope(1) io {
            std.net::tcp_connection connection = await accept_next(&secured->socket);
            return arrival {.ready = o::none, .socket = o::some(move connection),
                            .settings = o::some(std.arc::clone(&secured->settings)), .failure = o::none,
                            .origin = origin};
        }
    }
    throw std.net::net_error {.code = std.net::error_code::other, .native_code = 0i64};
}

/* The next arrival of one listener; a failure of the listener is its last arrival. */
@scoped
protected async arrival next_arrival(const std.service::listener* source, u32 origin)
    throws std.async::start_error, std.alloc::alloc_error {
    try {
        task_scope(1) one { return await accept_one(source, origin); }
    } catch (std.net::net_error failure) {
        return broken_arrival(failure, origin);
    }
}

protected bool is_broken(const arrival* item) {
    switch (item->failure) {
    case variant o::some(failure): return true;
    case variant o::none: break;
    }
    return false;
}

/* Hands every connection of one listener to the loop until the loop is gone or the listener
   fails; a failure is handed on as the last arrival. */
@scoped
protected async void accept_from(const std.service::listener* source, u32 origin,
                                 std.sync::sync_sender<arrival> to) throws std.error::fault {
    bool open = true;
    while (open == true) {
        task_scope(1) round {
            arrival next = await next_arrival(source, origin);
            if (is_broken(&next) == true) { open = false; }
            std.sync::reserve_result<arrival> room = await std.sync::reserve(&to);
            switch (move room) {
            case variant std.sync::reserve_result::reserved(move permit): std.sync::send_permit(move permit, move next);
            case variant std.sync::reserve_result::disconnected:
                drop next;
                open = false;
            }
        }
    }
}

/* The connection of an arrival: a TLS handshake first for a secured one. */
@scoped
protected async std.service::connection open_arrival(arrival accepted, o<std.time::duration> idle)
    throws std.error::fault {
    o<transport> prepared = core::replace(&accepted.ready, o::none);
    switch (move prepared) {
    case variant o::some(move ready):
        return std.service::connection {.inner = move ready, .idle = idle, .origin = accepted.origin};
    case variant o::none: break;
    }
    o<std.net::tcp_connection> socket = core::replace(&accepted.socket, o::none);
    o<arc std.tls::config> settings = core::replace(&accepted.settings, o::none);
    switch (move socket) {
    case variant o::some(move connection):
        switch (move settings) {
        case variant o::some(move config):
            try {
                task_scope(1) io {
                    std.tls::stream<std.net::tcp_connection> stream = await std.tls::accept(move connection, &*config);
                    return std.service::connection {.inner = transport::tls(move stream), .idle = idle,
                                                    .origin = accepted.origin};
                }
            } catch (std.tls::tls_error refused) {
                refused as void;
            }
        case variant o::none: drop connection;
        }
    case variant o::none: drop settings;
    }
    throw std.io::io_error {.code = std.io::error_code::other, .native_code = 0i64};
}

/* One connection of serve_all: its handler runs under the connection deadline. */
@generic<S: send & sync & unborrowed,
         H: copy & async fn once(arc S, std.service::connection) -> void throws(std.error::fault)>
@scoped
protected async void run_any(const (std.sync::sync_sender<std.service::outcome>)* done,
                             H handler,
                             arc S state,
                             arrival accepted,
                             o<std.time::duration> idle,
                             o<std.time::instant> limit) {
    try {
        deadline (limit) {
            task_scope(1) start {
                std.service::connection connection = await open_arrival(move accepted, idle);
                await (move handler).call(move state, move connection);
            }
        }
        report_outcome(done, std.service::outcome::completed);
    } catch (std.error::fault failure) {
        report_outcome(done, std.service::outcome::failed(std.error::from_fault(failure)));
    }
}

/* The signals that stop serve_all with a drain when its options ask for them. */
protected struct signals { std.signal::listener terminate; std.signal::listener interrupt; };

protected o<signals> listen_signals(bool wanted) throws std.process::process_error {
    if (wanted == false) { return o::none; }
    return o::some(signals {.terminate = std.signal::kind::terminate.listen(),
                            .interrupt = std.signal::listen(std.signal::kind::interrupt)});
}

/* What one round of the loop waited for. */
protected enum round { arrived(arrival), stopped(std.service::stop), finished(o<std.service::outcome>) };

protected std.service::stop stop_of(o<std.service::stop> request) {
    switch (request) {
    case variant o::some(value): return *value;
    case variant o::none: break;
    }
    return std.service::stop::drain;
}

protected round arrival_round(o<arrival> item) {
    switch (move item) {
    case variant o::some(move value): return round::arrived(move value);
    case variant o::none: break;
    }
    return round::stopped(std.service::stop::drain);
}

protected void set_serving(const std.service::health* status, bool serving) {
    u32 flag = 0u32;
    if (serving == true) { flag = 1u32; }
    core::atomic_store(&status->core->serving, flag, core::memory_order::release);
}

protected void set_active(const std.service::health* status, u32 active) {
    core::atomic_store(&status->core->active, active, core::memory_order::relaxed);
}

/* R-SLIB-SERVICE-0007: serve_with over several listeners at once: each connection of each
   listener runs one handler task with a clone of the shared state and a std.service::connection;
   a stop request, the end of every stop sender or, when the options ask for it, SIGTERM or
   SIGINT stops the service, and its health follows it. A listener that fails stops the service,
   which then reports that failure. */
@generic<S: send & sync & unborrowed,
         H: copy & async fn once(arc S, std.service::connection) -> void throws(std.error::fault)>
async std.service::report serve_all(array<std.service::listener> listeners,
                                    std.service::options settings,
                                    std.sync::receiver<std.service::stop> stop,
                                    std.service::health status,
                                    arc S state,
                                    H handler)
    throws std.async::start_error, std.net::net_error, std.process::process_error, std.alloc::alloc_error {
    throw (settings.capacity > max_capacity ||
           (settings.capacity == 0u32 && settings.overflow == std.service::overflow::wait) ||
           len(listeners) > max_listeners as usize)
        std.async::start_error::scope_full;
    // The signals are watched before the health reports serving, so none sent after it is lost.
    o<signals> watch = listen_signals(settings.stop_on_signals);
    std.sync::sync_channel<std.service::outcome> factory =
        std.sync::sync_channel::<std.service::outcome>(1024usize);
    std.sync::sync_sender<std.service::outcome> done = std.sync::sync_sender(&factory);
    std.sync::receiver<std.service::outcome> finished = std.sync::sync_receiver(move factory);
    std.sync::sync_channel<arrival> arrival_factory = std.sync::sync_channel::<arrival>(16usize);
    std.sync::sync_sender<arrival> handing = std.sync::sync_sender(&arrival_factory);
    std.sync::receiver<arrival> arrivals = std.sync::sync_receiver(move arrival_factory);
    std.service::report account = {.accepted = 0u64, .rejected = 0u64, .completed = 0u64,
                                   .failed = 0u64, .cancelled = 0u64, .last_failure = o::none};
    u32 active = 0u32;
    std.service::stop mode = std.service::stop::drain;
    o<std.net::net_error> broken = o::none;
    task_scope(17) acceptors {
        const std.service::listener[] all = std.array::as_slice(&listeners);
        for (usize index = 0usize; index < len(all); index += 1usize) {
            auto member = accept_from(&all[index], index as u32, std.sync::clone_sync_sender(&handing));
            std.async::detach(move member);
        }
        drop handing;
        set_serving(&status, true);
        task_scope(1024) handlers {
            bool serving = true;
            while (serving == true) {
                collect(&finished, &account, &active);
                set_active(&status, active);
                bool full = settings.overflow == std.service::overflow::wait && active >= settings.capacity;
                // Each round races the next arrival, or the outcome of a handler at capacity,
                // against the stop requests; the losers are cancelled, which loses no request.
                o<round> waited = o::none;
                // The listeners leave watch for the round, so that its waits borrow a place.
                switch (move watch) {
                case variant o::some(move listening):
                    if (full == true) {
                        task_scope(4) waiting {
                            auto outcome = finished.receive();
                            auto request = stop.receive();
                            auto term = listening.terminate.next();
                            auto intr = listening.interrupt.next();
                            select (waiting) {
                            case o<std.service::outcome> result = await move outcome:
                                waited = o::some(round::finished(result));
                            case o<std.service::stop> asked = await move request:
                                waited = o::some(round::stopped(stop_of(asked)));
                            case u64 count = await move term:
                                count as void;
                                waited = o::some(round::stopped(std.service::stop::drain));
                            case u64 count = await move intr:
                                count as void;
                                waited = o::some(round::stopped(std.service::stop::drain));
                            }
                            waiting.cancel_all();
                        }
                    }
                    if (full == false) {
                        task_scope(4) waiting {
                            auto arrived = arrivals.receive();
                            auto request = stop.receive();
                            auto term = listening.terminate.next();
                            auto intr = listening.interrupt.next();
                            select (waiting) {
                            case o<arrival> item = await move arrived: waited = o::some(arrival_round(move item));
                            case o<std.service::stop> asked = await move request:
                                waited = o::some(round::stopped(stop_of(asked)));
                            case u64 count = await move term:
                                count as void;
                                waited = o::some(round::stopped(std.service::stop::drain));
                            case u64 count = await move intr:
                                count as void;
                                waited = o::some(round::stopped(std.service::stop::drain));
                            }
                            waiting.cancel_all();
                        }
                    }
                    watch = o::some(move listening);
                case variant o::none:
                    watch = o::none;
                    if (full == true) {
                        task_scope(2) waiting {
                            auto outcome = finished.receive();
                            auto request = stop.receive();
                            select (waiting) {
                            case o<std.service::outcome> result = await move outcome:
                                waited = o::some(round::finished(result));
                            case o<std.service::stop> asked = await move request:
                                waited = o::some(round::stopped(stop_of(asked)));
                            }
                            waiting.cancel_all();
                        }
                    }
                    if (full == false) {
                        task_scope(2) waiting {
                            auto arrived = arrivals.receive();
                            auto request = stop.receive();
                            select (waiting) {
                            case o<arrival> item = await move arrived: waited = o::some(arrival_round(move item));
                            case o<std.service::stop> asked = await move request:
                                waited = o::some(round::stopped(stop_of(asked)));
                            }
                            waiting.cancel_all();
                        }
                    }
                }
                round next = round::stopped(std.service::stop::drain);
                switch (move waited) {
                case variant o::some(move value): next = move value;
                case variant o::none: break;
                }
                switch (move next) {
                case variant round::finished(result):
                    switch (*result) {
                    case variant o::some(value):
                        record(&account, *value);
                        active -= 1u32;
                    case variant o::none: break;
                    }
                case variant round::stopped(asked):
                    mode = *asked;
                    serving = false;
                case variant round::arrived(move item):
                    o<std.net::net_error> failure = item.failure;
                    switch (failure) {
                    case variant o::some(error):
                        broken = o::some(*error);
                        serving = false;
                        drop item;
                    case variant o::none:
                        collect(&finished, &account, &active);
                        if (active < settings.capacity) {
                            o<std.time::instant> limit = limit_after(settings.timeout);
                            try {
                                auto member = run_any(&done, handler, std.arc::clone(&state), move item,
                                                      settings.idle_timeout, limit);
                                std.async::detach(move member);
                                active += 1u32;
                                account.accepted += 1u64;
                                core::atomic_fetch_add(&status.core->accepted, 1u64, core::memory_order::relaxed)
                                    as void;
                            } catch (std.async::start_error rejected) {
                                rejected as void;
                                account.rejected += 1u64;
                            }
                        } else {
                            drop item;
                            account.rejected += 1u64;
                        }
                    }
                }
                if (serving == true && full == true) { await handlers.vacancy(); }
            }
            set_serving(&status, false);
            acceptors.cancel_all();
            if (mode == std.service::stop::drain) {
                try {
                    std.time::instant now = std.time::monotonic_now();
                    std.time::instant limit = std.time::instant_add(now, settings.timeout);
                    bool waiting = active > 0u32;
                    while (waiting == true) {
                        task_scope(1) pending {
                            auto next = finished.receive();
                            select (pending) {
                            case o<std.service::outcome> result = await move next:
                                switch (result) {
                                case variant o::some(value):
                                    record(&account, *value);
                                    active -= 1u32;
                                case variant o::none: waiting = false;
                                }
                            case until (limit): waiting = false;
                            }
                            pending.cancel_all();
                        }
                        set_active(&status, active);
                        if (active == 0u32) { waiting = false; }
                    }
                    limit as void;
                } catch (std.time::time_error failure) {
                    failure as void;
                }
            }
            handlers.cancel_all();
        }
        acceptors.cancel_all();
    }
    drop done;
    drop arrivals;
    drop watch;
    collect(&finished, &account, &active);
    account.cancelled += active as u64;
    set_active(&status, 0u32);
    drop listeners;
    switch (broken) {
    case variant o::some(failure): throw *failure;
    case variant o::none: break;
    }
    return account;
}

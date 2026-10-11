module example.c_bridge.signals;

// A C signal handler written in R (@signal_handler). The compiler proves that the handler and the
// @signal_safe functions it calls allocate nothing, block nowhere, touch no thread_local or
// runtime state and run no cleanup; the imports it calls are asserted async-signal-safe.
@link(name = "system.libc", kind = "system")
@header("signal.h")
extern "C" {
    @c_constant(name = "SIGUSR1")
    const c_int user_signal = 30i32 as c_int;
    @c_constant(name = "SIG_UNBLOCK")
    const c_int unblock_mask = 2i32 as c_int;
    @c_constant(name = "SIG_SETMASK")
    const c_int restore_mask = 3i32 as c_int;
    @safety("BRIDGE-SIGNAL", "Every handler installed is an @signal_handler definition or the default")
    raw fn?(c_int) -> void signal(c_int number, raw fn?(c_int) -> void handler);
    @signal_safe
    @safety("BRIDGE-RAISE", "The signal has a handler installed by this command")
    c_int raise(c_int number);
    // On arm64-apple-darwin sigset_t is a 32-bit mask; the header proves the prototypes.
    @safety("BRIDGE-SIGEMPTYSET", "The set is a live sigset_t")
    c_int sigemptyset(raw c_uint* set);
    @safety("BRIDGE-SIGADDSET", "The set is a live sigset_t")
    c_int sigaddset(raw c_uint* set, c_int number);
    @safety("BRIDGE-SIGMASK", "Both sets are live sigset_t objects or null")
    c_int pthread_sigmask(c_int how, raw const c_uint*? set, raw c_uint*? old);
}

// Plain statics the handler writes: the thread that raises the signal reads them after the
// handler returns, so a signal fence on each side orders the accesses.
u32 deliveries = 0u32;
u32 last_number = 0u32;

@signal_safe
protected void record(c_int number) {
    unsafe {
        deliveries += 1u32;
        last_number = number as u32;
    }
    core::signal_fence(core::memory_order::release);
}

@signal_handler
@safety("BRIDGE-ON-SIGNAL", "Installed by signal() for SIGUSR1 while the command runs")
extern "C" void on_user_signal(c_int number) {
    record(number);
}

// Runs on a thread of the blocking pool. The threads of an async program start with the signal
// blocked; this one accepts it while it raises, so each raise runs the handler on this thread
// before raise returns.
u32 interrupt(u32 count) {
    u32 synchronous = 0u32;
    c_uint accepted = 0u32 as c_uint;
    c_uint previous = 0u32 as c_uint;
    unsafe {
        sigemptyset(&accepted as raw c_uint*) as void;
        sigaddset(&accepted as raw c_uint*, user_signal) as void;
        pthread_sigmask(unblock_mask, &accepted as raw const c_uint*, &previous as raw c_uint*) as void;
    }
    for (u32 round = 0u32; round < count; round += 1u32) {
        unsafe {
            raise(user_signal) as void;
        }
        core::signal_fence(core::memory_order::acquire);
        unsafe {
            if (deliveries == round + 1u32) {
                synchronous += 1u32;
            }
        }
    }
    unsafe {
        pthread_sigmask(restore_mask, &previous as raw const c_uint*, null) as void;
    }
    return synchronous;
}

async std.string::string deliver(u32 count) throws std.async::start_error, std.alloc::alloc_error {
    raw fn(c_int) -> void handler = on_user_signal;
    raw fn?(c_int) -> void installed = handler;
    unsafe {
        signal(user_signal, installed) as void;
    }
    u32 synchronous = await std.async::blocking(interrupt, count);
    unsafe {
        signal(user_signal, null) as void;
        u32 handled = deliveries;
        u32 number = last_number;
        return f"raised={count} handled={handled} synchronous={synchronous} signal={number}\n";
    }
}

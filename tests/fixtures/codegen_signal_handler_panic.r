module test.codegen.signal_handler_panic;

/* Core R-FFI-0067, R-ERR-0006 (B11): a panic in a signal-safe function cannot unwind through the
   interrupted code, so it reports its category and aborts the process from the handler. */

@header("signal.h")
extern "C" {
    @c_constant(name = "SIGUSR1")
    const c_int user_signal = 30i32 as c_int;
    @safety("SIGNAL-INSTALL", "every installed handler is an @signal_handler definition")
    raw fn?(c_int) -> void signal(c_int number, raw fn?(c_int) -> void handler);
    @signal_safe
    @safety("SIGNAL-RAISE", "the signal has a handler installed by this program")
    c_int raise(c_int number);
}

u32[4] slots = {};

@signal_safe
protected void store(u32 index, u32 value) {
    unsafe {
        slots[index] = value;
    }
}

@signal_handler
@safety("ON-SIGNAL", "installed by signal() for SIGUSR1")
extern "C" void on_signal(c_int number) {
    store(number as u32, 1u32);
}

i32 main() {
    raw fn(c_int) -> void handler = on_signal;
    raw fn?(c_int) -> void installed = handler;
    unsafe {
        signal(user_signal, installed) as void;
        raise(user_signal) as void;
    }
    return 0;
}

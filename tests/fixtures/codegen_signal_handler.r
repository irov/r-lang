module test.codegen.signal_handler;

/* Core R-FFI-0067, R-OBJ-0013, Library R-LIB-0028 (B11): an R function installed as a C signal
   handler. The handler and the @signal_safe functions it calls are proved free of allocation,
   blocking, run-time state and cleanup; they count the deliveries in an atomic and a mutable
   static, keep the last number through an address constant and publish with a signal fence that
   the interrupted thread pairs. */

@header("signal.h")
extern "C" {
    @c_constant(name = "SIGUSR1")
    const c_int user_signal = 30i32 as c_int;
    @c_constant(name = "SIGUSR2")
    const c_int second_signal = 31i32 as c_int;
    @safety("SIGNAL-INSTALL", "every installed handler is an @signal_handler definition")
    raw fn?(c_int) -> void signal(c_int number, raw fn?(c_int) -> void handler);
    @signal_safe
    @safety("SIGNAL-RAISE", "the signal has a handler installed by this program")
    c_int raise(c_int number);
}

struct Log {
    u32 count;
    u32[4] numbers;
};

atomic u32 received = 0u32;
Log log = {};
raw Log* log_address = &log as raw Log*;

@signal_safe
protected u32 slot_of(u32 count) {
    return count & 3u32;
}

@signal_safe
protected void note(c_int number) {
    u32 count = core::atomic_fetch_add(&received, 1u32, core::memory_order::relaxed);
    unsafe {
        log_address->numbers[slot_of(count)] = number as u32;
        log_address->count += 1u32;
    }
    core::signal_fence(core::memory_order::release);
}

@signal_handler
@safety("ON-SIGNAL", "installed by signal() for SIGUSR1 and SIGUSR2")
extern "C" void on_signal(c_int number) {
    note(number);
}

i32 main() {
    raw fn(c_int) -> void handler = on_signal;
    raw fn?(c_int) -> void installed = handler;
    unsafe {
        signal(user_signal, installed) as void;
        signal(second_signal, installed) as void;
        if ((raise(user_signal) != 0i32 as c_int) || (raise(second_signal) != 0i32 as c_int)) {
            return 1;
        }
        raise(user_signal) as void;
        signal(user_signal, null) as void;
        signal(second_signal, null) as void;
    }
    core::signal_fence(core::memory_order::acquire);
    if (core::atomic_load(&received, core::memory_order::relaxed) != 3u32) {
        return 2;
    }
    unsafe {
        if (log.count != 3u32) {
            return 3;
        }
        if ((log.numbers[0] != user_signal as u32) || (log.numbers[1] != second_signal as u32) ||
            (log.numbers[2] != user_signal as u32)) {
            return 4;
        }
    }
    return 0;
}

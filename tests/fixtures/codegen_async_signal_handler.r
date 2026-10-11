module test.codegen.async_signal_handler;
import std.time;

/* Core R-FFI-0067 (B11): a signal handler written in R serves a program whose async functions
   signal the process between awaits. The tasks run on Dispatch workers, so the signal is sent to
   the process (kill), arrives asynchronously on a thread that accepts it, and each round waits
   for its delivery before the next: ordinary signals pending together are delivered once. */

@header("signal.h")
extern "C" {
    @c_constant(name = "SIGUSR1")
    const c_int user_signal = 30i32 as c_int;
    @safety("SIGNAL-INSTALL", "every installed handler is an @signal_handler definition")
    raw fn?(c_int) -> void signal(c_int number, raw fn?(c_int) -> void handler);
    @signal_safe
    @safety("SIGNAL-KILL", "the signal goes to this process, which handles it")
    c_int kill(c_int process, c_int number);
}

@header("unistd.h")
extern "C" {
    @signal_safe
    @safety("GETPID", "no precondition")
    c_int getpid();
}

atomic u32 received = 0u32;
atomic u32 last_number = 0u32;

@signal_handler
@safety("ON-SIGNAL", "installed by signal() for SIGUSR1")
extern "C" void on_signal(c_int number) {
    core::atomic_store(&last_number, number as u32, core::memory_order::relaxed);
    core::atomic_fetch_add(&received, 1u32, core::memory_order::release) as void;
}

protected async bool delivered(u32 expected)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    for (u32 wait = 0u32; wait < 5000u32; wait += 1u32) {
        if (core::atomic_load(&received, core::memory_order::acquire) == expected) {
            return true;
        }
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    }
    return false;
}

protected async u32 signal_rounds(u32 rounds)
    throws std.time::time_error, std.time::duration_error, std.async::start_error {
    u32 done = 0u32;
    for (u32 round = 0u32; round < rounds; round += 1u32) {
        unsafe {
            kill(getpid(), user_signal) as void;
        }
        if ((await delivered(round + 1u32)) == true) {
            done += 1u32;
        }
    }
    return done;
}

async i32 main() {
    raw fn(c_int) -> void handler = on_signal;
    raw fn?(c_int) -> void installed = handler;
    unsafe {
        signal(user_signal, installed) as void;
    }
    try {
        u32 rounds = await signal_rounds(5u32);
        if (rounds != 5u32) {
            return 1;
        }
    } catch (std.time::time_error failure) {
        return 2;
    } catch (std.time::duration_error failure) {
        return 2;
    } catch (std.async::start_error failure) {
        return 2;
    }
    unsafe {
        signal(user_signal, null) as void;
    }
    if (core::atomic_load(&last_number, core::memory_order::relaxed) != user_signal as u32) {
        return 3;
    }
    return 0;
}

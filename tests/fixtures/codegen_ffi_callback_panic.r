module test.codegen.ffi_callback_panic;

/* L39 (Core R-ERR-0006): a panic that reaches the C boundary of a callback aborts there with its
   report after the R frames of the callback ran their cleanup; it never unwinds through C. */

struct Counter {
    i32 value;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-USERDATA-STORE", "token is one live obligation; retain and release are live callbacks")
    void probe_userdata_store(raw const void* token,
                              raw fn(raw const void*) -> raw const void* retain,
                              raw fn(raw const void*) -> void release);

    @safety("PROBE-USERDATA-SHARE", "The function has no preconditions")
    c_int probe_userdata_share();
}

@callback
@safety("PROBE-PANIC-RETAIN", "the runtime is initialized")
extern "C" raw const void* probe_panic_retain(raw const void* token) {
    token as void;
    panic("callback failed");
}

@callback
@safety("PROBE-PANIC-RELEASE", "the runtime is initialized")
extern "C" void probe_panic_release(raw const void* token) {
    token as void;
}

i32 main() {
    arc Counter owner = new arc Counter { .value = 5 };
    raw const Counter* token = std.arc::into_raw(move owner);
    unsafe {
        probe_userdata_store(token as raw const void*, probe_panic_retain, probe_panic_release);
        probe_userdata_share() as void;
    }
    return 0;
}

module test.codegen.ffi_userdata;

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

    @safety("PROBE-USERDATA-COUNT", "The function has no preconditions")
    c_int probe_userdata_count();

    @safety("PROBE-USERDATA-CLEAR", "Every held token is released exactly once")
    void probe_userdata_clear();
}

@callback
@safety("PROBE-COUNTER-RETAIN", "token is one live arc Counter obligation; the runtime is initialized")
extern "C" raw const void* probe_counter_retain(raw const void* token) {
    unsafe {
        arc Counter owner = std.arc::from_raw(token as raw const Counter*);
        arc Counter duplicate = std.arc::clone(&owner);
        /* Both owners become obligations again: the input one and the retained one. */
        std.arc::into_raw(move owner) as void;
        raw const Counter* additional = std.arc::into_raw(move duplicate);
        return additional as raw const void*;
    }
}

@callback
@safety("PROBE-COUNTER-RELEASE", "token is one live arc Counter obligation and is consumed")
extern "C" void probe_counter_release(raw const void* token) {
    unsafe {
        arc Counter owner = std.arc::from_raw(token as raw const Counter*);
        drop owner;
    }
}

i32 main() {
    arc Counter owner = new arc Counter { .value = 5 };
    arc Counter keeper = std.arc::clone(&owner);
    raw const Counter* token = std.arc::into_raw(move owner);
    i32 outcome = 0;
    unsafe {
        probe_userdata_store(token as raw const void*, probe_counter_retain, probe_counter_release);
        if (std.arc::strong_count(&keeper) != 2) {
            outcome = 1;
        }
        if (probe_userdata_share() != 2i32 as c_int) {
            outcome = 2;
        }
        /* The kept owner plus two C obligations share one allocation. */
        if (std.arc::strong_count(&keeper) != 3) {
            outcome = 3;
        }
        probe_userdata_clear();
        if (probe_userdata_count() != 0i32 as c_int) {
            outcome = 4;
        }
        if (std.arc::strong_count(&keeper) != 1) {
            outcome = 5;
        }
    }
    return outcome;
}

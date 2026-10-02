module test.codegen.async_ffi_types;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_handle", kind = "struct")
    opaque struct Handle;

    @c_constant(name = "PROBE_STATUS_READY")
    const c_int PROBE_STATUS_READY = 3i32 as c_int;

    c_int probe_counter;
    thread_local c_int probe_thread_slot;

    @safety("PROBE-OPEN", "The result is a live handle owned by the caller")
    raw Handle* probe_open(c_int seed);

    @safety("PROBE-READ", "handle shall be a live handle for the whole call")
    c_int probe_read(raw const Handle* handle);

    @safety("PROBE-CLOSE", "handle shall be a live handle and is consumed")
    void probe_close(raw Handle* handle);

    @safety("PROBE-SUM", "count shall equal the number of int arguments that follow")
    c_int probe_sum(c_int count, ...);
}

protected async i32 probe_all() {
    i32 outcome = 0;
    unsafe {
        raw Handle* handle = probe_open(12i32 as c_int);
        c_int value = probe_read(handle as raw const Handle*);
        c_int total = probe_sum(2i32 as c_int, PROBE_STATUS_READY, value);
        probe_close(handle);
        probe_counter = total;
        probe_thread_slot = probe_counter;
        if (total != 15i32 as c_int) {
            outcome = 1;
        }
        if (probe_thread_slot != 15i32 as c_int) {
            outcome = 2;
        }
    }
    return outcome;
}

async i32 main() {
    try {
        i32 outcome = await probe_all();
        return outcome;
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}

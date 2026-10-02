module test.codegen.ffi_types;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_handle", kind = "struct")
    opaque struct Handle;

    @c_type(name = "probe_session", kind = "typedef")
    opaque struct Session;

    @c_constant(name = "PROBE_OK")
    const c_int PROBE_OK = 0i32 as c_int;

    @c_constant(name = "PROBE_NEGATIVE")
    const c_int PROBE_NEGATIVE = -7i32 as c_int;

    @c_constant(name = "PROBE_LIMIT")
    const c_uint PROBE_LIMIT = 4000000000u32 as c_uint;

    @c_constant(name = "PROBE_STATUS_READY")
    const c_int PROBE_STATUS_READY = 3i32 as c_int;

    c_int probe_counter;
    const c_int probe_version;
    thread_local c_int probe_thread_slot;

    @safety("PROBE-OPEN", "The result is a live handle owned by the caller")
    raw Handle* probe_open(c_int seed);

    @safety("PROBE-READ", "handle shall be a live handle for the whole call")
    c_int probe_read(raw const Handle* handle);

    @safety("PROBE-CLOSE", "handle shall be a live handle and is consumed")
    void probe_close(raw Handle* handle);

    @safety("PROBE-SESSION-OPEN", "The result is a live session owned by the caller")
    raw Session* probe_session_open(c_int id);

    @safety("PROBE-SESSION-ID", "session shall be a live session for the whole call")
    c_int probe_session_id(raw const Session* session);

    @safety("PROBE-SESSION-CLOSE", "session shall be a live session and is consumed")
    void probe_session_close(raw Session* session);

    @safety("PROBE-SUM", "count shall equal the number of int arguments that follow")
    c_int probe_sum(c_int count, ...);
}

i32 main() {
    i32 outcome = 0;
    unsafe {
        raw Handle* handle = probe_open(40i32 as c_int);
        raw Session* session = probe_session_open(9i32 as c_int);
        c_int total = probe_sum(3i32 as c_int, 1i32 as c_int, 2i32 as c_int, PROBE_STATUS_READY);
        if (probe_read(handle as raw const Handle*) != 40i32 as c_int) {
            outcome = 1;
        }
        probe_close(handle);
        if (probe_session_id(session as raw const Session*) != 9i32 as c_int) {
            outcome = 2;
        }
        probe_session_close(session);
        if (total != 6i32 as c_int) {
            outcome = 3;
        }
        if (PROBE_OK != 0i32 as c_int) {
            outcome = 4;
        }
        if (PROBE_NEGATIVE != -7i32 as c_int) {
            outcome = 5;
        }
        if (PROBE_LIMIT != 4000000000u32 as c_uint) {
            outcome = 6;
        }
        probe_counter = 5i32 as c_int;
        if (probe_counter != 5i32 as c_int) {
            outcome = 7;
        }
        if (probe_version != 21i32 as c_int) {
            outcome = 8;
        }
        probe_thread_slot = probe_counter;
        if (probe_thread_slot != 5i32 as c_int) {
            outcome = 9;
        }
    }
    return outcome;
}

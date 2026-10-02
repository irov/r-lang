module ffi.opaque_typedef_no_header;

@link(name = "probe", kind = "static")
@abi("probe-abi")
extern "C" {
    @c_type(name = "probe_session", kind = "typedef")
    opaque struct Session;

    @safety("PROBE-SESSION-OPEN", "The result is a live session owned by the caller")
    raw Session* probe_session_open(c_int id);
}

i32 main() {
    return 0;
}

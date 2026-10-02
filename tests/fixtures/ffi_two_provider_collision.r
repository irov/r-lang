module ffi.two_provider_collision;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

@link(name = "system.libc", kind = "system")
@header("stdlib.h")
extern "C" {
    @link_name("probe_increment")
    @safety("PROBE-INCREMENT-AGAIN", "The function has no preconditions")
    c_int probe_increment_again(c_int value);
}

i32 main() {
    return 0;
}

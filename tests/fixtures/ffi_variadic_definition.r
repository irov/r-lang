module ffi.variadic_definition;

@safety("PROBE-BAD", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_bad(c_int count, ...) {
    return count;
}

i32 main() {
    return 0;
}

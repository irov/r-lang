module ffi.import_reserved_no_header;

@abi("libc-record")
extern "C" {
    @safety("ABS", "The function has no preconditions")
    c_int abs(c_int value);
}

i32 main() {
    return 0;
}
